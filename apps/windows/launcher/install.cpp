#include "install.h"

#include <shlobj.h>

#include <algorithm>
#include <chrono>
#include <set>
#include <unordered_map>

#include "codec.h"

namespace launcher {
namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

double elapsed_ms(Clock::time_point since) {
    return std::chrono::duration<double, std::milli>(Clock::now() - since).count();
}

Problem classify(DWORD code, bool repairing) {
    switch (code) {
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL:
        return Problem::disk_full;
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
    case ERROR_USER_MAPPED_FILE:
        return Problem::in_use;
    case ERROR_ACCESS_DENIED:
        // Replacing a file that a running copy has loaded is refused as access denied.
        return repairing ? Problem::in_use : Problem::not_writable;
    case ERROR_WRITE_PROTECT:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_DIRECTORY:
    case ERROR_INVALID_DRIVE:
    case ERROR_NOT_READY:
        return Problem::not_writable;
    default:
        return Problem::io;
    }
}

[[noreturn]] void fail(DWORD code, const fs::path& path, bool repairing = false) {
    throw Error{classify(code, repairing), code, path};
}

struct Handle {
    HANDLE value;
    explicit Handle(HANDLE handle) : value(handle) {}
    ~Handle() {
        if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    bool valid() const { return value && value != INVALID_HANDLE_VALUE; }
};

std::wstring widen(std::string_view utf8) {
    std::wstring out(MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), static_cast<int>(out.size()));
    std::replace(out.begin(), out.end(), L'/', L'\\');
    return out;
}

std::wstring fold(std::wstring text) {
    for (auto& c : text) {
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
    }
    return text;
}

std::uint64_t file_time(const FILETIME& time) {
    return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}

void write_file(const fs::path& path, std::span<const std::uint8_t> data) {
    Handle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.valid()) fail(GetLastError(), path);
    for (std::size_t done = 0; done < data.size();) {
        const auto part = static_cast<DWORD>(std::min<std::size_t>(data.size() - done, 16u << 20));
        DWORD written = 0;
        if (!WriteFile(file.value, data.data() + done, part, &written, nullptr)) fail(GetLastError(), path);
        done += written;
    }
}

/// Whether the file on disk has exactly this size and SHA-256.
bool matches(const fs::path& path, const FileEntry& entry) {
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                            FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    LARGE_INTEGER size{};
    if (!file.valid() || !GetFileSizeEx(file.value, &size) || static_cast<std::uint64_t>(size.QuadPart) != entry.size) {
        return false;
    }
    if (entry.size == 0) return sha256({}) == entry.sha256;
    Handle mapping(CreateFileMappingW(file.value, nullptr, PAGE_READONLY, 0, 0, nullptr));
    if (!mapping.valid()) return false;
    const void* view = MapViewOfFile(mapping.value, FILE_MAP_READ, 0, 0, 0);
    if (!view) return false;
    const bool same = sha256({static_cast<const std::uint8_t*>(view), static_cast<std::size_t>(entry.size)}) == entry.sha256;
    UnmapViewOfFile(view);
    return same;
}

/// Deletes a file or folder tree; links are removed, never followed. False if anything is left.
bool delete_tree(const fs::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
    if (attributes & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(path.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) return DeleteFileW(path.c_str()) != 0;
    if (!(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        WIN32_FIND_DATAW data;
        HANDLE find = FindFirstFileExW((path / L"*").c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr,
                                       FIND_FIRST_EX_LARGE_FETCH);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                const std::wstring_view name = data.cFileName;
                if (name != L"." && name != L"..") delete_tree(path / name);
            } while (FindNextFileW(find, &data));
            FindClose(find);
        }
    }
    return RemoveDirectoryW(path.c_str()) != 0;
}

struct Found {
    std::wstring path;  // relative, '\' separated
    std::uint64_t size = 0, time = 0;
    bool link = false;  // reparse point: never trusted, never followed
};

