// Core tests mirroring apps/macos/Tests/MusicUnlockCoreTests/DecoderTests.swift, plus Windows naming rules.
// Uses the shared vectors in tests/fixtures/qmc; scratch files stay under the build directory.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "detail.h"
#include "musicunlock/audio.h"
#include "musicunlock/conversion.h"
#include "musicunlock/text.h"
#include "test_support.h"

namespace fs = std::filesystem;
using mu::Bytes;

namespace {

const fs::path fixtures = mu::utf8_to_path(MU_FIXTURES_DIR);
const fs::path scratch_root = mu::utf8_to_path(MU_TEST_SCRATCH);

Bytes load(const std::string& name) { return mu::detail::read_file(fixtures / name); }

Bytes concat(Bytes a, const Bytes& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

Bytes text_bytes(std::string_view text) { return Bytes(text.begin(), text.end()); }

bool contains(const Bytes& haystack, std::string_view needle) {
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                       [](std::uint8_t a, char b) { return a == static_cast<std::uint8_t>(b); }) != haystack.end();
}

void le32(Bytes& out, std::size_t value) { mu::detail::append_le32(out, value); }

/// Valid PCM WAV containing 80 silent samples, with no third-party audio.
Bytes wave_fixture() {
    Bytes data = text_bytes("RIFF");
    le32(data, 196);
    mu::detail::append(data, "WAVEfmt ");
    le32(data, 16);
    data.insert(data.end(), {1, 0, 1, 0});
    le32(data, 8000);
    le32(data, 16000);
    data.insert(data.end(), {2, 0, 16, 0});
    mu::detail::append(data, "data");
    le32(data, 160);
    data.insert(data.end(), 160, 0);
    return data;
}

Bytes aes_encrypt(const Bytes& input, mu::ByteSpan key) {
    auto out = mu::detail::aes128_ecb(mu::detail::AesMode::encrypt, input, key);
    CHECK(out.has_value());
    return out.value_or(Bytes{});
}

struct NcmFixture {
    Bytes container;
    Bytes audio;
};

NcmFixture ncm_fixture() {
    const Bytes key = text_bytes("fixture-key");
    Bytes encrypted_key = aes_encrypt(concat(text_bytes("neteasecloudmusic"), key), mu::detail::ncm_core_key);
    for (auto& b : encrypted_key) b ^= 0x64;
    const Bytes json = text_bytes(
        "music:{\"musicName\":\"原生测试\",\"artist\":[[\"测试歌手\",1]],\"album\":\"测试专辑\",\"format\":\"wav\"}");
    const std::string meta = mu::detail::base64_encode(aes_encrypt(json, mu::detail::ncm_meta_key));
    Bytes encrypted_meta = text_bytes("163 key(Don't modify):" + meta);
    for (auto& b : encrypted_meta) b ^= 0x63;
    std::array<std::uint8_t, 256> box{};
    for (std::size_t i = 0; i < 256; ++i) box[i] = static_cast<std::uint8_t>(i);
    for (std::size_t i = 0, j = 0; i < 256; ++i) {
        j = (box[i] + j + key[i % key.size()]) & 255;
        std::swap(box[i], box[j]);
    }
    const Bytes plain = wave_fixture();
    Bytes audio = plain;
    for (std::size_t offset = 0; offset < audio.size(); ++offset) {
        const std::size_t i = (offset + 1) & 255;
        const std::size_t a = box[i];
        const std::size_t b = box[(i + a) & 255];
        audio[offset] ^= box[(a + b) & 255];
    }
    Bytes container = text_bytes("CTENFDAM");
    container.insert(container.end(), {0, 0});
    le32(container, encrypted_key.size());
    mu::detail::append(container, encrypted_key);
    le32(container, encrypted_meta.size());
    mu::detail::append(container, encrypted_meta);
    container.insert(container.end(), 5, 0);
    le32(container, 0);
    le32(container, 0);
    mu::detail::append(container, audio);
    return {container, plain};
}

/// std::filesystem on MSVC cannot remove paths beyond MAX_PATH without the extended-length prefix.
fs::path removable(const fs::path& path) {
#ifdef _WIN32
    return fs::path(LR"(\\?\)" + fs::absolute(path).lexically_normal().make_preferred().wstring());
#else
    return path;
#endif
}

fs::path fresh_scratch(const std::string& name) {
    const fs::path folder = scratch_root / name;
    fs::remove_all(removable(folder));
    fs::create_directories(folder);
    return folder;
}

void write_bytes(const fs::path& path, const Bytes& data) {
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(data.data()),
                                                static_cast<std::streamsize>(data.size()));
}

TEST(qmc_upstream_reference_files) {
    for (std::string name : {"mflac0_rc4", "mflac_rc4", "mflac_map", "mgg_map", "qmc0_static"}) {
        const Bytes input = concat(load(name + "_raw.bin"), load(name + "_suffix.bin"));
        CHECK_EQ(mu::detail::decode_qmc(input), load(name + "_target.bin"), name);
    }
}

