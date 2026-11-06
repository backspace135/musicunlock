// Ported from qmc.ts / qmc_key.ts / qmc_cipher.ts, Copyright (c) 2019-2021 MengYX (MIT).
// TEA block logic is also derived from Go Authors' golang.org/x/crypto/tea (BSD-3-Clause).
#include <cmath>

#include "detail.h"

namespace mu::detail {

namespace {

constexpr std::array<std::uint8_t, 256> static_box = {
    0x77,0x48,0x32,0x73,0xde,0xf2,0xc0,0xc8,0x95,0xec,0x30,0xb2,0x51,0xc3,0xe1,0xa0,
    0x9e,0xe6,0x9d,0xcf,0xfa,0x7f,0x14,0xd1,0xce,0xb8,0xdc,0xc3,0x4a,0x67,0x93,0xd6,
    0x28,0xc2,0x91,0x70,0xca,0x8d,0xa2,0xa4,0xf0,0x08,0x61,0x90,0x7e,0x6f,0xa2,0xe0,
    0xeb,0xae,0x3e,0xb6,0x67,0xc7,0x92,0xf4,0x91,0xb5,0xf6,0x6c,0x5e,0x84,0x40,0xf7,
    0xf3,0x1b,0x02,0x7f,0xd5,0xab,0x41,0x89,0x28,0xf4,0x25,0xcc,0x52,0x11,0xad,0x43,
    0x68,0xa6,0x41,0x8b,0x84,0xb5,0xff,0x2c,0x92,0x4a,0x26,0xd8,0x47,0x6a,0x7c,0x95,
    0x61,0xcc,0xe6,0xcb,0xbb,0x3f,0x47,0x58,0x89,0x75,0xc3,0x75,0xa1,0xd9,0xaf,0xcc,
    0x08,0x73,0x17,0xdc,0xaa,0x9a,0xa2,0x16,0x41,0xd8,0xa2,0x06,0xc6,0x8b,0xfc,0x66,
    0x34,0x9f,0xcf,0x18,0x23,0xa0,0x0a,0x74,0xe7,0x2b,0x27,0x70,0x92,0xe9,0xaf,0x37,
    0xe6,0x8c,0xa7,0xbc,0x62,0x65,0x9c,0xc2,0x08,0xc9,0x88,0xb3,0xf3,0x43,0xac,0x74,
    0x2c,0x0f,0xd4,0xaf,0xa1,0xc3,0x01,0x64,0x95,0x4e,0x48,0x9f,0xf4,0x35,0x78,0x95,
    0x7a,0x39,0xd6,0x6a,0xa0,0x6d,0x40,0xe8,0x4f,0xa8,0xef,0x11,0x1d,0xf3,0x1b,0x3f,
    0x3f,0x07,0xdd,0x6f,0x5b,0x19,0x30,0x19,0xfb,0xef,0x0e,0x37,0xf0,0x0e,0xcd,0x16,
    0x49,0xfe,0x53,0x47,0x13,0x1a,0xbd,0xa4,0xf1,0x40,0x19,0x60,0x0e,0xed,0x68,0x09,
    0x06,0x5f,0x4d,0xcf,0x3d,0x1a,0xfe,0x20,0x77,0xe4,0xd9,0xda,0xf9,0xa4,0x2b,0x76,
    0x1c,0x71,0xdb,0x00,0xbc,0xfd,0x0c,0x6c,0xa5,0x47,0xf7,0xf6,0x00,0x79,0x4a,0x11,
};

// (int)(|tan(106 + i * 0.1)| * 100) & 0xff for i in 0..<8, fixed so every libm agrees.
constexpr std::array<std::uint8_t, 8> tea_salt = {105, 86, 70, 56, 43, 32, 21, 11};

std::size_t folded_position(std::size_t position) { return position > 0x7fff ? position % 0x7fff : position; }

std::array<std::uint8_t, 8> tea_block(const std::uint8_t* bytes, ByteSpan key) {
    std::uint32_t v0 = u32be({bytes, 4});
    std::uint32_t v1 = u32be({bytes + 4, 4});
    std::array<std::uint32_t, 4> k{};
    for (std::size_t i = 0; i < 4; ++i) k[i] = u32be(key.subspan(i * 4, 4));
    std::uint32_t sum = 0x9e3779b9u * 16;
    for (int round = 0; round < 16; ++round) {
        v1 -= ((v0 << 4) + k[2]) ^ (v0 + sum) ^ ((v0 >> 5) + k[3]);
        v0 -= ((v1 << 4) + k[0]) ^ (v1 + sum) ^ ((v1 >> 5) + k[1]);
        sum -= 0x9e3779b9u;
    }
    std::array<std::uint8_t, 8> out{};
    for (std::size_t i = 0; i < 4; ++i) {
        out[i] = static_cast<std::uint8_t>(v0 >> (24 - 8 * i));
        out[i + 4] = static_cast<std::uint8_t>(v1 >> (24 - 8 * i));
    }
    return out;
}

}  // namespace

void qmc_apply_static(Bytes& bytes, std::size_t offset) {
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const std::size_t position = folded_position(offset + i);
        bytes[i] ^= static_box[(position * position + 27) & 0xff];
    }
}

