#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace mu {

/// Core strings are UTF-8; invalid sequences become U+FFFD when converted.
std::u16string utf8_to_utf16(std::string_view text);
std::string utf16_to_utf8(std::u16string_view text);
std::string ascii_lower(std::string_view text);

std::string path_to_utf8(const std::filesystem::path& path);
std::filesystem::path utf8_to_path(std::string_view text);

}  // namespace mu
