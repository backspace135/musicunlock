#include "pch.h"

#include "MainWindow.xaml.h"

#include "Accounts.h"
#include "AccountsDialog.xaml.h"
#include "Shell.h"
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
    MainWindowT::InitializeComponent();
    ExtendsContentIntoTitleBar(true);
    SetTitleBar(AppTitleBar());
    SystemBackdrop(Media::MicaBackdrop());
    // AppWindow sizes are physical pixels; DESIGN.md sizes are effective pixels.
    HWND hwnd = nullptr;
    this->try_as<::IWindowNative>()->get_WindowHandle(&hwnd);
    const double scale = GetDpiForWindow(hwnd) / 96.0;
    auto pixels = [scale](int value) { return static_cast<int32_t>(value * scale + 0.5); };
    auto window = AppWindow();
    window.SetIcon(GetIconIdFromIcon(LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1))));
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
        if (closing_confirmed_ || !Audio().Busy()) return;
        args.Cancel(true);
        ConfirmClose();
    });
    Audio().Import(shell::command_line_paths());
    UpdateAccountsButton();
    if (auto directory = environment(L"MUSICUNLOCK_RENDER_DIR"); !directory.empty()) {
        PreviewBackdrop().Visibility(Visibility::Visible);
        if (environment(L"MUSICUNLOCK_RENDER_CONVERT").empty()) Audio().UsePreviewData();
        Root().Loaded([this, directory](auto&&, auto&&) { RenderPreviews(directory); });
    }
}

void MainWindow::OnWorkspaceChanged(Controls::SelectorBar const&, Controls::SelectorBarSelectionChangedEventArgs const&) {
    const bool audio = WorkspaceBar().SelectedItem() == AudioItem();
    AudioView().Visibility(audio ? Visibility::Visible : Visibility::Collapsed);
    AudioActions().Visibility(audio ? Visibility::Visible : Visibility::Collapsed);
    LyricsActions().Visibility(audio ? Visibility::Collapsed : Visibility::Visible);
    LyricsPlaceholder().Visibility(audio ? Visibility::Collapsed : Visibility::Visible);
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
    Controls::ContentDialog dialog;
    dialog.XamlRoot(Content().XamlRoot());
    dialog.Title(box_value(L"任务仍在进行"));
    dialog.Content(box_value(L"现在退出将中断当前任务。已经完成的文件会保留。"));
    dialog.PrimaryButtonText(L"退出");
    dialog.CloseButtonText(L"继续任务");
    dialog.DefaultButton(Controls::ContentDialogButton::Close);
    if (co_await dialog.ShowAsync() != Controls::ContentDialogResult::Primary) co_return;
    closing_confirmed_ = true;
    Close();
}

void MainWindow::ShowWorkspace(bool audio) { WorkspaceBar().SelectedItem(audio ? AudioItem() : LyricsItem()); }

void MainWindow::OnAddFiles(IInspectable const&, RoutedEventArgs const&) { Audio().ChooseFiles(false); }
void MainWindow::OnAddFolder(IInspectable const&, RoutedEventArgs const&) { Audio().ChooseFiles(true); }

void MainWindow::OnAcceleratorInvoked(Xaml::Input::KeyboardAccelerator const& accelerator,
                                      Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args) {
    args.Handled(true);
    const bool shift = (accelerator.Modifiers() & Windows::System::VirtualKeyModifiers::Shift) !=
                       Windows::System::VirtualKeyModifiers::None;
    switch (accelerator.Key()) {
        case Windows::System::VirtualKey::O:
            ShowWorkspace(true);
            Audio().ChooseFiles(shift);
            break;
        case Windows::System::VirtualKey::Enter:
            if (AudioView().Visibility() == Visibility::Visible) Audio().Start();
            break;
        case Windows::System::VirtualKey::Number1: ShowWorkspace(true); break;
        case Windows::System::VirtualKey::Number2: ShowWorkspace(false); break;
        default:  // Ctrl+.
            Audio().Stop();
            break;
    }
}

fire_and_forget MainWindow::RenderPreviews(std::wstring directory) {
    auto strong = get_strong();
    auto dispatcher = DispatcherQueue();
    std::filesystem::create_directories(directory);
    struct Shot {
        const wchar_t* name;
        ElementTheme theme;
    };
    std::vector<Shot> shots;
    const auto convert = environment(L"MUSICUNLOCK_RENDER_CONVERT");
    if (!convert.empty()) {
        // End-to-end: import a real folder, convert it into <folder>\输出, then capture the result.
        Audio().UseOutputDirectory(std::filesystem::path(convert) / L"输出");
        Audio().Import({std::filesystem::path(convert)});
        for (int i = 0; i < 600 && !Audio().Idle(); ++i) co_await resume_after(100ms), co_await wil::resume_foreground(dispatcher);
        Audio().Start();
        co_await resume_after(100ms);
        co_await wil::resume_foreground(dispatcher);
        for (int i = 0; i < 600 && !Audio().Idle(); ++i) co_await resume_after(100ms), co_await wil::resume_foreground(dispatcher);
        shots = {Shot{L"converted-light", ElementTheme::Light}, Shot{L"converted-dark", ElementTheme::Dark}};
    } else {
        // Same set as the macOS PreviewRenderer: empty and populated queue, light and dark.
        shots = {Shot{L"empty-light", ElementTheme::Light}, Shot{L"empty-dark", ElementTheme::Dark},
                 Shot{L"queue-light", ElementTheme::Light}, Shot{L"queue-dark", ElementTheme::Dark}};
    }
    bool samples = false;
    for (const Shot shot : shots) {
        if (!samples && std::wstring_view(shot.name).starts_with(L"queue")) {
            get_self<UnlockView>(AudioView())->ShowSampleJobs();
            samples = true;
        }
        Root().RequestedTheme(shot.theme);
        co_await resume_after(600ms);
        co_await wil::resume_foreground(dispatcher);
        co_await SavePreview(std::filesystem::path(directory) / (std::wstring(shot.name) + L".png"));
    }
    if (convert.empty()) co_await RenderAccountPreviews(directory);
    Application::Current().Exit();
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
                                {L"qq", Preview::official_page},          {L"signed-in", Preview::signed_in}};
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
