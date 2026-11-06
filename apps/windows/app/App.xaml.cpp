#include "pch.h"

#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "Taskbar.h"

#include <cstdio>

namespace winrt::MusicUnlock::implementation {

/// Development diagnostics: when MUSICUNLOCK_LOG names a file, unhandled errors are appended to it.
void log_error(std::wstring_view where, hresult code, std::wstring_view message) {
    wchar_t path[MAX_PATH];
    if (!GetEnvironmentVariableW(L"MUSICUNLOCK_LOG", path, MAX_PATH)) return;
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
