#include "musicunlock/conversion.h"

#include "detail.h"
#include "musicunlock/text.h"

namespace mu {

namespace {

bool is_space(char16_t c) {
    return (c >= 0x09 && c <= 0x0d) || c == 0x20 || c == 0x85 || c == 0xa0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
}

bool is_reserved_device(std::u16string_view stem) {
    // Windows maps these names to devices even when an extension follows ("CON.flac").
    const std::u16string_view base = stem.substr(0, stem.find(u'.'));
    std::string ascii;
    for (char16_t c : base) {
        if (c > 0x7f) return false;
        ascii.push_back(static_cast<char>(c));
    }
    while (!ascii.empty() && ascii.back() == ' ') ascii.pop_back();
    ascii = ascii_lower(ascii);
    if (ascii == "con" || ascii == "prn" || ascii == "aux" || ascii == "nul") return true;
    return ascii.size() == 4 && (ascii.starts_with("com") || ascii.starts_with("lpt")) && ascii[3] >= '1' &&
           ascii[3] <= '9';
}

}  // namespace

std::string_view naming_label(NamingPolicy policy) {
    switch (policy) {
        case NamingPolicy::artist_title: return "歌手 - 歌曲名";
        case NamingPolicy::title: return "歌曲名";
        case NamingPolicy::original: break;
    }
    return "保留原文件名";
}

namespace detail {

std::string sanitize_component(std::string_view name, std::size_t max_units, std::string_view fallback,
                               bool trim_leading_dots) {
    std::u16string text;
    for (char16_t c : utf8_to_utf16(name)) {
        const bool reserved = c < 0x20 || c == 0x7f || std::u16string_view(u"<>:\"/\\|?*").find(c) != std::u16string_view::npos;
        text.push_back(reserved ? u'_' : c);
    }
    if (text.size() > max_units) {
        std::size_t cut = max_units;
        if (text[cut - 1] >= 0xd800 && text[cut - 1] <= 0xdbff) --cut;  // keep surrogate pairs whole
        text.resize(cut);
    }
    std::size_t start = 0;
    while (start < text.size() && (is_space(text[start]) || (trim_leading_dots && text[start] == u'.'))) ++start;
    std::size_t end = text.size();
    // Windows silently drops trailing dots and spaces, which would change the name we checked.
    while (end > start && (is_space(text[end - 1]) || text[end - 1] == u'.')) --end;
    text = text.substr(start, end - start);
    if (text.empty()) return std::string(fallback);
    if (is_reserved_device(text)) text.insert(text.begin(), u'_');
    return utf16_to_utf8(text);
}

}  // namespace detail

std::string sanitize_file_stem(std::string_view name) { return detail::sanitize_component(name, 180, "音频", false); }

ConversionOutput convert_file(const std::filesystem::path& source, const std::filesystem::path& directory,
                              NamingPolicy naming, ByteSpan kgm_mask) {
    const Bytes original = detail::read_file(source);
    std::string ext = path_to_utf8(source.extension());
    if (!ext.empty() && ext.front() == '.') ext.erase(0, 1);
    DecodedAudio result = decode_audio(original, ext, kgm_mask);

    std::string name = path_to_utf8(source.stem());
    const AudioMetadata& meta = result.metadata;
    if (naming == NamingPolicy::title && !meta.title.empty()) {
        name = meta.title;
    } else if (naming == NamingPolicy::artist_title && !meta.title.empty()) {
        name = meta.artist.empty() ? meta.title : meta.artist + " - " + meta.title;
    }
    name = sanitize_file_stem(name);

    for (int suffix = 0; suffix < 10000; ++suffix) {
        const std::string stem = suffix == 0 ? name : name + " (" + std::to_string(suffix) + ")";
        const std::filesystem::path target = directory / utf8_to_path(stem + "." + result.file_extension);
        if (detail::write_new_file(target, result.data)) {
            return {target, result.file_extension, std::move(result.metadata)};
        }
    }
    detail::fail_invalid("输出目录同名文件过多，请选择其他目录");
}

}  // namespace mu
