#include "pch.h"

#include "MainWindow.xaml.h"

#include "Accounts.h"
#include "AccountsDialog.xaml.h"
#include "GlassMotion.h"
#include "Motion.h"
#include "MotionCapture.h"
#include "Preview.h"
#include "Shell.h"
#include "Taskbar.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <vector>

#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#include <wil/cppwinrt_helpers.h>

namespace winrt::MusicUnlock::implementation {

using namespace Microsoft::UI;
using namespace Microsoft::UI::Xaml;
using namespace std::chrono_literals;

namespace {

std::wstring environment(const wchar_t* name) {
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if (length == 0) return {};
    std::wstring value(length, L'\0');
    value.resize(GetEnvironmentVariableW(name, value.data(), length));
    return value;
}

/// Preview renders: Theme.xaml's HighContrast brushes as the element's light and dark theme resources.
void SimulateHighContrast(FrameworkElement const& element) {
    for (auto const& merged : Application::Current().Resources().MergedDictionaries()) {
        const auto themes = merged.ThemeDictionaries();
        const auto key = box_value(L"HighContrast");
        if (!themes.HasKey(key)) continue;
        const auto contrast = themes.Lookup(key).as<ResourceDictionary>();
        if (!contrast.HasKey(box_value(L"MuTextBrush"))) continue;
        for (const wchar_t* theme : {L"Light", L"Dark"}) {
            ResourceDictionary copy;
            for (auto const& entry : contrast) copy.Insert(entry.Key(), entry.Value());
            element.Resources().ThemeDictionaries().Insert(box_value(theme), copy);
        }
    }
}

}  // namespace

/// Appends to MUSICUNLOCK_LOG (App.xaml.cpp).
void log_error(std::wstring_view where, hresult code, std::wstring_view message);

void MainWindow::InitializeComponent() {
    const auto render_directory = environment(L"MUSICUNLOCK_RENDER_DIR");
    const auto render_motion = environment(L"MUSICUNLOCK_RENDER_MOTION");
    // Previews are static like the macOS renderer; the end-to-end render and motion capture keep motion.
    motion::suspend(!render_directory.empty() && environment(L"MUSICUNLOCK_RENDER_CONVERT").empty() && render_motion.empty());
    if (!render_motion.empty()) motion::capture(true);
    MainWindowT::InitializeComponent();
    ExtendsContentIntoTitleBar(true);
    SetTitleBar(AppTitleBar());
    UseBackdrop();
    // AppWindow sizes are physical pixels; DESIGN.md sizes are effective pixels.
    HWND hwnd = nullptr;
    this->try_as<::IWindowNative>()->get_WindowHandle(&hwnd);
    const double scale = GetDpiForWindow(hwnd) / 96.0;
    auto pixels = [scale](int value) { return static_cast<int32_t>(value * scale + 0.5); };
    auto window = AppWindow();
    window.SetIcon(GetIconIdFromIcon(LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1))));
    taskbar::label_window(hwnd, window);
    window.Resize({pixels(1060), pixels(720)});
    if (auto presenter = window.Presenter().try_as<Windowing::OverlappedPresenter>()) {
        presenter.PreferredMinimumWidth(pixels(880));
        presenter.PreferredMinimumHeight(pixels(600));
    }
    // Ctrl+. stops a batch. VK_OEM_PERIOD has no VirtualKey name, so it cannot be written in XAML.
    Xaml::Input::KeyboardAccelerator stop;
    stop.Modifiers(Windows::System::VirtualKeyModifiers::Control);
    stop.Key(static_cast<Windows::System::VirtualKey>(VK_OEM_PERIOD));
    stop.Invoked({this, &MainWindow::OnAcceleratorInvoked});
    Root().KeyboardAccelerators().Append(stop);
    AppWindow().Closing([this](auto&&, Windowing::AppWindowClosingEventArgs const& args) {
        if (closing_confirmed_ || !(Audio().Busy() || Lyrics().Busy())) return;
        args.Cancel(true);
        ConfirmClose();
    });
    Audio().OnFindLyricsRequested([this](std::string title) {
        ShowWorkspace(false);
        Lyrics().SearchForAudio(std::move(title));
    });
    ApplyMotion();
    // "Animation effects" may have been switched in Settings while the window was in the background.
    Activated([this](auto&&, WindowActivatedEventArgs const& args) {
        if (args.WindowActivationState() != WindowActivationState::Deactivated) ApplyMotion();
    });
    Audio().Import(shell::command_line_paths());
    UpdateAccountsButton();
    if (!render_directory.empty()) {
        SolidBackdrop().Visibility(Visibility::Visible);
        if (environment(L"MUSICUNLOCK_RENDER_CONVERT").empty()) Audio().UsePreviewData();
        Lyrics().UsePreviewData();
        Root().Loaded([this, render_directory, render_motion](auto&&, auto&&) {
            if (render_motion.empty()) RenderPreviews(render_directory);
            else RenderMotion(render_directory, render_motion);
        });
    }
}