TEST(qmc_upstream_derived_keys) {
    for (std::string name : {"mflac0_rc4", "mflac_rc4", "mflac_map", "mgg_map"}) {
        CHECK_EQ(mu::detail::derive_qmc_key(load(name + "_key_raw.bin")), load(name + "_key.bin"), name);
    }
}

TEST(kgm_constants_match_reference_table) {
    const Bytes values = load("kgm-predefined.bin");
    CHECK_EQ(values.size(), std::size_t{272}, "table size");
    CHECK(std::equal(values.begin(), values.end(), mu::detail::kgm_predefined.begin(), mu::detail::kgm_predefined.end()));
}

TEST(static_cipher_boundary) {
    Bytes bytes(16, 0);
    mu::detail::qmc_apply_static(bytes, 0x7ff8);
    CHECK_EQ(bytes, (Bytes{0xd8, 0x52, 0xf7, 0x67, 0x90, 0xca, 0xd6, 0x4a, 0x4a, 0xd6, 0xca, 0x90, 0x67, 0xf7, 0x52, 0xd8}),
             "static box wrap at 0x7fff");
}

TEST(ncm_container_and_metadata) {
    const auto fixture = ncm_fixture();
    const auto decoded = mu::detail::decode_ncm(fixture.container);
    CHECK_EQ(decoded.audio, fixture.audio, "ncm audio");
    CHECK_EQ(decoded.metadata.title, std::string("原生测试"), "title");
    CHECK_EQ(decoded.metadata.artist, std::string("测试歌手"), "artist");
    CHECK_EQ(decoded.metadata.album, std::string("测试专辑"), "album");
    const auto result = mu::decode_audio(fixture.container, "NCM");
    CHECK_EQ(result.file_extension, std::string("wav"), "sniffed extension");
    CHECK_EQ(result.data, fixture.audio, "untagged wav");
}

TEST(id3_and_flac_metadata) {
    mu::AudioMetadata metadata;
    metadata.title = "测试标题";
    metadata.artist = "测试歌手";
    metadata.cover = Bytes{0xff, 0xd8, 0xff, 0xd9};
    Bytes mp3 = {0xff, 0xfb, 0x90, 0x64};
    mp3.insert(mp3.end(), 64, 0);
    const Bytes tagged = mu::detail::restore_tags(mp3, "mp3", metadata);
    CHECK(mu::detail::starts_with(tagged, "ID3"));
    CHECK(std::equal(mp3.rbegin(), mp3.rend(), tagged.rbegin()));
    CHECK(contains(tagged, "APIC"));
    Bytes flac = text_bytes("fLaC");
    flac.insert(flac.end(), {0x80, 0, 0, 34});
    flac.insert(flac.end(), 34, 0);
    flac.insert(flac.end(), {0xff, 0xf8, 1, 2});
    const Bytes flac_tagged = mu::detail::restore_tags(flac, "flac", metadata);
    CHECK(contains(flac_tagged, "TITLE=测试标题"));
    CHECK(Bytes(flac_tagged.end() - 4, flac_tagged.end()) == (Bytes{0xff, 0xf8, 1, 2}));
}

TEST(export_does_not_overwrite_and_keeps_original) {
    const fs::path folder = fresh_scratch("export");
    const fs::path source = folder / mu::utf8_to_path("中文歌曲.uc");
    const Bytes audio = wave_fixture();
    Bytes encrypted = audio;
    for (auto& b : encrypted) b ^= 0xa3;
    write_bytes(source, encrypted);
    const auto first = mu::convert_file(source, folder, mu::NamingPolicy::original);
    const auto second = mu::convert_file(source, folder, mu::NamingPolicy::original);
    CHECK(first.path != second.path);
    CHECK_EQ(mu::path_to_utf8(second.path.filename()), std::string("中文歌曲 (1).wav"), "collision suffix");
    CHECK_EQ(mu::detail::read_file(first.path), audio, "first output");
    CHECK_EQ(mu::detail::read_file(source), encrypted, "source untouched");
    fs::remove_all(removable(folder));
}

TEST(long_output_paths_are_writable) {
    // Deeper than MAX_PATH (260) once the 180-unit name is added.
    fs::path folder = fresh_scratch("long");
    for (int i = 0; i < 3; ++i) folder /= std::string(40, static_cast<char>('a' + i));
    // Test setup goes through extended-length paths too: in a deep checkout (a git worktree) these
    // folders and the renamed source are already past MAX_PATH before the core is involved.
    fs::create_directories(removable(folder));
    Bytes encrypted = wave_fixture();
    for (auto& b : encrypted) b ^= 0xa3;
    const fs::path source = scratch_root / "long" / "short.uc";
    write_bytes(source, encrypted);
    const fs::path renamed = scratch_root / "long" / mu::utf8_to_path(std::string(150, 'y') + ".uc");
    fs::rename(removable(source), removable(renamed));
    const auto output = mu::convert_file(renamed, folder, mu::NamingPolicy::original);
    CHECK(mu::path_to_utf8(output.path).size() > 260);
    CHECK_EQ(mu::detail::read_file(output.path), wave_fixture(), "long path output");
    fs::remove_all(removable(scratch_root / "long"));
}

