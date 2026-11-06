#pragma once

#include "MainWindow.g.h"

#include "UnlockView.xaml.h"

namespace winrt::MusicUnlock::implementation {

struct MainWindow : MainWindowT<MainWindow> {
    MainWindow() = default;
    void InitializeComponent();

    void OnWorkspaceChanged(Microsoft::UI::Xaml::Controls::SelectorBar const&,
                            Microsoft::UI::Xaml::Controls::SelectorBarSelectionChangedEventArgs const&);

private:
    /// Development renders (MUSICUNLOCK_RENDER_DIR): PNGs of the window content in light and dark, then exit.
    fire_and_forget RenderPreviews(std::wstring directory);
};

}  // namespace winrt::MusicUnlock::implementation

namespace winrt::MusicUnlock::factory_implementation {

struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};

}  // namespace winrt::MusicUnlock::factory_implementation
