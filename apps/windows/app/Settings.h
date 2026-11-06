#pragma once
// Remembered export settings (macOS keeps them in UserDefaults). Stored as JSON in
// %LOCALAPPDATA%\MusicUnlock\settings.json, or in MUSICUNLOCK_DATA_DIR when that is set (tests, renders).

#include <filesystem>
#include <optional>

#include "musicunlock/conversion.h"

namespace settings {

struct Export {
    std::optional<std::filesystem::path> output_directory;
    mu::NamingPolicy naming = mu::NamingPolicy::original;
};

std::filesystem::path data_directory();
Export load();
/// Best effort: a read-only or missing profile folder must not interrupt the user.
void save(const Export& value);

}  // namespace settings
