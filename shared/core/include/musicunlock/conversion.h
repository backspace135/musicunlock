#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "musicunlock/audio.h"

namespace mu {

enum class NamingPolicy { original, artist_title, title };

std::string_view naming_label(NamingPolicy policy);

struct ConversionOutput {
    std::filesystem::path path;
    std::string file_extension;
    AudioMetadata metadata;
};

/// Decodes `source` into `directory` without touching the source. Existing files are never
/// overwritten; collisions get " (n)" suffixes, created atomically so concurrent exports are safe.
ConversionOutput convert_file(const std::filesystem::path& source, const std::filesystem::path& directory,
                              NamingPolicy naming, ByteSpan kgm_mask = {});

/// convert_file for a source that is already in memory, such as a file the Android app maps from a file
/// descriptor. `source_name` is the source's file name: its extension picks the decoder and its stem is the
/// name kept by NamingPolicy::original.
ConversionOutput convert_data(ByteSpan data, std::string_view source_name, const std::filesystem::path& directory,
                              NamingPolicy naming, ByteSpan kgm_mask = {});

/// How many names an export tries before giving up: `numbered_file_name` with suffix 0 to max_name_suffix - 1.
inline constexpr int max_name_suffix = 10000;

/// The name an export tries for a collision `suffix`: "name.ext", then "name (1).ext", "name (2).ext"...
/// Platforms that publish files themselves (Android's MediaStore) number them the same way.
std::string numbered_file_name(std::string_view stem, std::string_view extension, int suffix);

/// Makes a UTF-8 file stem safe on Windows: no reserved characters or device names,
/// no trailing dots or spaces, at most 180 UTF-16 units. Never returns an empty string.
std::string sanitize_file_stem(std::string_view name);

}  // namespace mu
