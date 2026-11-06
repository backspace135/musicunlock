#pragma once

#include "MainWindow.g.h"

namespace winrt::MusicUnlock::implementation {

struct MainWindow : MainWindowT<MainWindow> {
    MainWindow();

private:
    /// Development renders (MUSICUNLOCK_RENDER_DIR): light and dark PNGs of the window content, then exit.
    fire_and_forget RenderPreviews(std::wstring directory);
};

}  // namespace winrt::MusicUnlock::implementation

namespace winrt::MusicUnlock::factory_implementation {

struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};

}  // namespace winrt::MusicUnlock::factory_implementation
