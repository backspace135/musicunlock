// Single-exe launcher tests: the payload format and command-line handling (portable).
#include <algorithm>

#include "payload.h"
#include "test_support.h"

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