/// Mica on Windows 11; Windows 10 has no Mica, so desktop acrylic, or a solid workspace colour when neither
/// is available. MUSICUNLOCK_BACKDROP=acrylic or solid forces a fallback for checking it on Windows 11.
void MainWindow::UseBackdrop() {
    using namespace Microsoft::UI::Composition::SystemBackdrops;
    const auto forced = environment(L"MUSICUNLOCK_BACKDROP");
    if (forced.empty() && MicaController::IsSupported()) {
        SystemBackdrop(Media::MicaBackdrop());
    } else if (forced != L"solid" && DesktopAcrylicController::IsSupported()) {
        SystemBackdrop(Media::DesktopAcrylicBackdrop());
    } else {
        SolidBackdrop().Visibility(Visibility::Visible);
    }
}

void MainWindow::ApplyMotion() {
    motion::describe(L"音乐解锁工作区");
    motion::crossfade(AudioView());
    motion::describe(L"歌词工作区");
    motion::crossfade(LyricsWorkspace());
    // The action groups crossfade on the workspace spring, so they keep pace with the switch below them.
    motion::describe(L"音乐解锁操作");
    motion::crossfade(AudioActions(), glass_motion::workspace);
    motion::describe(L"歌词操作");
    motion::crossfade(LyricsActions(), glass_motion::workspace);
    motion::describe({});
    Audio().ApplyMotion();
}

void MainWindow::OnWorkspaceChanged(Controls::SelectorBar const&, Controls::SelectorBarSelectionChangedEventArgs const&) {
    const bool audio = WorkspaceBar().SelectedItem() == AudioItem();
    // The title bar centres the switcher in the space left by the actions; keep their widest width so it stays put.
    HeaderActions().MinWidth(std::max(HeaderActions().MinWidth(), HeaderActions().ActualWidth()));
    // The workspaces slide toward their own side of the switch; the title-bar actions keep their crossfade, on the
    // same curve. Collapsing a workspace drops its handoff visual, so the departure has to start before that.
    motion::describe(audio ? L"切回音乐解锁" : L"切到歌词工具");
    motion::workspace(audio ? UIElement(LyricsWorkspace()) : UIElement(AudioView()),
                      audio ? UIElement(AudioView()) : UIElement(LyricsWorkspace()), !audio);
    motion::describe({});
    AudioView().Visibility(audio ? Visibility::Visible : Visibility::Collapsed);
    AudioActions().Visibility(audio ? Visibility::Visible : Visibility::Collapsed);
    LyricsActions().Visibility(audio ? Visibility::Collapsed : Visibility::Visible);
    LyricsWorkspace().Visibility(audio ? Visibility::Collapsed : Visibility::Visible);
    motion::changed(AudioActions());
    motion::changed(LyricsActions());
}

fire_and_forget MainWindow::ShowAccounts(mu::MusicProvider provider) {
    if (accounts_open_) co_return;
    auto strong = get_strong();
    accounts_open_ = true;
    auto dialog = make<AccountsDialog>();
    get_self<AccountsDialog>(dialog)->Select(provider);
    dialog.XamlRoot(Content().XamlRoot());
    // The dialog sits in a popup outside Root, so a theme forced on Root (preview renders) is passed on.
    if (Root().RequestedTheme() != ElementTheme::Default) dialog.RequestedTheme(Root().RequestedTheme());
    try {
        co_await dialog.ShowAsync();
    } catch (hresult_error const&) {
        // Another dialog (closing confirmation) is open; only one can show at a time.
    }
    accounts_open_ = false;
    UpdateAccountsButton();
    Lyrics().AccountsChanged();
}

