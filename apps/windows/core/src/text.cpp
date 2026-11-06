#include "musicunlock/text.h"

#include <cstdint>

namespace mu {

namespace {

constexpr char32_t replacement = 0xfffd;

/// Decodes one UTF-8 scalar at `index`, rejecting overlong forms and surrogates.
char32_t next_scalar(std::string_view text, std::size_t& index) {
    auto byte = [&](std::size_t i) { return static_cast<std::uint8_t>(text[i]); };
    const std::uint8_t lead = byte(index++);
    if (lead < 0x80) return lead;
    int extra = 0;
    char32_t value = 0;
    char32_t minimum = 0;
    if ((lead & 0xe0) == 0xc0) { extra = 1; value = lead & 0x1f; minimum = 0x80; }
    else if ((lead & 0xf0) == 0xe0) { extra = 2; value = lead & 0x0f; minimum = 0x800; }
    else if ((lead & 0xf8) == 0xf0) { extra = 3; value = lead & 0x07; minimum = 0x10000; }
    else return replacement;
    for (int i = 0; i < extra; ++i) {
        if (index >= text.size() || (byte(index) & 0xc0) != 0x80) return replacement;
        value = value << 6 | (byte(index++) & 0x3f);
    }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return replacement;
    return value;
}

void append_utf8(std::string& out, char32_t value) {
    if (value < 0x80) {
        out.push_back(static_cast<char>(value));
    } else if (value < 0x800) {
        out.push_back(static_cast<char>(0xc0 | value >> 6));
        out.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else if (value < 0x10000) {
        out.push_back(static_cast<char>(0xe0 | value >> 12));
        out.push_back(static_cast<char>(0x80 | (value >> 6 & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | value >> 18));
        out.push_back(static_cast<char>(0x80 | (value >> 12 & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (value >> 6 & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    }
}

}  // namespace

std::u16string utf8_to_utf16(std::string_view text) {
    std::u16string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        char32_t value = next_scalar(text, i);
        if (value >= 0x10000) {
            value -= 0x10000;
            out.push_back(static_cast<char16_t>(0xd800 | value >> 10));
            out.push_back(static_cast<char16_t>(0xdc00 | (value & 0x3ff)));
        } else {
            out.push_back(static_cast<char16_t>(value));
        }
    }
    return out;
}

std::string utf16_to_utf8(std::u16string_view text) {
    std::string out;
    out.reserve(text.size() * 3);
    for (std::size_t i = 0; i < text.size(); ++i) {
        char32_t value = text[i];
        if (value >= 0xd800 && value <= 0xdbff && i + 1 < text.size() && text[i + 1] >= 0xdc00 &&
            text[i + 1] <= 0xdfff) {
            value = 0x10000 + ((value - 0xd800) << 10) + (text[++i] - 0xdc00);
        } else if (value >= 0xd800 && value <= 0xdfff) {
            value = replacement;
        }
        append_utf8(out, value);
    }
    return out;
}

std::string repair_utf8(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) append_utf8(out, next_scalar(text, i));
    return out;
}

bool is_valid_utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t start = i;
        // A literal U+FFFD in the input is valid; only a substituted one marks an error.
        if (next_scalar(text, i) == replacement && text.substr(start, i - start) != "\xEF\xBF\xBD") return false;
    }
    return true;
}

std::string_view trim_space(std::string_view text) {
    auto is_space = [](char32_t c) {
        return (c >= 0x09 && c <= 0x0d) || c == 0x20 || c == 0x85 || c == 0xa0 || c == 0x1680 ||
               (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f ||
               c == 0x3000;
    };
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t next = start;
        if (!is_space(next_scalar(text, next))) break;
        start = next;
    }
    std::size_t end = start;
    for (std::size_t i = start; i < text.size();) {
        if (!is_space(next_scalar(text, i))) end = i;
    }
    return text.substr(start, end - start);
}

std::string replace_all(std::string text, std::string_view from, std::string_view to) {
    if (from.empty()) return text;
    for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
    }
    return text;
}

std::string ascii_lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

std::string path_to_utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

std::filesystem::path utf8_to_path(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

}  // namespace mu
