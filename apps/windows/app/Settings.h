#pragma once
// Remembered export settings (macOS keeps them in UserDefaults). Stored as JSON in
// %LOCALAPPDATA%\MusicUnlock\settings.json, or in MUSICUNLOCK_DATA_DIR when that is set (tests, renders).

#include <filesystem>
#include <optional>

#include "musicunlock/conversion.h"
#include "musicunlock/lyrics.h"

namespace settings {

struct Export {
    std::optional<std::filesystem::path> output_directory;
    mu::NamingPolicy naming = mu::NamingPolicy::original;
};

/// Lyrics workspace export settings, kept in lyrics.json next to settings.json.
struct Lyrics {
    mu::LyricOptions options;
    std::optional<std::filesystem::path> output_directory;
};

std::filesystem::path data_directory();
Export load();
/// Best effort: a read-only or missing profile folder must not interrupt the user.
void save(const Export& value);
Lyrics load_lyrics();
void save_lyrics(const Lyrics& value);

}  // namespace settings