void MainWindow::UpdateAccountsButton() {
    const bool any = accounts::signed_in(mu::MusicProvider::netease) || accounts::signed_in(mu::MusicProvider::qq);
    AccountsIcon().Glyph(any ? L"\uEA8C" : L"\uE77B");  // ContactSolid / Contact
    hstring name = L"账号登录";
    for (const auto provider : mu::all_providers) {
        if (accounts::signed_in(provider)) name = name + L"，" + to_hstring(mu::provider_title(provider)) + L"会话已保存";
    }
    Controls::ToolTipService::SetToolTip(AccountsButton(), box_value(any ? name : hstring(L"官方登录后自动保存会话，无需手动获取 Cookie")));
    Automation::AutomationProperties::SetName(AccountsButton(), name);
}

fire_and_forget MainWindow::ConfirmClose() {
    auto strong = get_strong();
    if (co_await CloseDialog().ShowAsync() != Controls::ContentDialogResult::Primary) co_return;
    closing_confirmed_ = true;
    Close();
}

Controls::ContentDialog MainWindow::CloseDialog() {
    Controls::ContentDialog dialog;
    dialog.XamlRoot(Content().XamlRoot());
    dialog.Title(box_value(L"任务仍在进行"));
    dialog.Content(box_value(L"现在退出将中断当前任务。已经完成的文件会保留。"));
    dialog.PrimaryButtonText(L"退出");
    dialog.CloseButtonText(L"继续任务");
    dialog.DefaultButton(Controls::ContentDialogButton::Close);
    return dialog;
}

void MainWindow::ShowWorkspace(bool audio) { WorkspaceBar().SelectedItem(audio ? AudioItem() : LyricsItem()); }

void MainWindow::OnAddFiles(IInspectable const&, RoutedEventArgs const&) { Audio().ChooseFiles(false); }
void MainWindow::OnAddFolder(IInspectable const&, RoutedEventArgs const&) { Audio().ChooseFiles(true); }
void MainWindow::OnOpenLyrics(IInspectable const&, RoutedEventArgs const&) { Lyrics().OpenLocal(); }

void MainWindow::HandleAccelerator(Windows::System::VirtualKey key, Windows::System::VirtualKeyModifiers modifiers) {
    const bool shift = (modifiers & Windows::System::VirtualKeyModifiers::Shift) != Windows::System::VirtualKeyModifiers::None;
    switch (key) {
        case Windows::System::VirtualKey::O:
            // Like the macOS menu: Ctrl+O opens lyrics in the lyrics workspace and adds files otherwise.
            if (!shift && LyricsWorkspace().Visibility() == Visibility::Visible) {
                Lyrics().OpenLocal();
                break;
            }
            ShowWorkspace(true);
            Audio().ChooseFiles(shift);
            break;
        case Windows::System::VirtualKey::Enter:
            if (AudioView().Visibility() == Visibility::Visible) Audio().Start();
            break;
        case Windows::System::VirtualKey::Number1: ShowWorkspace(true); break;
        case Windows::System::VirtualKey::Number2: ShowWorkspace(false); break;
        case Windows::System::VirtualKey::F:
            ShowWorkspace(false);
            Lyrics().FocusSearch();
            break;
        case Windows::System::VirtualKey::S:
            if (LyricsWorkspace().Visibility() == Visibility::Visible) Lyrics().ExportCurrent();
            break;
    }
}

void MainWindow::OnAcceleratorInvoked(Xaml::Input::KeyboardAccelerator const& accelerator,
                                      Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args) {
    args.Handled(true);
    if (accelerator.Key() == static_cast<Windows::System::VirtualKey>(VK_OEM_PERIOD)) {
        if (LyricsWorkspace().Visibility() == Visibility::Visible) Lyrics().StopExport();
        else Audio().Stop();
        return;
    }
    HandleAccelerator(accelerator.Key(), accelerator.Modifiers());
}

