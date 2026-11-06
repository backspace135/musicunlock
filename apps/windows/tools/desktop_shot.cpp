// Runs MusicUnlock.exe on a private desktop so preview renders never show on, or capture, the user's desktop.
// The app renders its own XAML tree to PNG (MUSICUNLOCK_RENDER_DIR) and exits; the desktop is closed afterwards.
//
//   desktop_shot.exe <MusicUnlock.exe> <output directory> [folder to import and convert]
//
// Writes <output directory>\shot.done with the app's exit code when finished. Started by scripts/screenshot.py.
#include <windows.h>

#include <cstdio>
#include <string>

namespace {

void finish(const std::wstring& directory, const std::wstring& status) {
    FILE* file = nullptr;
    if (_wfopen_s(&file, (directory + L"\\shot.done").c_str(), L"w") == 0 && file) {
        fwprintf(file, L"%s\n", status.c_str());
        fclose(file);
    }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3 && argc != 4) return 2;
    const std::wstring app = argv[1];
    const std::wstring directory = argv[2];
    HDESK desktop = CreateDesktopW(L"MusicUnlockShot", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desktop) {
        finish(directory, L"CreateDesktop failed " + std::to_wstring(GetLastError()));
        return 1;
    }
    // Every process the app starts stays in this job and ends with it.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits);

    SetEnvironmentVariableW(L"MUSICUNLOCK_RENDER_DIR", directory.c_str());
    SetEnvironmentVariableW(L"MUSICUNLOCK_LOG", (directory + L"\\app.log").c_str());
    SetEnvironmentVariableW(L"MUSICUNLOCK_RENDER_CONVERT", argc == 4 ? argv[3] : nullptr);
    // Keep anything the app saves inside the output folder, never in the profile.
    SetEnvironmentVariableW(L"MUSICUNLOCK_DATA_DIR", (directory + L"\\appdata").c_str());
    wchar_t desktop_name[] = L"WinSta0\\MusicUnlockShot";
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    startup.lpDesktop = desktop_name;
    PROCESS_INFORMATION process{};
    std::wstring command = L"\"" + app + L"\"";
    const std::wstring folder = app.substr(0, app.find_last_of(L'\\'));
    std::wstring status;
    if (CreateProcessW(app.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, folder.c_str(),
                       &startup, &process)) {
        AssignProcessToJobObject(job, process.hProcess);
        ResumeThread(process.hThread);
        if (WaitForSingleObject(process.hProcess, 120'000) == WAIT_TIMEOUT) {
            status = L"timeout";
        } else {
            DWORD code = 0;
            GetExitCodeProcess(process.hProcess, &code);
            wchar_t text[16];
            swprintf_s(text, L"0x%08X", code);
            status = text;
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    } else {
        status = L"CreateProcess failed " + std::to_wstring(GetLastError());
    }
    CloseHandle(job);  // ends anything left over
    CloseDesktop(desktop);
    finish(directory, status);
    return 0;
}
