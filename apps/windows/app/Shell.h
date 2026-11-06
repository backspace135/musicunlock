#pragma once
// Small Win32 shell helpers for the UI: known folders and Explorer.

#include <filesystem>

namespace shell {

/// The user's Downloads folder (falls back to the profile folder).
std::filesystem::path downloads_folder();
/// Opens an Explorer window with `path` selected.
void reveal(const std::filesystem::path& path);
/// Opens `folder` in Explorer.
void open_folder(const std::filesystem::path& folder);

}  // namespace shell
