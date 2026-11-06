#include "detail.h"

namespace mu::detail {

namespace {
constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int sextet(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
}  // namespace

std::optional<Bytes> base64_decode(std::string_view text) {
    if (text.size() % 4 != 0) return std::nullopt;
    std::size_t padding = 0;
    if (!text.empty() && text.back() == '=') padding = text.size() >= 2 && text[text.size() - 2] == '=' ? 2 : 1;
    Bytes out;
    out.reserve(text.size() / 4 * 3);
    std::uint32_t buffer = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        int value = 0;
        if (i >= text.size() - padding) {
            value = 0;
        } else if ((value = sextet(text[i])) < 0) {
            return std::nullopt;
        }
        buffer = buffer << 6 | static_cast<std::uint32_t>(value);
        if (i % 4 == 3) {
            out.push_back(static_cast<std::uint8_t>(buffer >> 16));
            out.push_back(static_cast<std::uint8_t>(buffer >> 8));
            out.push_back(static_cast<std::uint8_t>(buffer));
            buffer = 0;
        }
    }
    out.resize(out.size() - padding);
    return out;
}

std::string base64_encode(ByteSpan bytes) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::size_t remaining = bytes.size() - i;
        std::uint32_t buffer = std::uint32_t{bytes[i]} << 16;
        if (remaining > 1) buffer |= std::uint32_t{bytes[i + 1]} << 8;
        if (remaining > 2) buffer |= bytes[i + 2];
        out.push_back(alphabet[buffer >> 18 & 63]);
        out.push_back(alphabet[buffer >> 12 & 63]);
        out.push_back(remaining > 1 ? alphabet[buffer >> 6 & 63] : '=');
        out.push_back(remaining > 2 ? alphabet[buffer & 63] : '=');
    }
    return out;
}

}  // namespace mu::detail