void scan(const fs::path& folder, const std::wstring& prefix, std::vector<Found>& out) {
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileExW((folder / L"*").c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr,
                                   FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring name = data.cFileName;
        if (name == L"." || name == L"..") continue;
        const bool link = data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !link) {
            scan(folder / name, prefix + name + L"\\", out);
        } else {
            out.push_back({prefix + name, (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow,
                           file_time(data.ftLastWriteTime), link});
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

/// The payload's files as they should appear on disk.
struct Layout {
    const Payload& payload;
    std::vector<std::wstring> paths;  // relative, '\' separated, manifest order

    explicit Layout(const Payload& p) : payload(p) {
        for (const auto& file : p.files) paths.push_back(widen(file.path));
    }
};

/// Result of comparing a folder with the manifest.
struct Comparison {
    std::vector<std::size_t> suspect;  // manifest indices that are missing or changed
    std::vector<Found> extras;         // files that should not be there (links included)
    std::vector<std::uint64_t> times;  // write time of every manifest file found, for the stamp
    bool complete = true;              // every manifest file is there with the right size
    bool clean() const { return suspect.empty() && extras.empty(); }
};

Comparison compare(const Layout& layout, const fs::path& folder, const std::optional<std::vector<std::uint64_t>>& stamp) {
    std::vector<Found> found;
    scan(folder, L"", found);
    std::unordered_map<std::wstring, std::size_t> index;
    for (std::size_t i = 0; i < layout.paths.size(); ++i) index.emplace(fold(layout.paths[i]), i);
    Comparison result;
    result.times.assign(layout.paths.size(), 0);
    std::vector<bool> seen(layout.paths.size(), false);
    for (auto& file : found) {
        const auto key = fold(file.path);
        const auto it = index.find(key);
        if (it == index.end() || file.link) {
            if (key != stamp_name) result.extras.push_back(std::move(file));
            continue;
        }
        const auto i = it->second;
        seen[i] = true;
        result.times[i] = file.time;
        if (file.size != layout.payload.files[i].size) result.complete = false;
        if (file.size != layout.payload.files[i].size || !stamp || (*stamp)[i] != file.time) result.suspect.push_back(i);
    }
    for (std::size_t i = 0; i < seen.size(); ++i) {
        if (!seen[i]) {
            result.suspect.push_back(i);
            result.complete = false;
        }
    }
    return result;
}

std::optional<std::vector<std::uint64_t>> read_stamp(const Layout& layout, const fs::path& folder) {
    Handle file(CreateFileW((folder / stamp_name).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
    LARGE_INTEGER size{};
    if (!file.valid() || !GetFileSizeEx(file.value, &size) || size.QuadPart > (1 << 22)) return std::nullopt;
    Bytes bytes(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    if (!ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) || read != bytes.size()) {
        return std::nullopt;
    }
    return decode_stamp(bytes, layout.payload.id, layout.paths.size());
}

void write_stamp(const Layout& layout, const fs::path& folder, const std::vector<std::uint64_t>& times) {
    write_file(folder / stamp_name, encode_stamp(layout.payload.id, times));
}

Buffer unpack(const Payload& payload) {
    try {
        return decompress(payload);
    } catch (const FormatError&) {
        throw Error{Problem::corrupt};
    }
}

std::span<const std::uint8_t> contents(const Buffer& data, const FileEntry& file) {
    return data.view().subspan(static_cast<std::size_t>(file.offset), static_cast<std::size_t>(file.size));
}

void create_folders(const fs::path& base, const Layout& layout) {
    std::set<std::wstring> folders;
    for (const auto& path : layout.paths) {
        for (auto slash = path.find(L'\\'); slash != std::wstring::npos; slash = path.find(L'\\', slash + 1)) {
            folders.insert(path.substr(0, slash));
        }
    }
    // Sorted, so every parent comes before its children.
    for (const auto& folder : folders) {
        if (!CreateDirectoryW((base / folder).c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
            fail(GetLastError(), base / folder);
        }
    }
}

/// Creates `path` and its parents, reporting the Win32 error of the step that fails.
void make_folders(const fs::path& path) {
    if (!CreateDirectoryW(path.c_str(), nullptr)) {
        DWORD code = GetLastError();
        if (code == ERROR_PATH_NOT_FOUND && path.has_relative_path() && path.parent_path() != path) {
            make_folders(path.parent_path());
            code = CreateDirectoryW(path.c_str(), nullptr) ? ERROR_SUCCESS : GetLastError();
        }
        if (code != ERROR_SUCCESS && code != ERROR_ALREADY_EXISTS) fail(code, path);
    }
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) fail(ERROR_DIRECTORY, path);
}

std::wstring process_suffix() { return L"-" + std::to_wstring(GetCurrentProcessId()); }

/// Repairs a folder whose comparison found problems: removes extras and rewrites files that fail the hash.
std::size_t repair(const Layout& layout, const fs::path& folder, const Comparison& comparison, double& decompress_ms) {
    std::size_t changed = 0;
    for (const auto& extra : comparison.extras) {
        const auto path = folder / extra.path;
        if (!delete_tree(path)) fail(GetLastError(), path, true);
        ++changed;
    }
    // A changed write time alone (a copy that kept the contents) is fine; only rewrite what fails the hash.
    std::vector<char> bad(comparison.suspect.size(), 0);
    parallel_for(comparison.suspect.size(), [&](std::size_t i) {
        const auto index = comparison.suspect[i];
        bad[i] = !matches(folder / layout.paths[index], layout.payload.files[index]);
    });
    if (std::find(bad.begin(), bad.end(), 1) != bad.end()) {
        const auto start = Clock::now();
        const Buffer data = unpack(layout.payload);
        decompress_ms = elapsed_ms(start);
        create_folders(folder, layout);
        for (std::size_t i = 0; i < bad.size(); ++i) {
            if (!bad[i]) continue;
            const auto index = comparison.suspect[i];
            const auto& file = layout.payload.files[index];
            const auto target = folder / layout.paths[index];
            auto fresh = target;
            fresh += L".mu-new";
            write_file(fresh, contents(data, file));
            if (!matches(fresh, file)) {
                DeleteFileW(fresh.c_str());
                throw Error{Problem::mismatch, 0, target};
            }
            const DWORD attributes = GetFileAttributesW(target.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY)) {
                SetFileAttributesW(target.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
            }
            if (!MoveFileExW(fresh.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                const DWORD code = GetLastError();
                DeleteFileW(fresh.c_str());
                fail(code, target, true);
            }
            ++changed;
        }
    }
    const auto after = compare(layout, folder, std::nullopt);
    if (!after.extras.empty() || !after.complete) {
        // Something reappeared or vanished while repairing (another program is writing here).
        throw Error{Problem::in_use, 0, folder};
    }
    write_stamp(layout, folder, after.times);
    return changed;
}

Prepared extract(const Layout& layout, const fs::path& root, const fs::path& folder) {
    make_folders(root);
    ULARGE_INTEGER available{};
    constexpr std::uint64_t margin = 32u << 20;
    if (GetDiskFreeSpaceExW(root.c_str(), &available, nullptr, nullptr) &&
        available.QuadPart < layout.payload.data_size + margin) {
        throw Error{Problem::disk_full, ERROR_DISK_FULL, root, layout.payload.data_size + margin, available.QuadPart};
    }

    Prepared result{Prepared::Action::extracted, folder};
    auto start = Clock::now();
    const Buffer data = unpack(layout.payload);
    result.decompress_ms = elapsed_ms(start);

    start = Clock::now();
    const fs::path temporary = root / (L".tmp-" + folder.filename().wstring() + process_suffix());
    delete_tree(temporary);
    struct Cleanup {
        const fs::path& path;
        bool armed = true;
        ~Cleanup() {
            if (armed) delete_tree(path);
        }
    } cleanup{temporary};
    if (!CreateDirectoryW(temporary.c_str(), nullptr)) fail(GetLastError(), temporary);
    create_folders(temporary, layout);
    parallel_for(layout.paths.size(), [&](std::size_t i) {
        const auto path = temporary / layout.paths[i];
        write_file(path, contents(data, layout.payload.files[i]));
        if (!matches(path, layout.payload.files[i])) throw Error{Problem::mismatch, 0, path};
    });
    const auto written = compare(layout, temporary, std::nullopt);
    if (!written.extras.empty() || !written.complete) {
        throw Error{Problem::mismatch, 0, temporary};
    }
    write_stamp(layout, temporary, written.times);
    if (!MoveFileExW(temporary.c_str(), folder.c_str(), 0)) {
        const DWORD code = GetLastError();
        if (GetFileAttributesW(folder.c_str()) == INVALID_FILE_ATTRIBUTES) fail(code, folder);
        // Another launcher (without our lock, e.g. in another session) got there first: check its copy.
        result.action = Prepared::Action::reused;
        const auto theirs = compare(layout, folder, read_stamp(layout, folder));
        if (!theirs.clean()) {
            result.action = Prepared::Action::repaired;
            result.repaired_files = repair(layout, folder, theirs, result.decompress_ms);
        }
    } else {
        cleanup.armed = false;
    }
    result.write_ms = elapsed_ms(start);
    return result;
}

}  // namespace

std::filesystem::path default_root() {
    std::wstring value(32768, L'\0');
    const DWORD length = GetEnvironmentVariableW(L"MUSICUNLOCK_RUNTIME_DIR", value.data(), static_cast<DWORD>(value.size()));
    if (length > 0 && length < value.size()) {
        value.resize(length);
        return fs::absolute(fs::path(value));
    }
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local))) {
        CoTaskMemFree(local);
        throw Error{Problem::not_writable, ERROR_PATH_NOT_FOUND, L"%LOCALAPPDATA%"};
    }
    fs::path root = fs::path(local) / L"MusicUnlock" / L"runtime";
    CoTaskMemFree(local);
    return root;
}

RootLock::RootLock(const std::filesystem::path& root, DWORD timeout_ms) {
    const auto key = fold(root.lexically_normal().wstring());
    const auto digest = sha256({reinterpret_cast<const std::uint8_t*>(key.data()), key.size() * sizeof(wchar_t)});
    const auto name = L"Local\\MusicUnlock.Runtime." + widen(hex(std::span(digest).first(8)));
    mutex_ = CreateMutexW(nullptr, FALSE, name.c_str());
    if (!mutex_) throw Error{Problem::io, GetLastError(), root};
    // WAIT_ABANDONED: the previous holder crashed; its temporary folder is cleaned up as a leftover.
    const DWORD wait = WaitForSingleObject(mutex_, timeout_ms);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
        CloseHandle(mutex_);
        mutex_ = nullptr;
        throw Error{Problem::busy, wait == WAIT_TIMEOUT ? 0 : GetLastError(), root};
    }
}

RootLock::~RootLock() {
    if (mutex_) {
        ReleaseMutex(mutex_);
        CloseHandle(mutex_);
    }
}

Prepared prepare(const Payload& payload, const std::filesystem::path& root) {
    if (sha256(payload.manifest) != payload.id ||
        std::none_of(payload.files.begin(), payload.files.end(),
                     [](const FileEntry& file) { return fold_case(file.path) == fold_case(app_executable); })) {
        throw Error{Problem::corrupt};
    }
    const Layout layout(payload);
    const fs::path folder = root / widen(payload.version());
    const DWORD attributes = GetFileAttributesW(folder.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            const auto comparison = compare(layout, folder, read_stamp(layout, folder));
            if (comparison.clean()) return {Prepared::Action::reused, folder};
            Prepared result{Prepared::Action::repaired, folder};
            const auto start = Clock::now();
            result.repaired_files = repair(layout, folder, comparison, result.decompress_ms);
            result.write_ms = elapsed_ms(start) - result.decompress_ms;
            return result;
        }
        // A file or link where the folder should be.
        if (!delete_tree(folder)) fail(GetLastError(), folder);
    }
    return extract(layout, root, folder);
}