fire_and_forget MainWindow::RenderPreviews(std::wstring directory) {
    auto strong = get_strong();
    auto dispatcher = DispatcherQueue();
    const std::filesystem::path folder(directory);
    std::filesystem::create_directories(folder);
    auto& audio = Audio();
    const auto wait = [&](auto duration) -> Windows::Foundation::IAsyncAction {
        co_await resume_after(duration);
        co_await wil::resume_foreground(dispatcher);
    };
    const auto shot = [&](const wchar_t* name, ElementTheme theme) -> Windows::Foundation::IAsyncAction {
        Root().RequestedTheme(theme);
        co_await wait(600ms);
        co_await SavePreview(folder / (std::wstring(name) + L".png"));
    };
    const auto resize = [this](int height) {
        HWND hwnd = nullptr;
        this->try_as<::IWindowNative>()->get_WindowHandle(&hwnd);
        const double scale = GetDpiForWindow(hwnd) / 96.0;
        AppWindow().Resize({static_cast<int32_t>(1060 * scale + 0.5), static_cast<int32_t>(height * scale + 0.5)});
    };
    // Dialogs sit in popups outside Root and do not follow the theme forced on it.
    const auto open = [&](Controls::ContentDialog const& dialog) {
        dialog.RequestedTheme(ElementTheme::Light);
        dialog.ShowAsync();
    };

    if (auto convert = environment(L"MUSICUNLOCK_RENDER_CONVERT"); !convert.empty()) {
        // End-to-end: import a real folder, convert it into <folder>\输出, then capture the result.
        audio.UseOutputDirectory(std::filesystem::path(convert) / L"输出");
        audio.Import({std::filesystem::path(convert)});
        for (int i = 0; i < 600 && !audio.Idle(); ++i) co_await wait(100ms);
        audio.Start();
        co_await wait(100ms);
        for (int i = 0; i < 600 && !audio.Idle(); ++i) co_await wait(100ms);
        co_await shot(L"converted-light", ElementTheme::Light);
        co_await shot(L"converted-dark", ElementTheme::Dark);
        preview::write_automation_tree(Root(), folder / L"converted-a11y.txt");
        Application::Current().Exit();
        co_return;
    }

    // Same set as the macOS PreviewRenderer: empty and populated queue, light and dark.
    co_await shot(L"empty-light", ElementTheme::Light);
    co_await shot(L"empty-dark", ElementTheme::Dark);
    audio.PreviewDropTarget(true);
    co_await shot(L"drop-light", ElementTheme::Light);
    audio.PreviewDropTarget(false);
    audio.ShowSampleJobs();
    co_await shot(L"queue-light", ElementTheme::Light);
    co_await shot(L"queue-dark", ElementTheme::Dark);
    preview::write_automation_tree(Root(), folder / L"a11y.txt");

    // Focus and hover visuals, the row menu and the dialogs (popups, drawn over the window by SavePreview).
    audio.PreviewKeyboard();
    co_await shot(L"keyboard-light", ElementTheme::Light);
    audio.PreviewRowMenu(1);
    co_await shot(L"menu-light", ElementTheme::Light);
    for (const auto& popup : Media::VisualTreeHelper::GetOpenPopupsForXamlRoot(Root().XamlRoot())) popup.IsOpen(false);
    const auto about = audio.AboutDialog();
    open(about);
    co_await shot(L"about-light", ElementTheme::Light);
    about.Hide();
    co_await wait(300ms);
    const auto close = CloseDialog();
    open(close);
    co_await shot(L"close-light", ElementTheme::Light);
    close.Hide();
    co_await wait(300ms);

    // The lyrics workspace, empty and with sample songs; macOS renders it at 1060 x 760.
    resize(760);
    ShowWorkspace(false);
    co_await shot(L"lyrics-empty-light", ElementTheme::Light);
    co_await shot(L"lyrics-empty-dark", ElementTheme::Dark);
    Lyrics().ShowSampleSongs();
    co_await shot(L"lyrics-editor-light", ElementTheme::Light);
    co_await shot(L"lyrics-editor-dark", ElementTheme::Dark);
    resize(720);
    ShowWorkspace(true);

    // Stand-in for a system setting a render must not change: 200 % text.
    preview::scale_text(Root(), 2.0);
    audio.PreviewTextScale(2.0);
    co_await shot(L"text200-queue-light", ElementTheme::Light);
    audio.OnClearQueue(nullptr, nullptr);
    co_await shot(L"text200-empty-light", ElementTheme::Light);
    preview::scale_text(Root(), 0.5);
    audio.PreviewTextScale(0);
    // Lay out at normal size before the workspace switch, which remembers the title bar actions' width.
    Root().UpdateLayout();

    resize(760);
    co_await RenderAccountPreviews(folder);
    co_await RenderLogin(folder);

    // Simulated high contrast last, as for the account dialog: it stays on Root.
    resize(720);
    ShowWorkspace(true);
    SimulateHighContrast(Root());
    Root().RequestedTheme(ElementTheme::Dark);
    co_await shot(L"contrast-empty", ElementTheme::Light);
    audio.ShowSampleJobs();
    co_await shot(L"contrast-queue", ElementTheme::Light);
    // Last: moving focus through everything scrolls the settings panel.
    preview::write_tab_order(Root(), folder / L"tab-order.txt");
    Application::Current().Exit();
}

