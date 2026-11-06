#include "pch.h"

#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "Settings.h"
#include "Taskbar.h"

#include <cstdio>
#include <filesystem>

namespace {

/// MUSICUNLOCK_LOG, or <data dir>\crash.log for crashes; set before the window opens.
wchar_t crash_log[MAX_PATH] = {};

/// "module+0xoffset" for a code address, so a crash line can be matched with the PDB.
void describe(FILE* file, const void* address) {
    HMODULE module = nullptr;
    wchar_t name[MAX_PATH] = L"?";
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCWSTR>(address), &module) &&
        GetModuleFileNameW(module, name, MAX_PATH)) {
        fwprintf(file, L"%s+0x%llX", wcsrchr(name, L'\\') ? wcsrchr(name, L'\\') + 1 : name,
                 static_cast<unsigned long long>(static_cast<const char*>(address) - reinterpret_cast<const char*>(module)));
    } else {
        fwprintf(file, L"%p", address);
    }
}

/// Last chance for a crash the XAML UnhandledException handler never sees (access violations, exceptions in
/// callbacks outside XAML): the code, where it happened and the stack, then the default handling.
LONG WINAPI log_crash(EXCEPTION_POINTERS* info) {
    FILE* file = nullptr;
    if (crash_log[0] && _wfopen_s(&file, crash_log, L"a, ccs=UTF-8") == 0 && file) {
        SYSTEMTIME now;
        GetSystemTime(&now);
        const auto& record = *info->ExceptionRecord;
        fwprintf(file, L"crash: %04u-%02u-%02uT%02u:%02u:%02uZ exception 0x%08X at ", now.wYear, now.wMonth, now.wDay,
                 now.wHour, now.wMinute, now.wSecond, static_cast<unsigned>(record.ExceptionCode));
        describe(file, record.ExceptionAddress);
        if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2) {
            fwprintf(file, L" (%s 0x%llX)", record.ExceptionInformation[0] == 1 ? L"write" : L"read",
                     static_cast<unsigned long long>(record.ExceptionInformation[1]));
        }
        fwprintf(file, L"\n");
        void* frames[48];
        const USHORT count = RtlCaptureStackBackTrace(0, 48, frames, nullptr);
        for (USHORT i = 0; i < count; ++i) {
            fwprintf(file, L"  ");
            describe(file, frames[i]);
            fwprintf(file, L"\n");
        }
        fclose(file);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

namespace winrt::MusicUnlock::implementation {

/// Diagnostics: when MUSICUNLOCK_LOG names a file, unhandled errors are appended to it; without it, only the
/// errors that end the app go to <data dir>\crash.log.
void log_error(std::wstring_view where, hresult code, std::wstring_view message) {
    wchar_t path[MAX_PATH];
    if (!GetEnvironmentVariableW(L"MUSICUNLOCK_LOG", path, MAX_PATH)) {
        if (where != L"unhandled" && where != L"launch" && where != L"start") return;
        wcscpy_s(path, crash_log);
    }
    if (!path[0]) return;
    FILE* file = nullptr;
    if (_wfopen_s(&file, path, L"a, ccs=UTF-8") != 0 || !file) return;
    fwprintf(file, L"%.*s: 0x%08X %.*s\n", static_cast<int>(where.size()), where.data(),
             static_cast<unsigned>(code.value), static_cast<int>(message.size()), message.data());
    fclose(file);
}

App::App() {
    UnhandledException([](IInspectable const&, Microsoft::UI::Xaml::UnhandledExceptionEventArgs const& e) {
        log_error(L"unhandled", e.Exception(), e.Message());
#if defined _DEBUG
        if (IsDebuggerPresent()) __debugbreak();
#endif
    });
}

void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&) {
    try {
        window_ = make<MainWindow>();
        window_.Activate();
    } catch (hresult_error const& error) {
        log_error(L"launch", error.code(), error.message());
        throw;
    }
}

}  // namespace winrt::MusicUnlock::implementation

// Own entry point (DISABLE_XAML_GENERATED_MAIN) so start-up failures are logged instead of only crashing.
int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    if (!GetEnvironmentVariableW(L"MUSICUNLOCK_LOG", crash_log, MAX_PATH)) {
        std::error_code ignored;
        const auto folder = settings::data_directory();
        std::filesystem::create_directories(folder, ignored);
        wcsncpy_s(crash_log, (folder / L"crash.log").c_str(), _TRUNCATE);
    }
    SetUnhandledExceptionFilter(log_crash);
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        taskbar::claim_process();
        winrt::Microsoft::UI::Xaml::Application::Start(
            [](auto&&) { winrt::make<winrt::MusicUnlock::implementation::App>(); });
    } catch (winrt::hresult_error const& error) {
        winrt::MusicUnlock::implementation::log_error(L"start", error.code(), error.message());
        return 1;
    }
    return 0;
}
