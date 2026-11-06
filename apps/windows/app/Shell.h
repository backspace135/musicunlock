#pragma once
// Small Win32 shell helpers for the UI: known folders and Explorer.

#include <windows.h>

#include <filesystem>
#include <optional>
#include <vector>

namespace shell {

/// System file dialog. Folders mode picks folders; otherwise files filtered to the supported formats.
/// Returns nothing when cancelled.
std::vector<std::filesystem::path> pick_audio(HWND owner, bool folders);
/// Single folder, starting at `initial` when it exists.
std::optional<std::filesystem::path> pick_folder(HWND owner, const std::filesystem::path& initial,
                                                 const wchar_t* title = L"选择输出文件夹");
/// A local LRC or TXT lyric file.
std::optional<std::filesystem::path> pick_lyric_file(HWND owner);
/// Paths passed on the command line (Explorer "open with" and drag onto the exe).
std::vector<std::filesystem::path> command_line_paths();

/// The user's Downloads folder (falls back to the profile folder).
std::filesystem::path downloads_folder();
/// Opens an Explorer window with `path` selected.
void reveal(const std::filesystem::path& path);
/// Opens `folder` in Explorer.
void open_folder(const std::filesystem::path& folder);
/// Opens a file shipped next to the executable with its default app.
void open_bundled(const wchar_t* name);

}  // namespace shell