void qmc_apply_map(Bytes& bytes, ByteSpan key, std::size_t offset) {
    if (key.empty()) fail_invalid("QMC 密钥为空");
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const std::size_t position = folded_position(offset + i);
        const std::size_t index = (position * position + 71214) % key.size();
        const unsigned rotate = ((index & 7) + 4) % 8;
        const unsigned value = key[index];
        bytes[i] ^= static_cast<std::uint8_t>((value << rotate | value >> rotate) & 0xff);
    }
}

void qmc_apply_rc4(Bytes& bytes, ByteSpan key, std::size_t offset) {
    if (key.empty()) fail_invalid("QMC 密钥为空");
    const std::size_t n = key.size();
    Bytes seed(n);
    for (std::size_t i = 0; i < n; ++i) seed[i] = static_cast<std::uint8_t>(i & 0xff);
    for (std::size_t i = 0, j = 0; i < n; ++i) {
        j = (seed[i] + j + key[i]) % n;
        std::swap(seed[i], seed[j]);
    }
    std::uint32_t hash = 1;
    for (std::uint8_t byte : key) {
        if (byte == 0) continue;
        const std::uint32_t next = hash * byte;
        if (next == 0 || next <= hash) break;
        hash = next;
    }
    // Returns -1 where JS produced NaN (a zero key byte), which upstream treats as "no mask".
    auto segment_key = [&](std::size_t id) -> std::int64_t {
        const std::size_t value = key[id % n];
        if (value == 0) return -1;
        const double scaled = std::floor(static_cast<double>(hash) / static_cast<double>((id + 1) * value) * 100);
        return static_cast<std::int64_t>(scaled) % static_cast<std::int64_t>(n);
    };

    std::size_t processed = 0;
    std::size_t position = offset;
    if (position < 128) {
        const std::size_t length = std::min(bytes.size(), 128 - position);
        for (std::size_t i = 0; i < length; ++i) {
            if (const auto index = segment_key(position + i); index >= 0) bytes[i] ^= key[static_cast<std::size_t>(index)];
        }
        processed += length;
        position += length;
    }
    Bytes box;
    while (processed < bytes.size()) {
        const std::size_t length = std::min(5120 - position % 5120, bytes.size() - processed);
        if (const auto segment = segment_key(position / 5120); segment >= 0) {
            const std::int64_t skip = static_cast<std::int64_t>(position % 5120) + segment;
            box = seed;
            std::size_t j = 0;
            std::size_t k = 0;
            for (std::int64_t index = -skip; index < static_cast<std::int64_t>(length); ++index) {
                j = (j + 1) % n;
                k = (box[j] + k) % n;
                std::swap(box[j], box[k]);
                if (index >= 0) bytes[processed + static_cast<std::size_t>(index)] ^= box[(box[j] + box[k]) % n];
            }
        }
        processed += length;
        position += length;
    }
}

