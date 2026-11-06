#pragma once

#include "MainWindow.g.h"

namespace winrt::MusicUnlock::implementation {

struct MainWindow : MainWindowT<MainWindow> {
    MainWindow();
};

}  // namespace winrt::MusicUnlock::implementation

namespace winrt::MusicUnlock::factory_implementation {

struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};

}  // namespace winrt::MusicUnlock::factory_implementation
