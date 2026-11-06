#include "detail.h"
#include "musicunlock/text.h"

namespace mu {

const std::set<std::string, std::less<>>& qq_extensions() {
    static const std::set<std::string, std::less<>> extensions = {
        "qmc0",    "qmc2",    "qmc3",   "qmcflac", "qmcogg", "mflac",  "mflac0", "mgg",
        "mgg0",    "mgg1",    "mggl",   "tkm",     "bkcmp3", "bkcm4a", "bkcflac", "bkcwav",
        "bkcape",  "bkcogg",  "bkcwma", "666c6163", "6d7033", "6f6767", "6d3461",  "776176",
    };
    return extensions;
}

const std::set<std::string, std::less<>>& supported_extensions() {
    static const std::set<std::string, std::less<>> extensions = [] {
        auto all = qq_extensions();
        all.insert({"ncm", "uc", "kwm", "kgm", "kgma", "vpr", "xm", "tm0", "tm2", "tm3", "tm6"});
        return all;
    }();
    return extensions;
}

bool is_supported_extension(std::string_view ext) { return supported_extensions().contains(ascii_lower(ext)); }

std::string platform_for(std::string_view ext) {
    const std::string lower = ascii_lower(ext);
    if (lower == "ncm" || lower == "uc") return "网易云";
    if (lower == "kgm" || lower == "kgma" || lower == "vpr") return "酷狗";
    if (lower == "kwm") return "酷我";
    if (lower == "xm") return "虾米";
    return "QQ 音乐";
}

std::optional<std::string> sniff_audio(ByteSpan bytes) {
    using detail::starts_with;
    if (starts_with(bytes, "ID3")) return "mp3";
    if (starts_with(bytes, "fLaC")) return "flac";
    if (starts_with(bytes, "OggS")) return "ogg";
    if (bytes.size() >= 8 && starts_with(bytes.subspan(4), "ftyp")) return "m4a";
    if (bytes.size() >= 12 && starts_with(bytes, "RIFF") && starts_with(bytes.subspan(8), "WAVE")) return "wav";
    if (starts_with(bytes, "MAC ")) return "ape";
    if (starts_with(bytes, "FRM8")) return "dff";
    if (starts_with(bytes, std::array<std::uint8_t, 8>{0x30, 0x26, 0xb2, 0x75, 0x8e, 0x66, 0xcf, 0x11})) return "wma";
    if (bytes.size() >= 2 && bytes[0] == 0xff) {
        if ((bytes[1] & 0xf6) == 0xf0) return "aac";
        if ((bytes[1] & 0xe0) == 0xe0 && (bytes[1] & 0x06) != 0) return "mp3";
    }
    return std::nullopt;
}

DecodedAudio decode_audio(ByteSpan data, std::string_view file_extension, ByteSpan kgm_mask) {
    const std::string ext = ascii_lower(file_extension);
    if (!supported_extensions().contains(ext)) detail::fail_unsupported("暂不支持 ." + ext + " 格式");
    if (data.empty()) detail::fail_invalid("文件为空");
    DecodedAudio result;
    Bytes bytes;
    if (ext == "ncm") {
        auto ncm = detail::decode_ncm(data);
        bytes = std::move(ncm.audio);
        result.metadata = std::move(ncm.metadata);
    } else if (qq_extensions().contains(ext)) {
        bytes = detail::decode_qmc(data);
    } else {
        bytes = detail::decode_legacy(data, ext, kgm_mask);
    }
    auto output_ext = sniff_audio(bytes);
    if (!output_ext) {
        detail::fail_unsupported("未识别出有效音频；文件可能损坏，或使用了此版本尚不支持的加密方式");
    }
    result.file_extension = *output_ext;
    result.data = detail::restore_tags(std::move(bytes), result.file_extension, result.metadata);
    return result;
}

}  // namespace mu