void MainWindow::RenderMotion(std::wstring directory, std::wstring scenarios) {
    // The three scenarios that prove the capture works. C–F register their own beside these.
    motion_capture::set_key_handler([this](auto key, auto modifiers) { HandleAccelerator(key, modifiers); });
    motion_capture::add(L"workspace", [this](motion_capture::Driver& driver) {
        driver.capture(1200);
        driver.key(Windows::System::VirtualKey::Number2, Windows::System::VirtualKeyModifiers::Control);
        driver.wait(400);
        driver.key(Windows::System::VirtualKey::Number1, Windows::System::VirtualKeyModifiers::Control);
        driver.wait(400);
    });
    motion_capture::add(L"notice", [this](motion_capture::Driver& driver) {
        driver.capture(1400);
        Audio().ShowNotice("这是一条示例提示");
        driver.wait(400);
        Audio().DismissNotice();
        driver.wait(300);
    });
    motion_capture::add(L"drop", [this](motion_capture::Driver& driver) {
        driver.capture(1400);
        Audio().PreviewDropTarget(true);
        driver.wait(400);
        Audio().PreviewDropTarget(false);
        driver.wait(300);
    });
    motion_capture::run(Root(), std::move(scenarios), std::move(directory));
}

Windows::Foundation::IAsyncAction MainWindow::RenderAccountPreviews(std::filesystem::path directory) {
    using Preview = AccountsDialog::Preview;
    auto strong = get_strong();
    auto dispatcher = DispatcherQueue();
    // The lyrics workspace title bar with its 账号登录 button.
    ShowWorkspace(false);
    for (const auto [name, theme] : {std::pair{L"light", ElementTheme::Light}, std::pair{L"dark", ElementTheme::Dark}}) {
        Root().RequestedTheme(theme);
        co_await resume_after(400ms);
        co_await wil::resume_foreground(dispatcher);
        co_await SavePreview(directory / (std::wstring(L"accounts-button-") + name + L".png"));
    }
    auto dialog = make<AccountsDialog>();
    auto& view = *get_self<AccountsDialog>(dialog);
    dialog.XamlRoot(Content().XamlRoot());
    auto showing = dialog.ShowAsync();
    co_await resume_after(400ms);
    co_await wil::resume_foreground(dispatcher);
    // The QQ page itself is never loaded in renders, but WebView2 must start from the app folder with its
    // data inside MUSICUNLOCK_DATA_DIR.
    view.ShowPreview(Preview::official_page);
    log_error(L"webview2", S_OK, co_await view.CheckWebView(mu::MusicProvider::qq));
    co_await wil::resume_foreground(dispatcher);

    struct State {
        const wchar_t* name;
        Preview preview;
    };
    constexpr State states[] = {{L"qr-waiting", Preview::qr_waiting},     {L"qr-scanned", Preview::qr_scanned},
                                {L"qr-expired", Preview::qr_expired},     {L"qr-confirmed", Preview::qr_confirmed},
                                {L"qr-refused", Preview::qr_refused},     {L"qq", Preview::official_page},
                                {L"signed-in", Preview::signed_in}};
    struct Theme {
        const wchar_t* name;
        ElementTheme theme;
    };
    constexpr Theme themes[] = {{L"light", ElementTheme::Light}, {L"dark", ElementTheme::Dark}, {L"contrast", ElementTheme::Light}};
    for (const Theme theme : themes) {
        if (std::wstring_view(theme.name) == L"contrast") {
            // High contrast is a system setting the app cannot switch on, so renders swap in Theme.xaml's
            // HighContrast brushes (system window / window-text colours). WinUI's own controls keep their style.
            for (const auto element : {Root().as<FrameworkElement>(), dialog.as<FrameworkElement>()}) {
                SimulateHighContrast(element);
                element.RequestedTheme(ElementTheme::Dark);
            }
        }
        Root().RequestedTheme(theme.theme);
        dialog.RequestedTheme(theme.theme);
        for (const State state : states) {
            view.ShowPreview(state.preview);
            co_await resume_after(500ms);
            co_await wil::resume_foreground(dispatcher);
            co_await SavePreview(directory / (L"accounts-" + std::wstring(state.name) + L"-" + theme.name + L".png"));
        }
    }
    dialog.Hide();
    co_await resume_after(200ms);
    co_await wil::resume_foreground(dispatcher);
}

