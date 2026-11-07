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

/// Makes a UTF-8 file stem safe on Windows: no reserved characters or device names,
/// no trailing dots or spaces, at most 180 UTF-16 units. Never returns an empty string.
std::string sanitize_file_stem(std::string_view name);

}  // namespace mu
