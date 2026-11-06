// MusicUnlock.exe, the single-file build: a small launcher with the whole app folder embedded as the PAYLOAD
// resource (written by launcher_pack, see scripts/pack.py). It extracts the folder once (install.h), then starts
// the app with the same command line and exits.
//
// MUSICUNLOCK_RUNTIME_DIR  extract under this folder instead of %LOCALAPPDATA%\MusicUnlock\runtime
// MUSICUNLOCK_LAUNCHER_LOG diagnostics for tests and measurements: append timings and errors to this file,
//                          show no dialog, wait for the app and return its exit code
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <new>

#include "install.h"

using namespace launcher;

namespace {

std::wstring environment(const wchar_t* name) {
    std::wstring value(GetEnvironmentVariableW(name, nullptr, 0), L'\0');
    if (value.empty()) return value;
    value.resize(GetEnvironmentVariableW(name, value.data(), static_cast<DWORD>(value.size())));
    return value;
}

/// Milliseconds since this process was created, so measurements include loading the executable.
double since_start() {
    FILETIME created, exited, kernel, user, now;
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    GetSystemTimePreciseAsFileTime(&now);
    const auto ticks = [](const FILETIME& t) { return (static_cast<std::int64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
    return static_cast<double>(ticks(now) - ticks(created)) / 10'000.0;
}

class Log {
public:
    explicit Log(std::wstring path) : path_(std::move(path)) {}
    bool enabled() const { return !path_.empty(); }
    template <typename... Args>
    void write(const wchar_t* format, Args... args) {
        FILE* file = nullptr;
        if (!enabled() || _wfopen_s(&file, path_.c_str(), L"a, ccs=UTF-8") != 0 || !file) return;
        fwprintf(file, L"[%8.1f ms] ", since_start());
        fwprintf(file, format, args...);
        fputwc(L'\n', file);
        fclose(file);
    }

private:
    std::wstring path_;
};

std::span<const std::uint8_t> embedded_payload() {
    HRSRC resource = FindResourceW(nullptr, L"PAYLOAD", MAKEINTRESOURCEW(10) /* RT_RCDATA */);
    HGLOBAL loaded = resource ? LoadResource(nullptr, resource) : nullptr;
    const void* data = loaded ? LockResource(loaded) : nullptr;
    if (!data) return {};
    return {static_cast<const std::uint8_t*>(data), SizeofResource(nullptr, resource)};
}

PROCESS_INFORMATION start_app(const std::filesystem::path& exe) {
    STARTUPINFOW own{};
    GetStartupInfoW(&own);
    // Keep the show state and the shortcut identity (taskbar pinning and jump lists follow the shortcut that
    // started us); inherited standard handles and hotkeys are not passed on.
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    startup.dwFlags = own.dwFlags & (STARTF_USESHOWWINDOW | STARTF_FORCEONFEEDBACK | STARTF_FORCEOFFFEEDBACK |
                                     STARTF_TITLEISLINKNAME | STARTF_TITLEISAPPID | STARTF_PREVENTPINNING);
    startup.wShowWindow = own.wShowWindow;
    if (startup.dwFlags & (STARTF_TITLEISLINKNAME | STARTF_TITLEISAPPID)) startup.lpTitle = own.lpTitle;
    // The arguments go through untouched: Explorer passes dropped files and "open with" paths here, and the app
    // reads them back with CommandLineToArgvW (shell::command_line_paths).
    std::wstring command = L"\"" + exe.wstring() + L"\"";
    if (const auto arguments = arguments_after_program(GetCommandLineW()); !arguments.empty()) {
        command += L' ';
        command += arguments;
    }
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
        throw Error{Problem::launch, GetLastError(), exe};
    }
    AllowSetForegroundWindow(process.dwProcessId);
    CloseHandle(process.hThread);
    return process;
}

const wchar_t* action_name(Prepared::Action action) {
    switch (action) {
    case Prepared::Action::extracted: return L"extracted";
    case Prepared::Action::repaired: return L"repaired";
    case Prepared::Action::reused: break;
    }
    return L"reused";
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    Log log(environment(L"MUSICUNLOCK_LAUNCHER_LOG"));
    try {
        Payload payload;
        try {
            payload = parse(embedded_payload());
        } catch (const FormatError&) {
            throw Error{Problem::corrupt};
        }
        const auto root = default_root();
        HANDLE app = nullptr;
        {
            // Held until the app has started, so no other launcher removes the folder in between; afterwards
            // the running executable keeps it from being removed.
            RootLock lock(root, 180'000);
            const auto prepared = prepare(payload, root);
            log.write(L"prepare %s %s: %zu files changed, decompress %.1f ms, write and verify %.1f ms",
                      action_name(prepared.action), prepared.folder.c_str(), prepared.repaired_files,
                      prepared.decompress_ms, prepared.write_ms);
            const auto process = start_app(prepared.folder / std::wstring(app_executable.begin(), app_executable.end()));
            log.write(L"started %lu", process.dwProcessId);
            app = process.hProcess;
            const auto cleanup = remove_old_versions(root, payload.version());
            log.write(L"cleanup: %zu removed, %zu in use", cleanup.removed, cleanup.in_use);
        }
        DWORD code = 0;
        if (log.enabled()) {
            WaitForSingleObject(app, INFINITE);
            GetExitCodeProcess(app, &code);
            log.write(L"app exited 0x%08X", code);
        }
        CloseHandle(app);
        return static_cast<int>(code);
    } catch (...) {
        std::wstring text;
        int problem = -1;
        try {
            throw;
        } catch (const Error& error) {
            text = message(error);
            problem = static_cast<int>(error.problem);
        } catch (const std::bad_alloc&) {
            // Decompressing needs memory for the whole app folder.
            text = L"内存不足，无法解压音匣的运行文件。\n\n请关闭一些程序后再打开。";
        } catch (...) {
            text = message(Error{Problem::io});
        }
        if (log.enabled()) {
            log.write(L"error %d: %s", problem, text.c_str());
        } else {
            MessageBoxW(nullptr, text.c_str(), L"音匣", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
        }
    }
    return 1;
}