TEST(windows_safe_file_names) {
    CHECK_EQ(mu::sanitize_file_stem("A/B:C*D?E\"F<G>H|I\\J"), std::string("A_B_C_D_E_F_G_H_I_J"), "reserved chars");
    CHECK_EQ(mu::sanitize_file_stem("line\nbreak\ttab"), std::string("line_break_tab"), "control chars");
    CHECK_EQ(mu::sanitize_file_stem("  歌名...  "), std::string("歌名"), "trailing dots/spaces");
    CHECK_EQ(mu::sanitize_file_stem("　全角空格　"), std::string("全角空格"), "ideographic spaces");
    CHECK_EQ(mu::sanitize_file_stem("CON"), std::string("_CON"), "device name");
    CHECK_EQ(mu::sanitize_file_stem("com3.live"), std::string("_com3.live"), "device name with dot");
    CHECK_EQ(mu::sanitize_file_stem("Console"), std::string("Console"), "not a device");
    CHECK_EQ(mu::sanitize_file_stem(" . "), std::string("音频"), "empty fallback");
    const std::string emoji = "\xF0\x9F\x8E\xB5";  // U+1F3B5, two UTF-16 units
    const std::string long_name = std::string(179, 'a') + emoji;
    CHECK_EQ(mu::sanitize_file_stem(long_name), std::string(179, 'a'), "surrogate pair not split");
    CHECK_EQ(mu::utf8_to_utf16(mu::sanitize_file_stem(std::string(300, 'z'))).size(), std::size_t{180}, "length cap");
}

TEST(kuwo_and_xiami_round_trip) {
    const Bytes audio = wave_fixture();
    // KWM: 0x400-byte header, key value stored little-endian at 0x18.
    Bytes kwm = text_bytes("yeelion-kuwo-tme");
    kwm.resize(0x400, 0);
    const std::uint64_t value = 1234567890123ULL;
    for (int i = 0; i < 8; ++i) kwm[0x18 + i] = static_cast<std::uint8_t>(value >> (8 * i));
    const std::string key_text = std::to_string(value);
    const std::string_view predefined = "MoOtOiTvINGwd2E6n0E1i7L5t2IoOoNk";
    for (std::size_t i = 0; i < audio.size(); ++i) {
        kwm.push_back(audio[i] ^ static_cast<std::uint8_t>(predefined[i % 32] ^ key_text[(i % 32) % key_text.size()]));
    }
    CHECK_EQ(mu::decode_audio(kwm, "kwm").data, audio, "kwm");
    // XM: bytes from `start` are stored as (plain ^ 0xff) + key.
    Bytes xm = text_bytes("ifmt");
    xm.insert(xm.end(), {0, 0, 0, 0, 0xfe, 0xfe, 0xfe, 0xfe, 4, 0, 0, 0x5a});
    for (std::size_t i = 0; i < audio.size(); ++i) {
        xm.push_back(i < 4 ? audio[i] : static_cast<std::uint8_t>((audio[i] ^ 0xff) + 0x5a));
    }
    CHECK_EQ(mu::decode_audio(xm, "xm").data, audio, "xm");
}

TEST(malformed_files_throw_instead_of_crashing) {
    for (const auto& ext : mu::supported_extensions()) {
        for (std::size_t count : {0, 1, 4, 8, 16, 32, 1024}) {
            CHECK_THROWS(mu::decode_audio(Bytes(count, 0), ext), ext + " x" + std::to_string(count));
        }
    }
    CHECK_THROWS(mu::decode_audio(text_bytes("not musicSTag"), "mflac"), "STag");
    CHECK_THROWS(mu::decode_audio(Bytes{1}, "unknown"), "unknown extension");
}

}  // namespace

int main(int argc, char** argv) {
    // Optional arguments pick tests by name fragment, e.g. `musicunlock_core_tests live_netease`.
    std::size_t ran = 0;
    for (const auto& test : registry()) {
        if (argc > 1 && std::none_of(argv + 1, argv + argc, [&](const char* part) {
                return std::string_view(test.name).find(part) != std::string_view::npos;
            })) {
            continue;
        }
        ++ran;
        const int before = failures;
        try {
            test.body();
        } catch (const std::exception& error) {
            report(__FILE__, __LINE__, std::string(test.name) + " threw: " + error.what());
        }
        std::cout << (failures == before ? "PASS " : "FAIL ") << test.name << "\n";
    }
    std::cout << ran << " tests, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