Cleanup remove_old_versions(const std::filesystem::path& root, std::string_view keep) {
    Cleanup result;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileExW((root / L"*").c_str(), FindExInfoBasic, &data, FindExSearchLimitToDirectories, nullptr, 0);
    if (find == INVALID_HANDLE_VALUE) return result;
    std::vector<std::wstring> names;
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) names.emplace_back(data.cFileName);
    } while (FindNextFileW(find, &data));
    FindClose(find);
    const std::wstring current = widen(keep);
    for (const auto& name : names) {
        const fs::path path = root / name;
        if (name.starts_with(L".tmp-") || name.starts_with(L".trash-")) {
            delete_tree(path);  // left by an interrupted run; the lock guarantees nobody is writing it
            continue;
        }
        // Only folders this launcher creates; MUSICUNLOCK_RUNTIME_DIR may point anywhere.
        std::string narrow;
        for (const wchar_t c : name) narrow.push_back(c < 128 ? static_cast<char>(c) : '?');
        if (name == current || !is_version_name(narrow)) continue;
        // A running copy keeps its executable mapped, so it cannot be opened for writing; renaming the folder
        // also fails while any file in it is open. Either way the folder stays.
        {
            Handle exe(CreateFileW((path / widen(app_executable)).c_str(), GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0,
                                   nullptr));
            if (!exe.valid() && GetLastError() != ERROR_FILE_NOT_FOUND) {
                ++result.in_use;
                continue;
            }
        }
        const fs::path trash = root / (L".trash-" + name + process_suffix());
        if (!MoveFileExW(path.c_str(), trash.c_str(), 0)) {
            ++result.in_use;
            continue;
        }
        delete_tree(trash);
        ++result.removed;
    }
    return result;
}

