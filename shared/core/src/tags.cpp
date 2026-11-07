// Restores container metadata using standard ID3v2.3 / FLAC blocks; encoded audio frames are untouched.
#include <utility>

#include "detail.h"
#include "musicunlock/text.h"

namespace mu::detail {

namespace {

std::string_view cover_mime(const Bytes& cover) {
    return starts_with(cover, std::array<std::uint8_t, 4>{0x89, 0x50, 0x4e, 0x47}) ? "image/png" : "image/jpeg";
}

Bytes tag_mp3(Bytes audio, const AudioMetadata& metadata) {
    // Preserve existing ID3 metadata instead of replacing its unknown frames.
    if (starts_with(audio, "ID3")) return audio;
    Bytes frames;
    auto add_frame = [&](std::string_view id, const Bytes& payload) {
        append(frames, id);
        append_be32(frames, payload.size());
        frames.insert(frames.end(), {0, 0});
        append(frames, payload);
    };
    const std::pair<std::string_view, const std::string*> texts[] = {
        {"TIT2", &metadata.title}, {"TPE1", &metadata.artist}, {"TALB", &metadata.album}};
    for (const auto& [id, text] : texts) {
        if (text->empty()) continue;
        Bytes payload = {1, 0xff, 0xfe};  // UTF-16 with little-endian BOM
        for (char16_t unit : utf8_to_utf16(*text)) {
            payload.push_back(static_cast<std::uint8_t>(unit));
            payload.push_back(static_cast<std::uint8_t>(unit >> 8));
        }
        add_frame(id, payload);
    }
    if (metadata.cover) {
        Bytes payload = {0};
        append(payload, cover_mime(*metadata.cover));
        payload.insert(payload.end(), {0, 3, 0});
        append(payload, *metadata.cover);
        add_frame("APIC", payload);
    }
    if (frames.size() >= std::size_t{1} << 28) fail_invalid("ID3 标签过大");
    const std::size_t size = frames.size();
    Bytes result = {0x49, 0x44, 0x33, 3, 0, 0};
    for (int shift : {21, 14, 7, 0}) result.push_back(static_cast<std::uint8_t>((size >> shift) & 127));
    append(result, frames);
    append(result, audio);
    return result;
}

Bytes tag_flac(const Bytes& audio, const AudioMetadata& metadata) {
    ByteReader reader{audio, 4};
    std::vector<std::pair<std::uint8_t, Bytes>> blocks;
    for (bool last = false; !last;) {
        const ByteSpan header = reader.take(4);
        last = (header[0] & 0x80) != 0;
        const std::size_t length = std::size_t{header[1]} << 16 | std::size_t{header[2]} << 8 | header[3];
        const ByteSpan body = reader.take(length);
        blocks.emplace_back(static_cast<std::uint8_t>(header[0] & 0x7f), Bytes(body.begin(), body.end()));
    }
    auto has_block = [&](std::uint8_t type) {
        return std::any_of(blocks.begin(), blocks.end(), [&](const auto& block) { return block.first == type; });
    };
    if (!has_block(4)) {
        constexpr std::string_view vendor = "MusicUnlock";
        std::vector<std::string> comments;
        for (const auto& [key, value] : {std::pair{"TITLE", &metadata.title}, std::pair{"ARTIST", &metadata.artist},
                                         std::pair{"ALBUM", &metadata.album}}) {
            if (!value->empty()) comments.push_back(std::string(key) + "=" + *value);
        }
        Bytes payload;
        append_le32(payload, vendor.size());
        append(payload, vendor);
        append_le32(payload, comments.size());
        for (const auto& comment : comments) {
            append_le32(payload, comment.size());
            append(payload, comment);
        }
        blocks.emplace_back(std::uint8_t{4}, std::move(payload));
    }
    if (metadata.cover && !has_block(6)) {
        const std::string_view mime = cover_mime(*metadata.cover);
        Bytes picture;
        append_be32(picture, 3);  // front cover
        append_be32(picture, mime.size());
        append(picture, mime);
        append_be32(picture, 0);  // empty description
        picture.insert(picture.end(), 16, 0);  // width, height, depth, palette size
        append_be32(picture, metadata.cover->size());
        append(picture, *metadata.cover);
        if (picture.size() < std::size_t{1} << 24) blocks.emplace_back(std::uint8_t{6}, std::move(picture));
    }
    Bytes result;
    append(result, "fLaC");
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        const auto& [type, body] = blocks[index];
        if (body.size() >= std::size_t{1} << 24) fail_invalid("FLAC 标签过大");
        result.push_back(static_cast<std::uint8_t>(type | (index == blocks.size() - 1 ? 0x80 : 0)));
        for (int shift : {16, 8, 0}) result.push_back(static_cast<std::uint8_t>((body.size() >> shift) & 255));
        append(result, body);
    }
    result.insert(result.end(), audio.begin() + static_cast<std::ptrdiff_t>(reader.position), audio.end());
    return result;
}

}  // namespace

Bytes restore_tags(Bytes audio, std::string_view ext, const AudioMetadata& metadata) {
    if (metadata.empty()) return audio;
    if (ext == "mp3") return tag_mp3(std::move(audio), metadata);
    if (ext == "flac") return tag_flac(audio, metadata);
    return audio;
}

}  // namespace mu::detail
