#pragma once

#include "MainWindow.g.h"

#include "UnlockView.xaml.h"

namespace winrt::MusicUnlock::implementation {

struct MainWindow : MainWindowT<MainWindow> {
    MainWindow() = default;
    void InitializeComponent();

    void OnWorkspaceChanged(Microsoft::UI::Xaml::Controls::SelectorBar const&,
                            Microsoft::UI::Xaml::Controls::SelectorBarSelectionChangedEventArgs const&);
    void OnAddFiles(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnAddFolder(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnAcceleratorInvoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const& accelerator,
                              Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);

private:
    UnlockView& Audio() { return *get_self<UnlockView>(AudioView()); }
    void ShowWorkspace(bool audio);

    /// Development renders (MUSICUNLOCK_RENDER_DIR): PNGs of the window content in light and dark, then exit.
    fire_and_forget RenderPreviews(std::wstring directory);
};

}  // namespace winrt::MusicUnlock::implementation

namespace winrt::MusicUnlock::factory_implementation {

struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};

}  // namespace winrt::MusicUnlock::factory_implementation