std::wstring message(const Error& error) {
    auto system_text = [&] {
        if (!error.code) return std::wstring();
        wchar_t* buffer = nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, error.code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
        std::wstring text = buffer ? buffer : L"";
        LocalFree(buffer);
        while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' ')) text.pop_back();
        return L"（错误 " + std::to_wstring(error.code) + (text.empty() ? L"" : L"：" + text) + L"）";
    };
    auto megabytes = [](std::uint64_t bytes) { return std::to_wstring((bytes + (1u << 20) - 1) >> 20); };
    const std::wstring path = error.path.wstring();
    switch (error.problem) {
    case Problem::corrupt:
        return L"MusicUnlock.exe 不完整或已损坏，无法解压。\n\n请重新下载 MusicUnlock.exe 后再打开。";
    case Problem::mismatch:
        return L"解压出的文件校验失败：\n" + path +
               L"\n\n可能是安全软件拦截或修改了这个文件，也可能是 MusicUnlock.exe 已损坏。"
               L"请查看安全软件的隔离记录，或重新下载后再试。";
    case Problem::disk_full:
        if (error.needed) {
            return L"磁盘空间不足。\n\n首次打开需要在 " + error.path.root_name().wstring() + L" 解压约 " +
                   megabytes(error.needed) + L" MB 文件，当前只剩 " + megabytes(error.available) +
                   L" MB 可用。请清理磁盘空间后再打开音匣。";
        }
        return L"磁盘空间不足，无法写入：\n" + path + L"\n\n请清理磁盘空间后再打开音匣。";
    case Problem::not_writable:
        return L"无法写入音匣的运行文件夹：\n" + path +
               L"\n\n请确认当前用户对这个文件夹有写入权限，并检查安全软件是否阻止了音匣。" + system_text();
    case Problem::in_use:
        return L"音匣的部分运行文件正在使用中，无法修复：\n" + path + L"\n\n请关闭所有音匣窗口后再打开。" + system_text();
    case Problem::busy:
        return L"另一个音匣正在准备运行文件，等待超时。\n\n请稍后再打开。";
    case Problem::launch:
        return L"无法启动音匣：\n" + path + L"\n\n" +
               (error.code == ERROR_VIRUS_INFECTED || error.code == ERROR_VIRUS_DELETED || error.code == ERROR_ACCESS_DENIED
                    ? L"安全软件可能拦截了音匣，请查看安全软件的隔离记录。"
                    : L"请重新打开 MusicUnlock.exe。") +
               system_text();
    case Problem::io:
        break;
    }
    return L"准备音匣的运行文件时出错。" + (path.empty() ? L"" : L"\n" + path) + L"\n\n请重新打开 MusicUnlock.exe。" +
           system_text();
}

}  // namespace launcher