Windows::Foundation::IAsyncAction MainWindow::RenderLogin(std::filesystem::path directory) {
    auto strong = get_strong();
    auto dispatcher = DispatcherQueue();
    const auto wait = [&](auto duration) -> Windows::Foundation::IAsyncAction {
        co_await resume_after(duration);
        co_await wil::resume_foreground(dispatcher);
    };
    // The dialog ShowAccounts opened, found among the window's popups once the last one has gone (one at a time).
    const auto open_dialog = [&]() -> Windows::Foundation::IAsyncOperation<MusicUnlock::AccountsDialog> {
        for (int i = 0; i < 30 && Media::VisualTreeHelper::GetOpenPopupsForXamlRoot(Root().XamlRoot()).Size(); ++i) {
            co_await wait(100ms);
        }
        ShowAccounts(mu::MusicProvider::qq);
        co_await wait(400ms);
        for (auto const& popup : Media::VisualTreeHelper::GetOpenPopupsForXamlRoot(Root().XamlRoot())) {
            if (auto dialog = popup.Child().try_as<MusicUnlock::AccountsDialog>()) co_return dialog;
        }
        co_return nullptr;
    };
    const auto until = [&](auto done) -> Windows::Foundation::IAsyncOperation<bool> {
        for (int i = 0; i < 100 && !done(); ++i) co_await wait(100ms);
        co_return done();
    };
    const auto step = [](const wchar_t* what, bool ok) { log_error(L"login", ok ? S_OK : E_FAIL, what); };
    ShowWorkspace(false);
    Root().RequestedTheme(ElementTheme::Light);

    auto dialog = co_await open_dialog();
    if (!dialog) co_return step(L"account dialog did not open", false);
    dialog.RequestedTheme(ElementTheme::Light);
    auto* view = get_self<AccountsDialog>(dialog);
    view->Select(mu::MusicProvider::qq);  // the QQ 音乐 tab, as the user picks it
    view->Live(true);
    if (!co_await until([&] { return view->ShowsSession(); })) co_return step(L"qq login was not accepted", false);
    co_await wait(800ms);  // the dialog has closed the login page and read the account name
    step(L"qq session saved and shown", accounts::signed_in(mu::MusicProvider::qq));
    co_await SavePreview(directory / L"login-qq-saved-light.png");
    dialog.Hide();
    co_await wait(500ms);
    step(L"dialog closed, title bar updated", !accounts_open_);
    co_await SavePreview(directory / L"login-qq-lyrics-light.png");

    dialog = co_await open_dialog();
    if (!dialog) co_return step(L"account dialog did not reopen", false);
    dialog.RequestedTheme(ElementTheme::Light);
    view = get_self<AccountsDialog>(dialog);
    view->Select(mu::MusicProvider::qq);
    view->Live(false);
    step(L"reopened on the saved session", view->ShowsSession());
    co_await wait(500ms);
    co_await SavePreview(directory / L"login-qq-reopened-light.png");
    view->OnLogout(nullptr, nullptr);
    co_await wait(1500ms);
    step(L"logged out", !accounts::signed_in(mu::MusicProvider::qq) && !view->ShowsSession());
    co_await SavePreview(directory / L"login-qq-logged-out-light.png");
    dialog.Hide();
    co_await wait(500ms);
    step(L"done, app still running", !accounts_open_);
}

