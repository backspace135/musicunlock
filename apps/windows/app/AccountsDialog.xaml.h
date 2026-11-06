#pragma once

#include "AccountsDialog.g.h"

#include <string>

#include <winrt/Microsoft.Web.WebView2.Core.h>

#include "musicunlock/music_api.h"
#include "musicunlock/qr_login.h"

namespace winrt::MusicUnlock::implementation {

struct AccountsDialog : AccountsDialogT<AccountsDialog> {
    AccountsDialog() = default;
    void InitializeComponent();

    /// Shows the provider's tab: its saved session, or a login (QR code or official page).
    void Select(mu::MusicProvider provider);

    /// Preview renders: fixed states with sample data; no network, saved sessions or login pages.
    enum class Preview { qr_waiting, qr_scanned, qr_expired, qr_confirmed, qr_refused, official_page, signed_in };
    void ShowPreview(Preview state);
    /// Preview renders: starts WebView2 on a blank page with the provider's data folder and describes the
    /// result for the log, so renders check that the runtime loads from the app folder.
    Windows::Foundation::IAsyncOperation<hstring> CheckWebView(mu::MusicProvider provider);
    /// Login renders: leaves the preview states for the real ones. The official page is a blank page instead of
    /// the platform's, and with `fake_qq_login` it gets fixed fake QQ Music cookies, so the login goes through the
    /// same cookie reading, saving and page teardown as a real one without network or a real account.
    void Live(bool fake_qq_login);
    /// True while the saved-session panel is shown (login renders).
    bool ShowsSession() { return SessionPanel().Visibility() == Microsoft::UI::Xaml::Visibility::Visible; }

    void OnProviderChanged(Microsoft::UI::Xaml::Controls::SelectorBar const&,
                           Microsoft::UI::Xaml::Controls::SelectorBarSelectionChangedEventArgs const&);
    void OnRefreshQr(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { StartQr(); }
    void OnOfficialPage(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnNativeQr(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnReloadPage(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { StartWeb(); }
    void OnRelogin(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnLogout(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    mu::MusicProvider Provider();
    void FitToWindow();
    void Refresh();
    void UpdateFooter();
    void ShowSession(mu::MusicProvider provider);
    void ShowQrState(mu::QrLoginState state, std::string_view message);
    void SetQrCode(std::string_view text);
    void ShowWebStatus(bool loading, hstring const& text);
    fire_and_forget StartQr();
    fire_and_forget StartWeb();
    fire_and_forget CaptureCookies();
    fire_and_forget LoadAccountName(mu::MusicProvider provider);
    /// Configures a fresh login page: https only, no downloads, permissions, dev tools or host objects.
    void Restrict(Microsoft::Web::WebView2::Core::CoreWebView2 const& core);
    /// Cancels QR polling and closes the login page (also when the dialog closes).
    void StopLogin();

    mu::MusicApi api_{mu::make_system_transport()};
    mu::CancelToken qr_cancel_;
    Microsoft::UI::Xaml::Controls::WebView2 web_{nullptr};
    mu::MusicProvider web_provider_ = mu::MusicProvider::netease;
    Microsoft::UI::Dispatching::DispatcherQueueTimer cookie_timer_{nullptr};
    Microsoft::UI::Xaml::XamlRoot::Changed_revoker root_changed_;
    uint64_t generation_ = 0;  // bumped whenever the login shown changes; late async results compare it
    bool official_ = false;    // NetEase on its official page instead of the QR code
    bool relogin_ = false;     // "重新登录": show a login although a session is saved
    bool session_seen_ = false;
    bool capturing_ = false;
    bool preview_ = false;
    bool live_render_ = false;  // Live(): blank official page
    bool fake_login_ = false;   // Live(true): fake cookies for the next official page
    std::string message_;  // last login / logout result, shown in the footer
};

}  // namespace winrt::MusicUnlock::implementation

namespace winrt::MusicUnlock::factory_implementation {

struct AccountsDialog : AccountsDialogT<AccountsDialog, implementation::AccountsDialog> {};

}  // namespace winrt::MusicUnlock::factory_implementation