Bytes decode_qmc(ByteSpan input) {
    if (input.size() < 8) fail_invalid("QMC 文件过短");
    const ByteSpan tail = input.last(4);
    if (starts_with(tail, "STag")) fail_unsupported("此 QQ 音乐文件未内嵌解密密钥，目前无法离线转换");
    std::optional<Bytes> key;
    std::size_t audio_size = input.size();
    if (starts_with(tail, "QTag")) {
        const std::size_t length = u32be(input.subspan(input.size() - 8, 4));
        if (length == 0 || length > input.size() - 8) fail_invalid("QMC QTag 长度无效");
        audio_size -= length + 8;
        const ByteSpan suffix = input.subspan(audio_size, length);
        const auto comma = std::find(suffix.begin(), suffix.end(), std::uint8_t{0x2c});
        if (comma == suffix.end()) fail_invalid("QMC QTag 密钥无效");
        key = derive_qmc_key(suffix.first(static_cast<std::size_t>(comma - suffix.begin())));
    } else if (const std::size_t length = u32le(tail); length < 0x300) {
        if (length == 0 || length > input.size() - 4) fail_invalid("QMC 内嵌密钥长度无效");
        audio_size -= length + 4;
        key = derive_qmc_key(input.subspan(audio_size, length));
    }
    if (audio_size == 0) fail_invalid("QMC 文件不包含音频");
    Bytes audio(input.begin(), input.begin() + static_cast<std::ptrdiff_t>(audio_size));
    if (!key) qmc_apply_static(audio);
    else if (key->size() > 300) qmc_apply_rc4(audio, *key);
    else qmc_apply_map(audio, *key);
    return audio;
}

Bytes derive_qmc_key(ByteSpan raw) {
    const auto decoded = base64_decode(std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()));
    if (!decoded || decoded->size() < 16) fail_invalid("QMC 内嵌密钥无效；新版外置 EKey 格式暂不支持");
    std::array<std::uint8_t, 16> tea_key{};
    for (std::size_t i = 0; i < 8; ++i) {
        tea_key[i * 2] = tea_salt[i];
        tea_key[i * 2 + 1] = (*decoded)[i];
    }
    Bytes result(decoded->begin(), decoded->begin() + 8);
    append(result, tea_decrypt(ByteSpan(*decoded).subspan(8), tea_key));
    return result;
}

Bytes tea_decrypt(ByteSpan bytes, ByteSpan key) {
    if (bytes.size() < 16 || bytes.size() % 8 != 0 || key.size() != 16) fail_invalid("QMC TEA 数据长度无效");
    auto plain = tea_block(bytes.data(), key);
    const std::size_t padding = plain[0] & 7;
    if (bytes.size() < 1 + padding + 2 + 7) fail_invalid("QMC TEA 填充无效");
    const std::size_t length = bytes.size() - 1 - padding - 2 - 7;
    std::array<std::uint8_t, 8> previous{};
    std::array<std::uint8_t, 8> current{};
    std::copy_n(bytes.begin(), 8, current.begin());
    std::size_t input_index = 8;
    std::size_t index = 1 + padding;
    auto next_byte = [&]() -> std::uint8_t {
        if (index == 8) {
            if (input_index > bytes.size() - 8) fail_invalid("QMC TEA 数据被截断");
            previous = current;
            std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(input_index), 8, current.begin());
            std::array<std::uint8_t, 8> mixed{};
            for (std::size_t i = 0; i < 8; ++i) mixed[i] = plain[i] ^ current[i];
            plain = tea_block(mixed.data(), key);
            input_index += 8;
            index = 0;
        }
        const std::uint8_t value = plain[index] ^ previous[index];
        ++index;
        return value;
    };
    next_byte();
    next_byte();
    Bytes result;
    result.reserve(length);
    for (std::size_t i = 0; i < length; ++i) result.push_back(next_byte());
    for (int i = 0; i < 7; ++i) {
        if (next_byte() != 0) fail_invalid("QMC TEA 密钥校验失败");
    }
    return result;
}

}  // namespace mu::detail
