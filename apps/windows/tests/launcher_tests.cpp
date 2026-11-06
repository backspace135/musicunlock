// Single-exe launcher tests: the payload format and command-line handling everywhere, and on Windows the
// compression, extraction, verification, repair, cleanup and locking. Scratch folders stay under the build folder;
// nothing is written to %LOCALAPPDATA%. The launcher executable itself is covered by scripts/smoke_launcher.py.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>

#include "payload.h"
#include "test_support.h"

#ifdef _WIN32
#include <atomic>
#include <thread>

#include "codec.h"
#include "install.h"
#endif

namespace fs = std::filesystem;
using namespace launcher;

namespace {

Bytes text(std::string_view value) { return Bytes(value.begin(), value.end()); }

/// A hash stand-in for the portable tests (the real one is CNG): any fixed mapping will do.
Sha256 fake_hash(std::span<const std::uint8_t> data) {
    Sha256 out{};
    for (std::size_t i = 0; i < data.size(); ++i) out[i % out.size()] = static_cast<std::uint8_t>(out[i % out.size()] * 31 + data[i]);
    return out;
}

/// Stored payload with the given files, laid out back to back in one chunk.
Bytes stored_payload(const std::vector<std::pair<std::string, std::string>>& files) {
    std::vector<FileEntry> entries;
    Bytes data;
    for (const auto& [path, contents] : files) {
        FileEntry entry;
        entry.path = path;
        entry.offset = data.size();
        entry.size = contents.size();
        entry.sha256 = fake_hash(text(contents));
        entries.push_back(entry);
        data.insert(data.end(), contents.begin(), contents.end());
    }
    std::vector<EncodedChunk> chunks;
    if (!data.empty()) chunks.push_back({static_cast<std::uint32_t>(data.size()), data});
    return encode(Algorithm::stored, entries, chunks, fake_hash);
}

TEST(payload_round_trips) {
    const Bytes bytes = stored_payload({{"MusicUnlock.exe", "exe"}, {"zh-CN/资源.mui", "mui"}, {"empty", ""}});
    const Payload payload = parse(bytes);
    CHECK(payload.algorithm == Algorithm::stored);
    CHECK_EQ(payload.files.size(), 3u, "file count");
    CHECK_EQ(payload.data_size, 6u, "data size");
    CHECK_EQ(payload.files[1].path, std::string("zh-CN/资源.mui"), "UTF-8 path");
    CHECK_EQ(payload.files[1].offset, 3u, "offset");
    CHECK(payload.id == fake_hash(payload.manifest));
    CHECK_EQ(payload.version(), hex(std::span(payload.id).first(8)), "version name");
    CHECK(is_version_name(payload.version()));
    CHECK_EQ(payload.compressed.size(), 6u, "compressed size");
}

TEST(payload_rejects_damage) {
    const Bytes good = stored_payload({{"MusicUnlock.exe", "exe"}, {"a/b.dll", "dll"}});
    for (std::size_t size = 0; size < good.size(); ++size) {
        CHECK_THROWS_AS(parse(std::span(good).first(size)), FormatError, "truncated to " + std::to_string(size));
    }
    Bytes trailing = good;
    trailing.push_back(0);
    CHECK_THROWS_AS(parse(trailing), FormatError, "trailing byte");
    Bytes magic = good;
    magic[0] = 'X';
    CHECK_THROWS_AS(parse(magic), FormatError, "magic");
    Bytes algorithm = good;
    algorithm[8] = 9;
    CHECK_THROWS_AS(parse(algorithm), FormatError, "algorithm");
    Bytes data_size = good;
    data_size[24] += 1;  // chunks no longer add up
    CHECK_THROWS_AS(parse(data_size), FormatError, "data size");
}

TEST(payload_rejects_unsafe_and_clashing_paths) {
    for (const char* path : {"../evil.dll", "a/../../b", "C:/x", "/abs", "a//b", "a\\b", "x:stream", "con", "Aux.txt",
                             "lpt1", "dot.", "space ", "", "a/"}) {
        CHECK_THROWS_AS(parse(stored_payload({{path, "x"}})), FormatError, path);
        CHECK(!valid_path(path));
    }
    for (const char* path : {"MusicUnlock.exe", "zh-CN/a.mui", "Microsoft.UI.Xaml/Assets/x.png", "中文 名称.txt", "console.dll",
                             "com10", ".hidden"}) {
        CHECK(valid_path(path));
    }
    CHECK_THROWS_AS(parse(stored_payload({{"A.dll", "1"}, {"a.DLL", "2"}})), FormatError, "case-insensitive duplicate");
    CHECK_THROWS_AS(parse(stored_payload({{"lib", "1"}, {"LIB/x.dll", "2"}})), FormatError, "file used as folder");
}

TEST(stamp_round_trips_and_rejects_other_payloads) {
    Sha256 id{};
    id[0] = 1;
    const Bytes stamp = encode_stamp(id, {10, 20, 30});
    const auto times = decode_stamp(stamp, id, 3);
    CHECK(times && *times == std::vector<std::uint64_t>({10, 20, 30}));
    Sha256 other = id;
    other[31] = 9;
    CHECK(!decode_stamp(stamp, other, 3));
    CHECK(!decode_stamp(stamp, id, 2));
    CHECK(!decode_stamp(std::span(stamp).first(stamp.size() - 1), id, 3));
    CHECK(!decode_stamp({}, id, 0));
}

TEST(arguments_pass_through_unchanged) {
    CHECK(arguments_after_program(L"\"C:\\Program Files\\MusicUnlock.exe\" \"D:\\a b.ncm\" -x") == L"\"D:\\a b.ncm\" -x");
    CHECK(arguments_after_program(L"MusicUnlock.exe   D:\\a.ncm") == L"D:\\a.ncm");
    CHECK(arguments_after_program(L"MusicUnlock.exe\tD:\\a.ncm \"\" ") == L"D:\\a.ncm \"\" ");
    CHECK(arguments_after_program(L"\"C:\\x y\\MusicUnlock.exe\"") == L"");
    CHECK(arguments_after_program(L"MusicUnlock.exe") == L"");
    CHECK(arguments_after_program(L"") == L"");
    CHECK(arguments_after_program(L"\"unterminated") == L"");
}

#ifdef _WIN32

const fs::path scratch_root = fs::path(MU_TEST_SCRATCH) / "launcher";

fs::path fresh(const std::string& name) {
    const auto folder = scratch_root / name;
    fs::remove_all(folder);
    fs::create_directories(folder);
    return folder;
}

Bytes random_bytes(std::size_t size, unsigned seed) {
    std::mt19937 generator(seed);
    Bytes out(size);
    // Half random, half repetitive, so compression has something to do.
    for (std::size_t i = 0; i < size; ++i) out[i] = i % 2 ? static_cast<std::uint8_t>(generator()) : static_cast<std::uint8_t>(i / 64);
    return out;
}

std::vector<SourceFile> sample_app(unsigned seed = 1) {
    return {{"MusicUnlock.exe", random_bytes(70'000, seed)},
            {"Microsoft.UI.Xaml.dll", random_bytes(3'000'000, seed + 1)},
            {"zh-CN/Microsoft.UI.Xaml.dll.mui", random_bytes(9'000, seed + 2)},
            {"Microsoft.UI.Xaml/Assets/中文 名称.png", random_bytes(1'234, seed + 3)},
            {"empty.txt", {}}};
}

Bytes read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void write(const fs::path& path, const Bytes& data) {
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

bool same_as_source(const fs::path& folder, const std::vector<SourceFile>& files) {
    for (const auto& file : files) {
        if (read(folder / fs::path(std::u8string(file.path.begin(), file.path.end()))) != file.data) return false;
    }
    return true;
}

TEST(compression_round_trips_in_chunks) {
    const Bytes data = random_bytes(5'000'000, 7);
    for (const auto algorithm : {Algorithm::xpress_huff, Algorithm::lzms, Algorithm::stored}) {
        const Bytes bytes = pack({{"MusicUnlock.exe", data}}, algorithm, 1u << 20);
        const Payload payload = parse(bytes);
        CHECK_EQ(payload.chunks.size(), 5u, "chunk count");
        if (algorithm != Algorithm::stored) CHECK(payload.compressed.size() < data.size());
        const Buffer out = decompress(payload);
        CHECK(std::equal(data.begin(), data.end(), out.view().begin(), out.view().end()));
        CHECK(payload.files[0].sha256 == sha256(data));
    }
    // Damaged compressed data is reported, not crashed on.
    Bytes bytes = pack({{"MusicUnlock.exe", data}}, Algorithm::xpress_huff, 1u << 20);
    std::fill(bytes.end() - 4096, bytes.end(), std::uint8_t{0xFF});
    CHECK_THROWS_AS(decompress(parse(bytes)), FormatError, "damaged chunk");
}

TEST(extracts_then_reuses_then_repairs) {
    const auto root = fresh("prepare") / "runtime";
    const auto files = sample_app();
    const Bytes bytes = pack(files, Algorithm::lzms, 1u << 20);
    const Payload payload = parse(bytes);
    const auto first = prepare(payload, root);
    CHECK(first.action == Prepared::Action::extracted);
    CHECK_EQ(first.folder, root / payload.version(), "folder");
    CHECK(same_as_source(first.folder, files));
    CHECK(fs::exists(first.folder / stamp_name));
    CHECK_EQ(std::distance(fs::directory_iterator(root), fs::directory_iterator()), 1, "no temporary folder left");

    CHECK(prepare(payload, root).action == Prepared::Action::reused);

    // Same size, new contents; a deleted file; an extra DLL next to the app; a folder replaced by a file.
    const auto dll = first.folder / "Microsoft.UI.Xaml.dll";
    Bytes changed = read(dll);
    changed[1000] ^= 1;
    write(dll, changed);
    fs::remove(first.folder / "empty.txt");
    write(first.folder / "version.dll", text("planted"));
    fs::remove_all(first.folder / "zh-CN");
    write(first.folder / "zh-CN", text("not a folder"));
    const auto repaired = prepare(payload, root);
    CHECK(repaired.action == Prepared::Action::repaired);
    CHECK_EQ(repaired.repaired_files, 5u, "two extras removed, three files rewritten");
    CHECK(same_as_source(first.folder, files));
    CHECK(!fs::exists(first.folder / "version.dll"));
    CHECK(prepare(payload, root).action == Prepared::Action::reused);

    // A lost stamp means every file is hashed once; nothing needs rewriting.
    fs::remove(first.folder / stamp_name);
    const auto rehashed = prepare(payload, root);
    CHECK(rehashed.action == Prepared::Action::repaired);
    CHECK_EQ(rehashed.repaired_files, 0u, "nothing rewritten");
    CHECK(prepare(payload, root).action == Prepared::Action::reused);

    // A read-only, tampered file is still repaired.
    const auto exe = first.folder / "MusicUnlock.exe";
    write(exe, text("tampered"));
    SetFileAttributesW(exe.c_str(), FILE_ATTRIBUTE_READONLY);
    CHECK(prepare(payload, root).action == Prepared::Action::repaired);
    CHECK(read(exe) == files[0].data);
}

TEST(rejects_payload_whose_manifest_does_not_match_its_id) {
    Bytes bytes = pack(sample_app(), Algorithm::xpress_huff, 1u << 20);
    Payload payload = parse(bytes);
    payload.id[0] ^= 1;
    bool corrupt = false;
    try {
        prepare(payload, fresh("corrupt"));
    } catch (const Error& error) {
        corrupt = error.problem == Problem::corrupt;
    }
    CHECK(corrupt);
    // A payload without MusicUnlock.exe is no app.
    corrupt = false;
    try {
        prepare(parse(pack({{"other.exe", text("x")}}, Algorithm::stored, 1u << 20)), fresh("no-exe"));
    } catch (const Error& error) {
        corrupt = error.problem == Problem::corrupt;
    }
    CHECK(corrupt);
}

TEST(reports_unwritable_root) {
    const auto base = fresh("unwritable");
    write(base / "file", text("x"));
    const Bytes bytes = pack(sample_app(), Algorithm::xpress_huff, 1u << 20);
    const Payload payload = parse(bytes);  // points into bytes
    Problem problem = Problem::io;
    try {
        prepare(payload, base / "file" / "runtime");
    } catch (const Error& error) {
        problem = error.problem;
        CHECK(message(error).find(L"无法写入") != std::wstring::npos);
    }
    CHECK(problem == Problem::not_writable);
}

TEST(removes_old_versions_unless_in_use) {
    const auto root = fresh("cleanup");
    const Bytes current_bytes = pack(sample_app(1), Algorithm::xpress_huff, 1u << 20);
    const Payload current = parse(current_bytes);
    const Bytes old_bytes = pack(sample_app(2), Algorithm::xpress_huff, 1u << 20);
    const Bytes busy_bytes = pack(sample_app(3), Algorithm::xpress_huff, 1u << 20);
    const Payload old = parse(old_bytes), busy = parse(busy_bytes);
    prepare(current, root);
    prepare(old, root);
    prepare(busy, root);
    fs::create_directories(root / ".tmp-0123456789abcdef-42" / "sub");
    fs::create_directories(root / "notes");  // not ours: left alone
    write(root / "notes" / "keep.txt", text("x"));
    {
        // An open file without delete sharing, as a running copy has.
        HANDLE open = CreateFileW((root / busy.version() / "zh-CN" / "Microsoft.UI.Xaml.dll.mui").c_str(), GENERIC_READ,
                                  FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        CHECK(open != INVALID_HANDLE_VALUE);
        const auto cleanup = remove_old_versions(root, current.version());
        CloseHandle(open);
        CHECK_EQ(cleanup.removed, 1u, "removed");
        CHECK_EQ(cleanup.in_use, 1u, "in use");
    }
    CHECK(fs::exists(root / current.version() / "MusicUnlock.exe"));
    CHECK(!fs::exists(root / old.version()));
    CHECK(same_as_source(root / busy.version(), sample_app(3)));
    CHECK(!fs::exists(root / ".tmp-0123456789abcdef-42"));
    CHECK(fs::exists(root / "notes" / "keep.txt"));
    CHECK_EQ(remove_old_versions(root, current.version()).removed, 1u, "removed once closed");
}

TEST(lock_waits_for_the_other_launcher) {
    const auto root = fresh("lock");
    std::atomic<bool> held{false}, release{false};
    std::thread holder([&] {
        RootLock lock(root, 1000);
        held = true;
        while (!release) Sleep(5);
    });
    while (!held) Sleep(5);
    Problem problem = Problem::io;
    try {
        RootLock lock(root, 50);
    } catch (const Error& error) {
        problem = error.problem;
    }
    CHECK(problem == Problem::busy);
    release = true;
    holder.join();
    RootLock lock(root, 1000);  // free again
    // Different roots do not share a lock.
    RootLock other(fresh("lock-other"), 0);
}

TEST(messages_say_what_to_do) {
    Error full{Problem::disk_full, ERROR_DISK_FULL, L"C:\\Users\\x\\AppData\\Local\\MusicUnlock\\runtime", 150u << 20, 20u << 20};
    const auto text_full = message(full);
    CHECK(text_full.find(L"C:") != std::wstring::npos && text_full.find(L"150 MB") != std::wstring::npos &&
          text_full.find(L"20 MB") != std::wstring::npos);
    CHECK(message(Error{Problem::in_use, ERROR_SHARING_VIOLATION, L"D:\\x"}).find(L"关闭所有音匣窗口") != std::wstring::npos);
    CHECK(message(Error{Problem::corrupt}).find(L"重新下载") != std::wstring::npos);
    CHECK(message(Error{Problem::launch, ERROR_VIRUS_INFECTED, L"D:\\x"}).find(L"安全软件") != std::wstring::npos);
    CHECK(message(Error{Problem::not_writable, ERROR_ACCESS_DENIED, L"D:\\x"}).find(L"写入权限") != std::wstring::npos);
}

#endif

}  // namespace

int main(int argc, char** argv) {
    // An optional argument runs only the tests whose name contains it.
    for (const auto& test : registry()) {
        if (argc > 1 && std::string_view(test.name).find(argv[1]) == std::string_view::npos) continue;
        const int before = failures;
        try {
            test.body();
        } catch (const std::exception& error) {
            report(__FILE__, __LINE__, std::string(test.name) + " threw: " + error.what());
        } catch (...) {
            report(__FILE__, __LINE__, std::string(test.name) + " threw");
        }
        std::cout << (failures == before ? "PASS " : "FAIL ") << test.name << std::endl;
    }
    std::cout << registry().size() << " tests, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
