// Runs MusicUnlock.exe on a private desktop so preview renders never show on, or capture, the user's desktop.
// The app renders its own XAML tree to PNG (MUSICUNLOCK_RENDER_DIR) and exits; the desktop is closed afterwards.
//
//   desktop_shot.exe [--shortcut plain|app-id] <desktop name> <MusicUnlock.exe> <output directory>
//                    [folder to import and convert [file...]] [--motion <scenario,scenario|all>]
//
// The desktop name is unique per checkout, so parallel worktrees never share a desktop. Files after the folder
// are passed on the app's command line, as Explorer does for files dropped on the exe. The single-file
// MusicUnlock.exe extracts into <output directory>\..\runtime and logs to launcher.log; it waits for the app.
// The app writes its taskbar identity to taskbar.log. --shortcut starts it as Explorer starts a shortcut
// (STARTF_TITLEISLINKNAME): <output directory>\start.lnk to the exe, without or with the app's AppUserModelID.
//
// Writes <output directory>\shot.done with the app's exit code when finished. Started by scripts/screenshot.py.
#include <windows.h>
#include <propvarutil.h>
#include <shobjidl.h>

#include <cstdio>
#include <string>

// The PKEY_ constants are only declared without INITGUID.
#include <initguid.h>
#include <propkey.h>

namespace {

void finish(const std::wstring& directory, const std::wstring& status) {
    FILE* file = nullptr;
    if (_wfopen_s(&file, (directory + L"\\shot.done").c_str(), L"w") == 0 && file) {
        fwprintf(file, L"%s\n", status.c_str());
        fclose(file);
    }
}

/// A shortcut to `target`, carrying `app_id` as its AppUserModelID unless empty, as an installer would make it.
bool make_shortcut(const std::wstring& link, const std::wstring& target, const wchar_t* app_id) {
    IShellLinkW* shortcut = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shortcut)))) return false;
    shortcut->SetPath(target.c_str());
    IPropertyStore* properties = nullptr;
    if (*app_id && SUCCEEDED(shortcut->QueryInterface(IID_PPV_ARGS(&properties)))) {
        PROPVARIANT value;
        if (SUCCEEDED(InitPropVariantFromString(app_id, &value))) {
            properties->SetValue(PKEY_AppUserModel_ID, value);
            properties->Commit();
            PropVariantClear(&value);
        }
        properties->Release();
    }
    IPersistFile* file = nullptr;
    const bool saved = SUCCEEDED(shortcut->QueryInterface(IID_PPV_ARGS(&file))) && SUCCEEDED(file->Save(link.c_str(), TRUE));
    if (file) file->Release();
    shortcut->Release();
    return saved;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    std::wstring shortcut;
    if (argc >= 3 && std::wstring(argv[1]) == L"--shortcut") {
        shortcut = argv[2];
        argc -= 2;
        argv += 2;
    }
    if (argc < 4) return 2;
    const std::wstring desktop_name = argv[1];
    const std::wstring app = argv[2];
    const std::wstring directory = argv[3];
    std::wstring link = directory + L"\\start.lnk";
    if (!shortcut.empty()) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (!make_shortcut(link, app, shortcut == L"app-id" ? L"MusicUnlock.App" : L"")) {
            finish(directory, L"shortcut failed");
            return 1;
        }
    }
    HDESK desktop = CreateDesktopW(desktop_name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
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
    SetEnvironmentVariableW(L"MUSICUNLOCK_TASKBAR_LOG", (directory + L"\\taskbar.log").c_str());
    SetEnvironmentVariableW(L"MUSICUNLOCK_LAUNCHER_PATH", nullptr);  // only the single exe sets it
    SetEnvironmentVariableW(L"MUSICUNLOCK_RENDER_CONVERT", argc >= 5 ? argv[4] : nullptr);
    // Motion capture (screenshot.py --motion) names its scenarios here; empty keeps the static previews.
    // It is always the last argument, so it is never mistaken for a file to convert.
    if (argc >= 6 && std::wstring(argv[argc - 2]) == L"--motion") {
        SetEnvironmentVariableW(L"MUSICUNLOCK_RENDER_MOTION", argv[argc - 1]);
        argc -= 2;
    }
    // Keep anything the app saves inside the output folder, never in the profile.
    SetEnvironmentVariableW(L"MUSICUNLOCK_DATA_DIR", (directory + L"\\appdata").c_str());
    // Same for the single exe's extracted runtime, which stays between runs (warm starts).
    const std::wstring build = directory.substr(0, directory.find_last_of(L'\\'));
    SetEnvironmentVariableW(L"MUSICUNLOCK_RUNTIME_DIR", (build + L"\\runtime").c_str());
    SetEnvironmentVariableW(L"MUSICUNLOCK_LAUNCHER_LOG", (directory + L"\\launcher.log").c_str());
    std::wstring startup_desktop = L"WinSta0\\" + desktop_name;
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    startup.lpDesktop = startup_desktop.data();
    if (!shortcut.empty()) {
        startup.dwFlags |= STARTF_TITLEISLINKNAME;
        startup.lpTitle = link.data();
    }
    PROCESS_INFORMATION process{};
    std::wstring command = L"\"" + app + L"\"";
    for (int i = 5; i < argc; ++i) command += L" \"" + std::wstring(argv[i]) + L"\"";
    const std::wstring folder = app.substr(0, app.find_last_of(L'\\'));
    std::wstring status;
    if (CreateProcessW(app.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, folder.c_str(),
                       &startup, &process)) {
        AssignProcessToJobObject(job, process.hProcess);
        ResumeThread(process.hThread);
        const DWORD timeout = GetEnvironmentVariableW(L"MUSICUNLOCK_RENDER_MOTION", nullptr, 0) ? 300'000 : 120'000;
        if (WaitForSingleObject(process.hProcess, timeout) == WAIT_TIMEOUT) {
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
