#include "detail.h"
#include "musicunlock/text.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include <fstream>
#endif

namespace mu::detail {

#ifdef _WIN32

namespace {

std::string system_message(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                            FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::u16string text(reinterpret_cast<const char16_t*>(buffer), length);
    LocalFree(buffer);
    while (!text.empty() && (text.back() == u'\r' || text.back() == u'\n' || text.back() == u'.' || text.back() == u'。')) {
        text.pop_back();
    }
    return text.empty() ? "错误 " + std::to_string(code) : utf16_to_utf8(text);
}

/// Extended-length form so long output folders and 180-character names stay writable.
std::wstring native_path(const std::filesystem::path& path) {
    std::wstring full = std::filesystem::absolute(path).lexically_normal().make_preferred().wstring();
    if (full.rfind(LR"(\\?\)", 0) == 0) return full;
    if (full.rfind(LR"(\\)", 0) == 0) return LR"(\\?\UNC\)" + full.substr(2);
    return LR"(\\?\)" + full;
}

struct Handle {
    HANDLE value;
    ~Handle() {
        if (value != INVALID_HANDLE_VALUE) CloseHandle(value);
    }
};

}  // namespace

Bytes read_file(const std::filesystem::path& path) {
    Handle file{CreateFileW(native_path(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
    LARGE_INTEGER size{};
    if (file.value == INVALID_HANDLE_VALUE || !GetFileSizeEx(file.value, &size)) {
        fail_invalid("无法读取文件（" + system_message(GetLastError()) + "）");
    }
    if (static_cast<unsigned long long>(size.QuadPart) > (std::size_t{1} << 32)) fail_invalid("文件过大（超过 4 GB）");
    Bytes data(static_cast<std::size_t>(size.QuadPart));
    std::size_t done = 0;
    while (done < data.size()) {
        DWORD read = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(data.size() - done, 1 << 24));
        if (!ReadFile(file.value, data.data() + done, chunk, &read, nullptr)) {
            fail_invalid("无法读取文件（" + system_message(GetLastError()) + "）");
        }
        if (read == 0) break;
        done += read;
    }
    data.resize(done);
    return data;
}

bool write_new_file(const std::filesystem::path& path, ByteSpan data) {
    const std::wstring target = native_path(path);
    Handle file{CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (file.value == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        // An existing folder of the same name reports access denied rather than "exists".
        if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS ||
            (error == ERROR_ACCESS_DENIED && GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES)) {
            return false;
        }
        fail_invalid("无法创建输出文件（" + system_message(error) + "）");
    }
    std::size_t done = 0;
    while (done < data.size()) {
        DWORD written = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(data.size() - done, 1 << 24));
        if (!WriteFile(file.value, data.data() + done, chunk, &written, nullptr) || written == 0) {
            const DWORD error = GetLastError();
            CloseHandle(file.value);
            file.value = INVALID_HANDLE_VALUE;
            DeleteFileW(target.c_str());
            fail_invalid("写入输出文件失败（" + system_message(error) + "）");
        }
        done += written;
    }
    return true;
}

#else

Bytes read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) fail_invalid("无法读取文件（" + std::string(std::strerror(errno)) + "）");
    Bytes data(static_cast<std::size_t>(file.tellg()));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()))) {
        fail_invalid("无法读取文件");
    }
    return data;
}

bool write_new_file(const std::filesystem::path& path, ByteSpan data) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd < 0) {
        if (errno == EEXIST) return false;
        fail_invalid("无法创建输出文件（" + std::string(std::strerror(errno)) + "）");
    }
    std::size_t done = 0;
    while (done < data.size()) {
        const ssize_t written = ::write(fd, data.data() + done, data.size() - done);
        if (written <= 0) {
            if (written < 0 && errno == EINTR) continue;
            const std::string reason = written < 0 ? std::strerror(errno) : "磁盘已满";
            ::close(fd);
            ::unlink(path.c_str());
            fail_invalid("写入输出文件失败（" + reason + "）");
        }
        done += static_cast<std::size_t>(written);
    }
    if (::close(fd) != 0) {
        const std::string reason = std::strerror(errno);
        ::unlink(path.c_str());
        fail_invalid("写入输出文件失败（" + reason + "）");
    }
    return true;
}

#endif

}  // namespace mu::detail
