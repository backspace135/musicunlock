#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace mu {

/// Core strings are UTF-8; invalid sequences become U+FFFD when converted.
std::u16string utf8_to_utf16(std::string_view text);
std::string utf16_to_utf8(std::u16string_view text);
std::string ascii_lower(std::string_view text);
/// Replaces invalid UTF-8 with U+FFFD.
std::string repair_utf8(std::string_view text);
bool is_valid_utf8(std::string_view text);
/// Trims Unicode whitespace and line breaks (Foundation's whitespacesAndNewlines).
std::string_view trim_space(std::string_view text);
std::string replace_all(std::string text, std::string_view from, std::string_view to);

std::string path_to_utf8(const std::filesystem::path& path);
std::filesystem::path utf8_to_path(std::string_view text);

}  // namespace mu
