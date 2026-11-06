#pragma once
// Internal helpers shared by the decoders. Not part of the public core API.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "musicunlock/audio.h"

namespace mu::detail {

[[noreturn]] inline void fail_invalid(const std::string& message) {
    throw DecodeError(DecodeError::Kind::invalid, message);
}

[[noreturn]] inline void fail_unsupported(const std::string& message) {
    throw DecodeError(DecodeError::Kind::unsupported, message);
}

inline bool starts_with(ByteSpan bytes, std::string_view prefix) {
    return bytes.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), bytes.begin(),
                      [](char a, std::uint8_t b) { return static_cast<std::uint8_t>(a) == b; });
}

template <std::size_t N>
bool starts_with(ByteSpan bytes, const std::array<std::uint8_t, N>& prefix) {
    return bytes.size() >= N && std::equal(prefix.begin(), prefix.end(), bytes.begin());
}

inline std::uint32_t u32le(ByteSpan bytes) {
    return std::uint32_t{bytes[0]} | std::uint32_t{bytes[1]} << 8 | std::uint32_t{bytes[2]} << 16 |
           std::uint32_t{bytes[3]} << 24;
}

inline std::uint32_t u32be(ByteSpan bytes) {
    return std::uint32_t{bytes[0]} << 24 | std::uint32_t{bytes[1]} << 16 | std::uint32_t{bytes[2]} << 8 |
           std::uint32_t{bytes[3]};
}

inline void append(Bytes& out, ByteSpan bytes) { out.insert(out.end(), bytes.begin(), bytes.end()); }
inline void append(Bytes& out, std::string_view text) { out.insert(out.end(), text.begin(), text.end()); }

inline void append_be32(Bytes& out, std::size_t value) {
    for (int shift : {24, 16, 8, 0}) out.push_back(static_cast<std::uint8_t>(value >> shift));
}

inline void append_le32(Bytes& out, std::size_t value) {
    for (int shift : {0, 8, 16, 24}) out.push_back(static_cast<std::uint8_t>(value >> shift));
}

struct ByteReader {
    ByteSpan bytes;
    std::size_t position = 0;

    ByteSpan take(std::size_t count) {
        if (position > bytes.size() || count > bytes.size() - position) {
            fail_invalid("文件被截断，或加密容器头部无效");
        }
        auto result = bytes.subspan(position, count);
        position += count;
        return result;
    }

    std::size_t little32() { return u32le(take(4)); }
};

/// Strict RFC 4648 decoding (padding required, no whitespace), like Foundation's default.
std::optional<Bytes> base64_decode(std::string_view text);
std::string base64_encode(ByteSpan bytes);

enum class AesMode { encrypt, decrypt };
/// AES-128-ECB with PKCS#7 padding through the platform crypto library.
std::optional<Bytes> aes128_ecb(AesMode mode, ByteSpan input, ByteSpan key);

struct NcmResult {
    Bytes audio;
    AudioMetadata metadata;
};
NcmResult decode_ncm(ByteSpan data);
extern const std::array<std::uint8_t, 16> ncm_core_key;
extern const std::array<std::uint8_t, 16> ncm_meta_key;

Bytes decode_qmc(ByteSpan input);
Bytes derive_qmc_key(ByteSpan raw);
Bytes tea_decrypt(ByteSpan bytes, ByteSpan key);
void qmc_apply_static(Bytes& bytes, std::size_t offset = 0);
void qmc_apply_map(Bytes& bytes, ByteSpan key, std::size_t offset = 0);
void qmc_apply_rc4(Bytes& bytes, ByteSpan key, std::size_t offset = 0);

Bytes decode_legacy(ByteSpan input, std::string_view ext, ByteSpan kgm_mask);
extern const std::array<std::uint8_t, 272> kgm_predefined;

Bytes restore_tags(Bytes audio, std::string_view ext, const AudioMetadata& metadata);

/// Windows-safe single path component; see sanitize_file_stem.
std::string sanitize_component(std::string_view name, std::size_t max_units, std::string_view fallback,
                               bool trim_leading_dots);

/// Legacy Chinese text (GBK / GB18030) to UTF-8 through the platform codec; nullopt if invalid.
std::optional<std::string> gb18030_to_utf8(ByteSpan bytes);

/// Reads a whole file; paths may be long or non-ASCII on Windows.
Bytes read_file(const std::filesystem::path& path);
/// Creates `path` exclusively and writes `data`. Returns false if the name is taken.
bool write_new_file(const std::filesystem::path& path, ByteSpan data);

}  // namespace mu::detail
