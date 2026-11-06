#pragma once

#include "MainWindow.g.h"

#include <filesystem>

#include "LyricsView.xaml.h"
#include "UnlockView.xaml.h"
#include "musicunlock/lyrics.h"

namespace winrt::MusicUnlock::implementation {

struct MainWindow : MainWindowT<MainWindow> {
    MainWindow() = default;
    void InitializeComponent();

    void OnWorkspaceChanged(Microsoft::UI::Xaml::Controls::SelectorBar const&,
                            Microsoft::UI::Xaml::Controls::SelectorBarSelectionChangedEventArgs const&);
    void OnAddFiles(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnAddFolder(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnOpenLyrics(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnAccounts(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { ShowAccounts(mu::MusicProvider::netease); }
    void OnAcceleratorInvoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const& accelerator,
                              Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);

    /// 账号登录 on the provider's tab (the title bar button, or a lyrics request that needs a login).
    fire_and_forget ShowAccounts(mu::MusicProvider provider);

private:
    UnlockView& Audio() { return *get_self<UnlockView>(AudioView()); }
    LyricsView& Lyrics() { return *get_self<LyricsView>(LyricsWorkspace()); }
    void ShowWorkspace(bool audio);
    void UseBackdrop();
    void ApplyMotion();
    /// Asks before closing while a batch runs; finished files stay either way.
    fire_and_forget ConfirmClose();
    Microsoft::UI::Xaml::Controls::ContentDialog CloseDialog();

    /// Title bar glyph: outline person when no platform is signed in, filled when one is.
    void UpdateAccountsButton();

    bool closing_confirmed_ = false;
    bool accounts_open_ = false;

    /// Development renders (MUSICUNLOCK_RENDER_DIR): PNGs of the window content in light and dark, with
    /// dialogs, menus, large text and high contrast, and the UI Automation tree; then exit.
    fire_and_forget RenderPreviews(std::wstring directory);
    /// The account dialog's states over the lyrics workspace in light, dark and simulated high contrast.
    Windows::Foundation::IAsyncAction RenderAccountPreviews(std::filesystem::path directory);
    /// One PNG of the window content, with open dialogs drawn over it.
    Windows::Foundation::IAsyncAction SavePreview(std::filesystem::path file);
};

}  // namespace winrt::MusicUnlock::implementation

namespace winrt::MusicUnlock::factory_implementation {

struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};

}  // namespace winrt::MusicUnlock::factory_implementation
