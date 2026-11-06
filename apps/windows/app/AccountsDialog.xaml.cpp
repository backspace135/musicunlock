#include "pch.h"

#include "AccountsDialog.xaml.h"
#if __has_include("AccountsDialog.g.cpp")
#include "AccountsDialog.g.cpp"
#endif

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <thread>
#include <utility>

#include <winrt/Windows.UI.Xaml.Interop.h>
#include <wil/cppwinrt_helpers.h>

#include "Accounts.h"

namespace winrt::MusicUnlock::implementation {

using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::Web::WebView2::Core;
using namespace std::chrono_literals;
using mu::MusicProvider;
using mu::QrLoginState;

namespace {

Visibility shown(bool visible) { return visible ? Visibility::Visible : Visibility::Collapsed; }

hstring text(std::string_view utf8) { return to_hstring(utf8); }

hstring login_page(MusicProvider provider) {
    return provider == MusicProvider::netease ? L"https://music.163.com/#/login" : L"https://y.qq.com/portal/profile.html";
}

mu::Cookie to_cookie(CoreWebView2Cookie const& cookie) {
    mu::Cookie result{to_string(cookie.Name()), to_string(cookie.Value()), to_string(cookie.Domain()),
                      to_string(cookie.Path()), std::nullopt, cookie.IsSecure()};
    if (result.path.empty()) result.path = "/";
    if (!cookie.IsSession()) result.expires = static_cast<std::int64_t>(cookie.Expires());
    return result;
}

/// Segoe Fluent Icons glyph for each QR state, shown beside the status text.
const wchar_t* qr_glyph(QrLoginState state) {
    switch (state) {
        case QrLoginState::loading: return L"\uE895";  // Sync
        case QrLoginState::waiting: return L"\uED14";  // QRCode
        case QrLoginState::scanned: return L"\uE8EA";  // CellPhone
        case QrLoginState::confirmed: return L"\uE73E";  // CheckMark
        case QrLoginState::expired: return L"\uE72C";  // Refresh
        case QrLoginState::failed: break;
    }
    return L"\uE7BA";  // Warning
}

// Same shape as a real code: key plus chainId (see MusicApi::create_qr_code).
constexpr const char* sample_qr_url =
    "https://music.163.com/login?codekey=00000000-0000-4000-8000-000000000000"
    "&chainId=v1_0123456789ABCDEF0123456789ABCDEF_pc_login_1700000000000";

}  // namespace

void AccountsDialog::InitializeComponent() {
    AccountsDialogT::InitializeComponent();
    preview_ = GetEnvironmentVariableW(L"MUSICUNLOCK_RENDER_DIR", nullptr, 0) != 0;
    // Closing the dialog ends the QR polling and the login page.
    Closing([this](auto&&, auto&&) {
        StopLogin();
        root_changed_.revoke();
    });
    Opened([this](auto&&, auto&&) {
        FitToWindow();
        root_changed_ = XamlRoot().Changed(auto_revoke, [this](auto&&, auto&&) { FitToWindow(); });
        Refresh();
    });
}

void AccountsDialog::FitToWindow() {
    // Title, tabs, footer and the dialog's button bar take about 310 px of the window height.
    if (const auto root = XamlRoot()) ContentArea().Height(std::clamp(root.Size().Height - 310.0, 200.0, 430.0));
}

MusicProvider AccountsDialog::Provider() {
    return ProviderBar().SelectedItem() == QQItem() ? MusicProvider::qq : MusicProvider::netease;
}

void AccountsDialog::Select(MusicProvider provider) {
    ProviderBar().SelectedItem(provider == MusicProvider::qq ? QQItem() : NeteaseItem());
}

void AccountsDialog::OnProviderChanged(SelectorBar const&, SelectorBarSelectionChangedEventArgs const&) {
    message_.clear();
    official_ = relogin_ = false;
    if (IsLoaded()) Refresh();
}

void AccountsDialog::Refresh() {
    if (preview_) return;
    const auto provider = Provider();
    if (accounts::signed_in(provider) && !relogin_) {
        StopLogin();
        ShowSession(provider);
        LoadAccountName(provider);
    } else if (provider == MusicProvider::netease && !official_) {
        StartQr();
    } else {
        StartWeb();
    }
    UpdateFooter();
}

void AccountsDialog::UpdateFooter() {
    const bool web = WebPanel().Visibility() == Visibility::Visible;
    ReloadPageButton().Visibility(shown(web));
    NativeQrButton().Visibility(shown(web && Provider() == MusicProvider::netease));
    if (!message_.empty()) {
        FooterIcon().Glyph(L"\uE946");  // Info
        FooterText().Text(text(message_));
    } else if (web) {
        FooterIcon().Glyph(L"\uE946");
        FooterText().Text(L"完成扫码或账号登录后会自动保存会话。若没有反应，请点击刷新页面。");
    } else {
        FooterIcon().Glyph(L"\uE72E");  // Lock
        FooterText().Text(L"登录信息用 Windows 数据保护加密，只保存在这台电脑上。");
    }
}

void AccountsDialog::ShowSession(MusicProvider provider) {
    SessionPanel().Visibility(Visibility::Visible);
    QrPanel().Visibility(Visibility::Collapsed);
    WebPanel().Visibility(Visibility::Collapsed);
    const std::string title(mu::provider_title(provider));
    SessionTitle().Text(text(title + "：会话已保存"));
    AccountText().Text(L"正在读取账号信息…");
    LogoutButton().Content(box_value(text("退出" + title)));
}

fire_and_forget AccountsDialog::LoadAccountName(MusicProvider provider) {
    auto strong = get_strong();
    const auto generation = generation_;
    auto dispatcher = DispatcherQueue();
    const std::string cookie = accounts::cookie_header(provider);
    co_await resume_background();
    std::string line;
    try {
        const auto name = api_.account_name(provider, cookie);
        line = name ? "账号：" + *name : "平台没有返回账号信息，会话可能已过期，可以重新登录。";
    } catch (const mu::LyricError& error) {
        line = std::string("无法读取账号信息：") + error.what();
    } catch (...) {
        line = "暂时无法读取账号信息，请检查网络。会话仍已保存。";
    }
    co_await wil::resume_foreground(dispatcher);
    if (generation == generation_ && SessionPanel().Visibility() == Visibility::Visible) AccountText().Text(text(line));
}

void AccountsDialog::SetQrCode(std::string_view url) {
    const auto data = mu::qr_path_data(mu::qr_matrix(url));
    QrPath().Data(Markup::XamlBindingHelper::ConvertValue(xaml_typename<Media::Geometry>(), box_value(text(data)))
                      .as<Media::Geometry>());
}

void AccountsDialog::ShowQrState(QrLoginState state, std::string_view message) {
    SessionPanel().Visibility(Visibility::Collapsed);
    WebPanel().Visibility(Visibility::Collapsed);
    QrPanel().Visibility(Visibility::Visible);
    const bool loading = state == QrLoginState::loading;
    QrProgress().IsActive(loading);
    QrPath().Visibility(shown(!loading));
    const bool covered = state != QrLoginState::loading && state != QrLoginState::waiting;
    QrCover().Visibility(shown(covered));
    QrCoverIcon().Visibility(shown(covered));
    QrCoverIcon().Glyph(qr_glyph(state));
    QrStatusIcon().Glyph(qr_glyph(state));
    QrStatusText().Text(text(message.empty() ? mu::qr_login_message(state) : message));
    const bool ended = state == QrLoginState::expired || state == QrLoginState::failed;
    RefreshQrButton().Content(box_value(ended ? L"重新生成二维码" : L"刷新二维码"));
    RefreshQrButton().IsEnabled(!loading && state != QrLoginState::confirmed);
}

fire_and_forget AccountsDialog::StartQr() {
    auto strong = get_strong();
    StopLogin();
    const mu::CancelToken cancel;
    qr_cancel_ = cancel;
    auto dispatcher = DispatcherQueue();
    ShowQrState(QrLoginState::loading, {});
    UpdateFooter();
    co_await resume_background();
    std::optional<mu::QrTicket> ticket;
    std::string error;
    try {
        ticket = api_.create_qr_code();
    } catch (const mu::LyricError& failure) {
        error = failure.what();
    } catch (...) {
        error = "无法连接网易云音乐，请检查网络后刷新二维码。";
    }
    co_await wil::resume_foreground(dispatcher);
    if (cancel.cancelled()) co_return;
    if (!ticket) {
        ShowQrState(QrLoginState::failed, error);
        co_return;
    }
    SetQrCode(ticket->url);
    ShowQrState(QrLoginState::waiting, {});
    co_await resume_background();
    mu::QrLoginStep result;
    try {
        result = mu::poll_qr_login(
            api_, *ticket, cancel,
            [cancel] {
                for (int slice = 0; slice < 20; ++slice) {
                    cancel.check();
                    std::this_thread::sleep_for(100ms);
                }
            },
            [strong, dispatcher, cancel](const mu::QrLoginStep& step) {
                dispatcher.TryEnqueue([strong, cancel, step] {
                    if (!cancel.cancelled()) strong->ShowQrState(step.state, step.message);
                });
            });
    } catch (const mu::Cancelled&) {
        co_return;
    }
    co_await wil::resume_foreground(dispatcher);
    if (cancel.cancelled()) co_return;
    ShowQrState(result.state, result.message);
    if (result.state != QrLoginState::confirmed) co_return;
    message_ = accounts::accept(MusicProvider::netease, result.session)
                   .value_or("平台没有返回有效登录，请使用官方登录页重试。");
    relogin_ = false;
    UpdateFooter();
    // Let the confirmation register before switching to the saved session.
    co_await resume_after(800ms);
    co_await wil::resume_foreground(dispatcher);
    if (!cancel.cancelled()) Refresh();
}

void AccountsDialog::ShowWebStatus(bool loading, hstring const& status) {
    WebProgress().IsActive(loading);
    WebProgress().Visibility(shown(loading));
    WebStatusIcon().Visibility(shown(!loading));
    WebStatusText().Text(status);
    WebStatus().Visibility(Visibility::Visible);
}

void AccountsDialog::Restrict(CoreWebView2 const& core) {
    auto settings = core.Settings();
    settings.AreDevToolsEnabled(false);
    settings.AreHostObjectsAllowed(false);
    settings.IsWebMessageEnabled(false);
    settings.IsStatusBarEnabled(false);
    settings.IsPasswordAutosaveEnabled(false);
    settings.IsGeneralAutofillEnabled(false);
    // No custom-scheme launching, file navigation or downloads (macOS decidePolicyFor).
    core.NavigationStarting([](auto&&, CoreWebView2NavigationStartingEventArgs const& args) {
        const std::wstring_view uri = args.Uri();
        if (!uri.starts_with(L"https://") && !uri.starts_with(L"about:")) args.Cancel(true);
    });
    core.NewWindowRequested([](CoreWebView2 const& sender, CoreWebView2NewWindowRequestedEventArgs const& args) {
        args.Handled(true);
        if (std::wstring_view(args.Uri()).starts_with(L"https://")) sender.Navigate(args.Uri());
    });
    core.DownloadStarting([](auto&&, CoreWebView2DownloadStartingEventArgs const& args) { args.Cancel(true); });
    core.PermissionRequested([](auto&&, CoreWebView2PermissionRequestedEventArgs const& args) {
        args.State(CoreWebView2PermissionState::Deny);
    });
}

fire_and_forget AccountsDialog::StartWeb() {
    auto strong = get_strong();
    StopLogin();
    const auto generation = generation_;
    const auto provider = Provider();
    SessionPanel().Visibility(Visibility::Collapsed);
    QrPanel().Visibility(Visibility::Collapsed);
    WebPanel().Visibility(Visibility::Visible);
    ShowWebStatus(true, L"正在打开官方登录页…");
    UpdateFooter();
    if (preview_) co_return;
    if (!accounts::clear_pending_webview_data(provider)) {
        ShowWebStatus(false, L"上次退出登录的浏览数据仍在清除，请稍后点击刷新页面。");
        co_return;
    }
    WebView2 web;
    web_ = web;
    web_provider_ = provider;
    web.Visibility(Visibility::Collapsed);
    WebHost().Child(web);
    hstring failure;
    try {
        const auto folder = accounts::webview_folder(provider);
        std::error_code ignored;
        std::filesystem::create_directories(folder, ignored);
        const auto environment = co_await CoreWebView2Environment::CreateWithOptionsAsync(L"", folder.wstring(), nullptr);
        co_await web.EnsureCoreWebView2Async(environment);
    } catch (hresult_error const& error) {
        failure = error.message();
    }
    if (generation != generation_) co_return;  // closed or moved on meanwhile
    if (!failure.empty()) {
        ShowWebStatus(false, L"无法启动 Microsoft Edge WebView2 运行时（" + failure +
                                 L"）。请安装或修复 WebView2 运行时后点击刷新页面，网易云音乐也可以改用原生扫码。");
        co_return;
    }
    const auto core = web.CoreWebView2();
    Restrict(core);
    core.NavigationCompleted([weak = get_weak(), generation](auto&&, CoreWebView2NavigationCompletedEventArgs const& args) {
        auto self = weak.get();
        if (!self || generation != self->generation_) return;
        if (args.IsSuccess()) {
            self->WebStatus().Visibility(Visibility::Collapsed);
            self->web_.Visibility(Visibility::Visible);
            self->CaptureCookies();
        } else if (self->web_.Visibility() == Visibility::Collapsed &&
                   args.WebErrorStatus() != CoreWebView2WebErrorStatus::OperationCanceled) {
            self->ShowWebStatus(false, L"无法打开官方登录页，请检查网络后点击刷新页面。");
        }
    });
    if (relogin_) {
        // A requested re-login must not immediately re-accept the saved music key. Keep the account site's
        // own sign-in cookies, but require the platform to issue a fresh music session.
        const auto keys = provider == MusicProvider::netease
                              ? std::vector<std::string_view>{"MUSIC_U"}
                              : std::vector<std::string_view>{"qqmusic_key", "qm_keyst", "qqmusic_session"};
        auto manager = core.CookieManager();
        for (auto const& cookie : co_await manager.GetCookiesAsync(L"")) {
            const auto name = to_string(cookie.Name());
            if (mu::provider_owns(provider, to_string(cookie.Domain())) &&
                std::find(keys.begin(), keys.end(), name) != keys.end()) {
                manager.DeleteCookie(cookie);
            }
        }
        if (generation != generation_) co_return;
    }
    if (live_render_) {
        if (std::exchange(fake_login_, false) && provider == MusicProvider::qq) {
            auto manager = core.CookieManager();
            for (const auto [name, value] : {std::pair{L"qqmusic_key", L"render-test-key"}, std::pair{L"uin", L"10000"}}) {
                manager.AddOrUpdateCookie(manager.CreateCookie(name, value, L".qq.com", L"/"));
            }
        }
        core.Navigate(L"about:blank");
    } else {
        core.Navigate(login_page(provider));
    }
    // WebView2 has no cookie-change event: read the jar every second while the page is open.
    cookie_timer_ = DispatcherQueue().CreateTimer();
    cookie_timer_.Interval(1s);
    cookie_timer_.Tick([weak = get_weak()](auto&&, auto&&) {
        if (auto self = weak.get()) self->CaptureCookies();
    });
    cookie_timer_.Start();
}

fire_and_forget AccountsDialog::CaptureCookies() {
    if (!web_ || capturing_ || !web_.CoreWebView2()) co_return;
    auto strong = get_strong();
    capturing_ = true;
    const auto generation = generation_;
    const auto provider = web_provider_;
    std::vector<mu::Cookie> cookies;
    try {
        for (auto const& cookie : co_await web_.CoreWebView2().CookieManager().GetCookiesAsync(L"")) {
            cookies.push_back(to_cookie(cookie));
        }
    } catch (hresult_error const&) {
    }
    capturing_ = false;
    if (generation != generation_) co_return;
    if (!mu::has_music_session(cookies, provider, mu::unix_now())) {
        session_seen_ = false;
        co_return;
    }
    // Login redirects often set the key and the account id in separate responses: accept on the next reading.
    if (!session_seen_) {
        session_seen_ = true;
        co_return;
    }
    const auto message = accounts::accept(provider, cookies);
    if (!message) co_return;
    message_ = *message;
    relogin_ = official_ = false;
    // This still runs inside WebView2's GetCookiesAsync completion. Showing the session closes the login page,
    // and closing a WebView2 from its own callback makes it crash afterwards (#21): leave the callback first.
    co_await wil::resume_foreground(DispatcherQueue());
    if (generation == generation_) Refresh();
}

void AccountsDialog::StopLogin() {
    ++generation_;
    qr_cancel_.cancel();
    session_seen_ = false;
    if (cookie_timer_) {
        cookie_timer_.Stop();
        cookie_timer_ = nullptr;
    }
    if (web_) {
        WebHost().Child(nullptr);
        web_.Close();
        web_ = nullptr;
    }
}

void AccountsDialog::OnOfficialPage(IInspectable const&, RoutedEventArgs const&) {
    official_ = true;
    Refresh();
}

void AccountsDialog::OnNativeQr(IInspectable const&, RoutedEventArgs const&) {
    official_ = false;
    Refresh();
}

void AccountsDialog::OnRelogin(IInspectable const&, RoutedEventArgs const&) {
    message_.clear();
    relogin_ = true;
    Refresh();
}

void AccountsDialog::OnLogout(IInspectable const&, RoutedEventArgs const&) {
    StopLogin();
    message_ = accounts::logout(Provider());
    relogin_ = official_ = false;
    Refresh();
}

void AccountsDialog::ShowPreview(Preview state) {
    preview_ = true;
    StopLogin();
    message_.clear();
    official_ = relogin_ = false;
    const bool qq = state == Preview::official_page;
    Select(qq ? MusicProvider::qq : MusicProvider::netease);
    switch (state) {
        case Preview::qr_waiting:
        case Preview::qr_scanned:
        case Preview::qr_expired:
        case Preview::qr_confirmed: {
            SetQrCode(sample_qr_url);
            const auto qr = state == Preview::qr_waiting   ? QrLoginState::waiting
                            : state == Preview::qr_scanned ? QrLoginState::scanned
                            : state == Preview::qr_expired ? QrLoginState::expired
                                                           : QrLoginState::confirmed;
            ShowQrState(qr, {});
            if (qr == QrLoginState::confirmed) message_ = "网易云音乐登录成功，会话已加密保存在这台电脑上。";
            break;
        }
        case Preview::qr_refused:
            SetQrCode(sample_qr_url);
            ShowQrState(QrLoginState::failed, mu::qr_refusal_message(8821, "请切换其他登录方式或升级新版本再试"));
            break;
        case Preview::official_page: StartWeb(); break;
        case Preview::signed_in:
            ShowSession(MusicProvider::netease);
            AccountText().Text(L"账号：示例用户");
            break;
    }
    UpdateFooter();
}

void AccountsDialog::Live(bool fake_qq_login) {
    preview_ = false;
    live_render_ = true;
    fake_login_ = fake_qq_login;
    message_.clear();
    official_ = relogin_ = false;
    Refresh();
}

Windows::Foundation::IAsyncOperation<hstring> AccountsDialog::CheckWebView(MusicProvider provider) {
    auto strong = get_strong();
    WebView2 web;
    web.Width(1);
    web.Height(1);
    WebHost().Child(web);
    hstring result;
    try {
        const auto folder = accounts::webview_folder(provider);
        std::error_code ignored;
        std::filesystem::create_directories(folder, ignored);
        const auto environment = co_await CoreWebView2Environment::CreateWithOptionsAsync(L"", folder.wstring(), nullptr);
        co_await web.EnsureCoreWebView2Async(environment);
        result = L"WebView2 " + environment.BrowserVersionString() + L" ready, user data " + hstring(folder.wstring());
    } catch (hresult_error const& error) {
        result = L"WebView2 failed: " + error.message();
    }
    WebHost().Child(nullptr);
    web.Close();
    co_return result;
}

}  // namespace winrt::MusicUnlock::implementation
