// Container and key-box logic adapted from Unlock Music, Copyright (c) 2019-2021 MengYX (MIT).
#include <nlohmann/json.hpp>

#include "detail.h"

namespace mu::detail {

const std::array<std::uint8_t, 16> ncm_core_key = {0x68, 0x7a, 0x48, 0x52, 0x41, 0x6d, 0x73, 0x6f,
                                                   0x35, 0x6b, 0x49, 0x6e, 0x62, 0x61, 0x78, 0x57};
const std::array<std::uint8_t, 16> ncm_meta_key = {0x23, 0x31, 0x34, 0x6c, 0x6a, 0x6b, 0x5f, 0x21,
                                                   0x5c, 0x5d, 0x26, 0x30, 0x55, 0x3c, 0x27, 0x28};

namespace {

Bytes decrypt_aes(ByteSpan input, ByteSpan key) {
    if (input.empty() || input.size() % 16 != 0) fail_invalid("NCM AES 数据长度无效");
    auto plain = aes128_ecb(AesMode::decrypt, input, key);
    if (!plain) fail_invalid("NCM 密钥或元数据解密失败");
    return std::move(*plain);
}

std::string string_field(const nlohmann::json& object, const char* key) {
    auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string();
}

void read_metadata(ByteSpan encrypted_block, AudioMetadata& metadata) {
    Bytes encrypted(encrypted_block.begin(), encrypted_block.end());
    for (auto& byte : encrypted) byte ^= 0x63;
    // Layout: "163 key(Don't modify):" + base64(AES("music:" + JSON)).
    std::optional<Bytes> cipher;
    if (encrypted.size() > 22) {
        cipher = base64_decode(std::string_view(reinterpret_cast<const char*>(encrypted.data()) + 22,
                                                encrypted.size() - 22));
    }
    if (!cipher) fail_invalid("NCM 元数据无效");
    const Bytes plain = decrypt_aes(*cipher, ncm_meta_key);
    const auto separator = std::find(plain.begin(), plain.end(), std::uint8_t{0x3a});
    nlohmann::json json = nlohmann::json::parse(separator == plain.end() ? plain.end() : separator + 1,
                                                plain.end(), nullptr, false);
    if (separator == plain.end() || !json.is_object()) fail_invalid("NCM 元数据无法读取");
    if (auto main = json.find("mainMusic"); main != json.end() && main->is_object()) json = *main;
    metadata.title = string_field(json, "musicName");
    metadata.album = string_field(json, "album");
    // "artist" is [[name, id], ...]; anything else is ignored rather than guessed.
    if (auto artists = json.find("artist"); artists != json.end() && artists->is_array() &&
                                            std::all_of(artists->begin(), artists->end(),
                                                        [](const auto& item) { return item.is_array(); })) {
        for (const auto& artist : *artists) {
            if (artist.empty() || !artist.front().is_string()) continue;
            if (!metadata.artist.empty()) metadata.artist += "; ";
            metadata.artist += artist.front().template get<std::string>();
        }
    }
}

}  // namespace

NcmResult decode_ncm(ByteSpan data) {
    ByteReader reader{data};
    if (!starts_with(reader.take(8), "CTENFDAM")) fail_invalid("不是有效的 NCM 文件");
    reader.take(2);
    const ByteSpan key_block = reader.take(reader.little32());
    Bytes encrypted_key(key_block.begin(), key_block.end());
    for (auto& byte : encrypted_key) byte ^= 0x64;
    const Bytes plaintext_key = decrypt_aes(encrypted_key, ncm_core_key);
    if (plaintext_key.size() <= 17 || !starts_with(plaintext_key, "neteasecloudmusic")) {
        fail_invalid("NCM 音频密钥无效");
    }
    const ByteSpan key = ByteSpan(plaintext_key).subspan(17);
    std::array<std::uint8_t, 256> box{};
    for (std::size_t i = 0; i < 256; ++i) box[i] = static_cast<std::uint8_t>(i);
    for (std::size_t i = 0, j = 0; i < 256; ++i) {
        j = (box[i] + j + key[i % key.size()]) & 0xff;
        std::swap(box[i], box[j]);
    }
    std::array<std::uint8_t, 256> mask{};
    for (std::size_t index = 0; index < 256; ++index) {
        const std::size_t i = (index + 1) & 0xff;
        const std::size_t si = box[i];
        const std::size_t sj = box[(i + si) & 0xff];
        mask[index] = box[(si + sj) & 0xff];
    }

    NcmResult result;
    if (const std::size_t meta_length = reader.little32(); meta_length > 0) {
        read_metadata(reader.take(meta_length), result.metadata);
    }
    reader.take(5);  // CRC32 + reserved byte
    const std::size_t cover_space = reader.little32();
    const std::size_t cover_length = reader.little32();
    if (cover_length > cover_space) fail_invalid("NCM 封面长度无效");
    const ByteSpan cover = reader.take(cover_space);
    if (cover_length > 0) result.metadata.cover = Bytes(cover.begin(), cover.begin() + cover_length);

    result.audio.assign(data.begin() + reader.position, data.end());
    for (std::size_t i = 0; i < result.audio.size(); ++i) result.audio[i] ^= mask[i & 0xff];
    return result;
}

}  // namespace mu::detail