Windows::Foundation::IAsyncAction MainWindow::SavePreview(std::filesystem::path file) {
    using namespace Windows::Graphics::Imaging;
    using namespace Windows::Storage::Streams;
    auto strong = get_strong();
    auto dispatcher = DispatcherQueue();
    Media::Imaging::RenderTargetBitmap bitmap;
    co_await bitmap.RenderAsync(Root());
    const IBuffer base = co_await bitmap.GetPixelsAsync();
    const int width = bitmap.PixelWidth(), height = bitmap.PixelHeight();
    std::vector<uint8_t> pixels(base.data(), base.data() + base.Length());
    // Dialogs live in popups outside Root: draw each over the window (premultiplied "over", as the compositor does).
    const double scale = Root().ActualWidth() > 0 ? width / Root().ActualWidth() : 1;
    // A ContentDialog's smoke layer is a popup of its own and goes under the dialog.
    std::vector<UIElement> layers;
    for (auto const& popup : Media::VisualTreeHelper::GetOpenPopupsForXamlRoot(Root().XamlRoot())) {
        if (const UIElement child = popup.Child()) layers.push_back(child);
    }
    std::stable_partition(layers.begin(), layers.end(),
                          [](UIElement const& layer) { return !layer.try_as<Controls::ContentDialog>(); });
    for (const UIElement& child : layers) {
        Media::Imaging::RenderTargetBitmap layer;
        co_await layer.RenderAsync(child);
        const IBuffer top = co_await layer.GetPixelsAsync();
        const auto origin = child.TransformToVisual(nullptr).TransformPoint({0, 0});
        const int left = static_cast<int>(origin.X * scale + 0.5), up = static_cast<int>(origin.Y * scale + 0.5);
        const int layer_width = layer.PixelWidth();
        for (int y = std::max(0, -up); y < layer.PixelHeight() && y + up < height; ++y) {
            for (int x = std::max(0, -left); x < layer_width && x + left < width; ++x) {
                const uint8_t* over = top.data() + (static_cast<size_t>(y) * layer_width + x) * 4;
                uint8_t* under = pixels.data() + (static_cast<size_t>(y + up) * width + x + left) * 4;
                for (int c = 0; c < 4; ++c) under[c] = static_cast<uint8_t>(over[c] + under[c] * (255 - over[3]) / 255);
            }
        }
    }
    InMemoryRandomAccessStream stream;
    const BitmapEncoder encoder = co_await BitmapEncoder::CreateAsync(BitmapEncoder::PngEncoderId(), stream);
    encoder.SetPixelData(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied, static_cast<uint32_t>(width),
                         static_cast<uint32_t>(height), 96, 96,
                         array_view<const uint8_t>(pixels.data(), static_cast<uint32_t>(pixels.size())));
    co_await encoder.FlushAsync();
    const auto size = static_cast<uint32_t>(stream.Size());
    Buffer png(size);
    stream.Seek(0);
    co_await stream.ReadAsync(png, size, InputStreamOptions::None);
    co_await wil::resume_foreground(dispatcher);
    std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(png.data()), png.Length());
}

}  // namespace winrt::MusicUnlock::implementation
