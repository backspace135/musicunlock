// Stand-in for MusicUnlock.exe in the launcher smoke test (scripts/smoke_launcher.py): records how it was started
// and exits with code 7. No window and no console, like the real app's subsystem.
//
// MUSICUNLOCK_PROBE_OUT      UTF-8 file to write: executable path, working folder, MUSICUNLOCK_LAUNCHER_PATH,
//                            then one line per argument
// MUSICUNLOCK_PROBE_HOLD_MS  then keep running this long (a copy that is still in use)
#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <string>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    wchar_t value[MAX_PATH];
    if (GetEnvironmentVariableW(L"MUSICUNLOCK_PROBE_OUT", value, MAX_PATH)) {
        const std::wstring final_path = value, temporary = final_path + L".part";
        // Written under a temporary name first, so the test never reads half a file.
        FILE* file = nullptr;
        if (_wfopen_s(&file, temporary.c_str(), L"w, ccs=UTF-8") != 0 || !file) return 3;
        wchar_t exe[MAX_PATH], folder[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        GetCurrentDirectoryW(MAX_PATH, folder);
        fwprintf(file, L"%s\n%s\n", exe, folder);
        wchar_t launcher[MAX_PATH] = L"";
        GetEnvironmentVariableW(L"MUSICUNLOCK_LAUNCHER_PATH", launcher, MAX_PATH);
        fwprintf(file, L"%s\n", launcher);
        int count = 0;
        LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
        for (int i = 1; arguments && i < count; ++i) fwprintf(file, L"%s\n", arguments[i]);
        LocalFree(arguments);
        fclose(file);
        MoveFileExW(temporary.c_str(), final_path.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    if (GetEnvironmentVariableW(L"MUSICUNLOCK_PROBE_HOLD_MS", value, MAX_PATH)) Sleep(std::wcstoul(value, nullptr, 10));
    return 7;
}
