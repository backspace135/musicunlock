#pragma once
// Turning dropped or picked paths into convertible files. Shared by the app and the CLI.

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace mu {

struct ImportedFile {
    std::filesystem::path path;
    std::uint64_t size = 0;
};

struct ImportScan {
    std::vector<ImportedFile> files;
    std::size_t skipped = 0;  // regular files with an unsupported extension
};

/// Supported audio files below `directory`, recursively. Hidden files and folders (dot names, and the
/// hidden attribute on Windows) are skipped; unreadable folders are ignored. Adds other files to `skipped`.
std::vector<ImportedFile> find_audio_files(const std::filesystem::path& directory, std::size_t* skipped = nullptr);

/// The app's import rule (macOS AppModel.importURLs): folders are expanded, other paths are kept only
/// when they are supported regular files. Results are absolute, deduplicated and naturally sorted.
ImportScan scan_import(const std::vector<std::filesystem::path>& inputs);

/// Finder / Explorer order: case-insensitive, digit runs compared by value ("2" before "10").
bool natural_less(std::string_view a, std::string_view b);

}  // namespace mu
