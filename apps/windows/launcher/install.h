#pragma once
// Extracts the payload to <root>\<version>\ and keeps it intact. Windows only.
//
// - First start: decompress, write every file into <root>\.tmp-<version>-<pid>, hash each file back from disk,
//   record a stamp, then rename the folder into place, so a half-written folder never looks complete.
// - Warm start: compare file sizes and last-write times with the stamp and look for extra files (no hashing,
//   nothing decompressed). Anything off is repaired file by file, so a running copy keeps working.
// - Other version folders are removed when nothing has a file open in them (renaming the folder fails then).
// A named mutex per root serialises all of this between launchers starting at the same time.

#include <windows.h>

#include <filesystem>
#include <string>

#include "payload.h"

namespace launcher {

/// The executable the launcher starts, at the top of the payload.
inline constexpr std::string_view app_executable = "MusicUnlock.exe";
/// The stamp file inside every extracted folder.
inline constexpr std::wstring_view stamp_name = L".musicunlock-stamp";

enum class Problem {
    corrupt,       // the embedded payload is damaged
    mismatch,      // a file read back from disk differs from what was written
    disk_full,
    not_writable,  // access denied, write-protected or missing folder
    in_use,        // a file to repair is open in a running copy
    busy,          // another launcher held the lock too long
    launch,        // CreateProcess failed
    io,
};

/// Thrown for every failure the user can act on.
struct Error {
    Problem problem = Problem::io;
    DWORD code = 0;  // Win32 error, when there is one
    std::filesystem::path path;
    std::uint64_t needed = 0, available = 0;  // disk_full: bytes
};

/// Chinese message for the error dialog, with what to do next.
std::wstring message(const Error& error);

/// MUSICUNLOCK_RUNTIME_DIR when set (tests and previews keep everything inside the build folder),
/// otherwise %LOCALAPPDATA%\MusicUnlock\runtime.
std::filesystem::path default_root();

/// Holds the per-root mutex; waits up to `timeout_ms` for another launcher to finish.
class RootLock {
public:
    RootLock(const std::filesystem::path& root, DWORD timeout_ms);
    ~RootLock();
    RootLock(const RootLock&) = delete;
    RootLock& operator=(const RootLock&) = delete;

private:
    HANDLE mutex_ = nullptr;
};

struct Prepared {
    enum class Action { reused, extracted, repaired } action = Action::reused;
    std::filesystem::path folder;
    std::size_t repaired_files = 0;     // replaced or removed
    double decompress_ms = 0, write_ms = 0;  // zero when reused
};

/// Makes <root>\<version> match the payload: reuses it, repairs it or extracts it. Call with the RootLock held.
/// Throws Error.
Prepared prepare(const Payload& payload, const std::filesystem::path& root);

struct Cleanup {
    std::size_t removed = 0, in_use = 0;
};
/// Removes other version folders that are not in use and leftovers of interrupted runs. Never throws.
Cleanup remove_old_versions(const std::filesystem::path& root, std::string_view keep);

}  // namespace launcher
