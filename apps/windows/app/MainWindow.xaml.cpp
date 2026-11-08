#include "pch.h"

#include "MainWindow.xaml.h"

#include "Accounts.h"
#include "AccountsDialog.xaml.h"
#include "GlassSwitchTests.h"
#include "GlassMotion.h"
#include "GlassMenu.h"
#include "GlassMenuButton.h"
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
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Provider.h>
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
    Audio().OnQueueChanged([this] {
        motion::describe(L"添加文件");
        motion::bounce(AddFilesIcon());
    });
    Audio().OnFindLyricsRequested([this](std::string title) {
        ShowWorkspace(false);
        Lyrics().SearchForAudio(std::move(title));
    });
    glass_menu::attach(Root());
    AccountsButton().Flyout(nullptr);
    auto account_menu = get_self<GlassMenuButton>(AccountsButton());
    account_menu->expand = [this] { OnAccounts(nullptr, nullptr); };
    account_menu->collapse = [] { glass_menu::close(); };
    ApplyMotion();
    // "Animation effects" may have been switched in Settings while the window was in the background.
    Activated([this](auto&&, WindowActivatedEventArgs const& args) {
        if (args.WindowActivationState() != WindowActivationState::Deactivated) ApplyMotion();
        else glass_menu::close();
    });
    WorkspaceBar().Labels().ReplaceAll({L"音乐解锁", L"歌词工具"});
    WorkspaceBar().Glyphs().ReplaceAll({L"\uE785", L"\uE8E4"});
    WorkspaceBar().SelectedIndex(0);
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

void MainWindow::OnAccounts(IInspectable const&, RoutedEventArgs const&) {
    glass_menu::show_entries(AccountsButton(), L"账号登录", false, [this] {
            std::vector<glass_menu::Entry> entries;
            bool first = true;
            for (const auto provider : mu::all_providers) {
                if (!first) entries.push_back({glass_menu::Entry::Kind::divider, {}, false, true, {}});
                first = false;
                const std::wstring title(to_hstring(mu::provider_title(provider)));
                if (accounts::signed_in(provider)) {
                    entries.push_back({glass_menu::Entry::Kind::note, title + L"：会话已保存", false, true, {}});
                    entries.push_back({glass_menu::Entry::Kind::item, L"重新登录" + title, false, true,
                                       [this, provider] { ShowAccounts(provider); }});
                    entries.push_back({glass_menu::Entry::Kind::item, L"退出" + title, false, true, [this, provider] {
                                           accounts::logout(provider);
                                           UpdateAccountsButton();
                                           Lyrics().AccountsChanged();
                                       }});
                } else {
                    entries.push_back({glass_menu::Entry::Kind::item, L"登录" + title, false, true,
                                       [this, provider] { ShowAccounts(provider); }});
                }
            }
            return entries;
    });
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

void MainWindow::OnWorkspaceChanged(MusicUnlock::GlassSwitch const&, IInspectable const&) {
    if (!syncing_workspace_) ApplyWorkspace(WorkspaceBar().SelectedIndex() == 0);
}

void MainWindow::ApplyWorkspace(bool audio) {
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
    if (any != accounts_signed_in_) {
        accounts_signed_in_ = any;
        motion::describe(L"账号");
        motion::replace(any ? AccountsSignedOut() : AccountsSignedIn(), any ? AccountsSignedIn() : AccountsSignedOut());
    }
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

void MainWindow::ShowWorkspace(bool audio) {
    syncing_workspace_ = true;
    WorkspaceBar().SelectedIndex(audio ? 0 : 1);
    syncing_workspace_ = false;
    ApplyWorkspace(audio);
}

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
    // 先应用主题再打开菜单，避免主题变化的关闭行为使展开预览失效。
    Root().RequestedTheme(ElementTheme::Light);
    Root().UpdateLayout();
    co_await wait(600ms);
    glass_menu::open_for_preview(Lyrics().MenuButton(L"导出格式：LRC"));
    co_await shot(L"glass-format-light", ElementTheme::Light);
    {
        auto peer = Automation::Peers::FrameworkElementAutomationPeer::CreatePeerForElement(Lyrics().MenuButton(L"导出格式"));
        auto expand = peer.GetPattern(Automation::Peers::PatternInterface::ExpandCollapse)
                          .try_as<Automation::Provider::IExpandCollapseProvider>();
        if (!expand || expand.ExpandCollapseState() != Automation::ExpandCollapseState::Expanded) {
            throw hresult_error(E_FAIL, L"菜单按钮缺少已展开的 ExpandCollapse 模式");
        }
    }
    bool menu_visible = false;
    for (const auto& popup : Media::VisualTreeHelper::GetOpenPopupsForXamlRoot(Root().XamlRoot())) {
        if (popup.Child().try_as<MusicUnlock::GlassSurface>()) menu_visible = true;
    }
    if (!menu_visible) throw hresult_error(E_FAIL, L"玻璃菜单预览时菜单已关闭");
    bool checked_lrc = false;
    std::function<void(Automation::Peers::AutomationPeer const&)> check_menu;
    check_menu = [&](Automation::Peers::AutomationPeer const& peer) {
        if (peer.GetName() == L"LRC") {
            auto toggle = peer.GetPattern(Automation::Peers::PatternInterface::Toggle)
                              .try_as<Automation::Provider::IToggleProvider>();
            checked_lrc = peer.GetAutomationControlType() == Automation::Peers::AutomationControlType::MenuItem &&
                toggle && toggle.ToggleState() == Automation::ToggleState::On;
        }
        if (auto children = peer.GetChildren()) for (auto const& child : children) check_menu(child);
    };
    for (auto const& popup : Media::VisualTreeHelper::GetOpenPopupsForXamlRoot(Root().XamlRoot())) {
        if (auto surface = popup.Child().try_as<MusicUnlock::GlassSurface>()) {
            auto items = surface.Content().as<FrameworkElement>();
            check_menu(Automation::Peers::FrameworkElementAutomationPeer::CreatePeerForElement(items));
        }
    }
    if (!checked_lrc) throw hresult_error(E_FAIL, L"菜单项没有通过标准模式报告勾选状态");
    // 通过真实菜单行的 Invoke 模式选择，检查显示值、讲述人名称及业务模型的同步。
    std::function<bool(DependencyObject const&, hstring const&)> invoke_named;
    invoke_named = [&](DependencyObject const& node, hstring const& name) {
        if (const auto item = node.try_as<Controls::Button>(); item &&
            Automation::AutomationProperties::GetName(item) == name) {
            auto peer = Automation::Peers::FrameworkElementAutomationPeer::CreatePeerForElement(item);
            peer.GetPattern(Automation::Peers::PatternInterface::Invoke).as<Automation::Provider::IInvokeProvider>().Invoke();
            return true;
        }
        for (int i = 0; i < Media::VisualTreeHelper::GetChildrenCount(node); ++i) {
            if (invoke_named(Media::VisualTreeHelper::GetChild(node, i), name)) return true;
        }
        return false;
    };
    bool selected_srt = false;
    for (const auto& popup : Media::VisualTreeHelper::GetOpenPopupsForXamlRoot(Root().XamlRoot())) {
        if (invoke_named(popup.Child(), L"SRT")) { selected_srt = true; break; }
    }
    co_await wait(100ms);
    const auto format_picker = Lyrics().MenuButton(L"导出格式");
    if (!selected_srt || Automation::AutomationProperties::GetName(format_picker) != L"导出格式：SRT") {
        throw hresult_error(E_FAIL, L"菜单选择没有同步到选择器和讲述人名称");
    }
    {
        auto peer = Automation::Peers::FrameworkElementAutomationPeer::CreatePeerForElement(format_picker);
        if (peer.GetPattern(Automation::Peers::PatternInterface::ExpandCollapse)
                .as<Automation::Provider::IExpandCollapseProvider>().ExpandCollapseState() != Automation::ExpandCollapseState::Collapsed) {
            throw hresult_error(E_FAIL, L"选择后菜单按钮未报告已收起");
        }
    }
    glass_menu::open_for_preview(format_picker);
    bool restored_lrc = false;
    for (const auto& popup : Media::VisualTreeHelper::GetOpenPopupsForXamlRoot(Root().XamlRoot())) {
        if (invoke_named(popup.Child(), L"LRC")) { restored_lrc = true; break; }
    }
    co_await wait(100ms);
    if (!restored_lrc || Automation::AutomationProperties::GetName(format_picker) != L"导出格式：LRC") {
        throw hresult_error(E_FAIL, L"菜单恢复选择失败");
    }
    glass_menu::close();
    OnAccounts(nullptr, nullptr);
    co_await shot(L"glass-accounts-light", ElementTheme::Light);
    bool account_menu = false;
    for (const auto& popup : Media::VisualTreeHelper::GetOpenPopupsForXamlRoot(Root().XamlRoot())) {
        if (popup.Child().try_as<Controls::ContentDialog>()) throw hresult_error(E_FAIL, L"账号按钮不应直接打开登录对话框");
        if (popup.Child().try_as<MusicUnlock::GlassSurface>()) account_menu = true;
    }
    if (!account_menu) throw hresult_error(E_FAIL, L"账号菜单未打开");
    glass_menu::close();
    Root().RequestedTheme(ElementTheme::Dark);
    co_await wait(600ms);
    glass_menu::open_for_preview(Lyrics().MenuButton(L"导出格式：LRC"));
    co_await shot(L"glass-format-dark", ElementTheme::Dark);
    glass_menu::close();
    OnAccounts(nullptr, nullptr);
    co_await shot(L"glass-accounts-dark", ElementTheme::Dark);
    glass_menu::close();
    const auto menu_preview = [&](const wchar_t* name, ElementTheme theme, FrameworkElement const& button,
                                  bool account) -> Windows::Foundation::IAsyncAction {
        Root().RequestedTheme(theme);
        co_await wait(600ms);
        if (account) OnAccounts(nullptr, nullptr);
        else glass_menu::open_for_preview(button);
        co_await shot(name, theme);
        bool visible = false;
        for (const auto& popup : Media::VisualTreeHelper::GetOpenPopupsForXamlRoot(Root().XamlRoot())) {
            if (popup.Child().try_as<MusicUnlock::GlassSurface>()) {
                visible = true;
                preview::write_automation_tree(popup.Child(), folder / (std::wstring(name) + L"-a11y.txt"));
            }
        }
        if (!visible) throw hresult_error(E_FAIL, L"展开菜单预览没有可见的菜单");
        glass_menu::close();
    };
    std::function<FrameworkElement(DependencyObject const&, hstring const&)> find_host;
    find_host = [&](DependencyObject const& node, hstring const& name) -> FrameworkElement {
        if (auto host = node.try_as<Controls::Border>(); host && host.Name() == name) {
            return host.Child().try_as<FrameworkElement>();
        }
        for (int i = 0; i < Media::VisualTreeHelper::GetChildrenCount(node); ++i) {
            if (auto found = find_host(Media::VisualTreeHelper::GetChild(node, i), name)) return found;
        }
        return nullptr;
    };
    for (const auto& [suffix, theme] : {std::pair{L"light", ElementTheme::Light}, std::pair{L"dark", ElementTheme::Dark}}) {
        ShowWorkspace(true);
        co_await wait(100ms);
        co_await menu_preview((L"glass-naming-" + std::wstring(suffix)).c_str(), theme, find_host(AudioView(), L"NamingHost"), false);
        co_await menu_preview((L"glass-queue-more-" + std::wstring(suffix)).c_str(), theme, find_host(AudioView(), L"MoreHost"), false);
        ShowWorkspace(false);
        co_await wait(100ms);
        co_await menu_preview((L"glass-provider-" + std::wstring(suffix)).c_str(), theme, Lyrics().MenuButton(L"音乐来源"), false);
        co_await menu_preview((L"glass-kind-" + std::wstring(suffix)).c_str(), theme, Lyrics().MenuButton(L"搜索类型"), false);
        co_await menu_preview((L"glass-format-" + std::wstring(suffix)).c_str(), theme, Lyrics().MenuButton(L"导出格式"), false);
        co_await menu_preview((L"glass-lyrics-more-" + std::wstring(suffix)).c_str(), theme, Lyrics().MenuButton(L"更多歌词操作"), false);
        co_await menu_preview((L"glass-accounts-" + std::wstring(suffix)).c_str(), theme, AccountsButton(), true);
    }
    co_await shot(L"lyrics-editor-light", ElementTheme::Light);
    co_await shot(L"lyrics-editor-dark", ElementTheme::Dark);
    preview::scale_text(Root(), 2.0);
    co_await shot(L"text200-lyrics-light", ElementTheme::Light);
    co_await shot(L"text200-lyrics-dark", ElementTheme::Dark);
    preview::scale_text(Root(), 0.5);
    resize(720);
    ShowWorkspace(true);

    // Stand-in for a system setting a render must not change: 200 % text.
    preview::scale_text(Root(), 2.0);
    audio.PreviewTextScale(2.0);
    co_await shot(L"text200-queue-light", ElementTheme::Light);
    co_await shot(L"text200-queue-dark", ElementTheme::Dark);
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
    preview::scale_text(Root(), 2.0);
    audio.PreviewTextScale(2.0);
    co_await shot(L"contrast-text200-queue", ElementTheme::Light);
    preview::scale_text(Root(), 0.5);
    audio.PreviewTextScale(0);
    co_await menu_preview(L"glass-naming-contrast", ElementTheme::Light, find_host(AudioView(), L"NamingHost"), false);
    co_await menu_preview(L"glass-queue-more-contrast", ElementTheme::Light, find_host(AudioView(), L"MoreHost"), false);
    ShowWorkspace(false);
    Lyrics().ShowSampleSongs();
    co_await shot(L"contrast-lyrics", ElementTheme::Light);
    preview::scale_text(Root(), 2.0);
    co_await shot(L"contrast-text200-lyrics", ElementTheme::Light);
    preview::scale_text(Root(), 0.5);
    co_await wait(100ms);
    co_await menu_preview(L"glass-provider-contrast", ElementTheme::Light, Lyrics().MenuButton(L"音乐来源"), false);
    co_await menu_preview(L"glass-kind-contrast", ElementTheme::Light, Lyrics().MenuButton(L"搜索类型"), false);
    co_await menu_preview(L"glass-format-contrast", ElementTheme::Light, Lyrics().MenuButton(L"导出格式"), false);
    co_await menu_preview(L"glass-lyrics-more-contrast", ElementTheme::Light, Lyrics().MenuButton(L"更多歌词操作"), false);
    co_await menu_preview(L"glass-accounts-contrast", ElementTheme::Light, AccountsButton(), true);
    ShowWorkspace(true);
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
    motion_capture::add(L"switch-tests", [this](motion_capture::Driver& driver) {
        glass_switch_tests::collection_state();
        driver.note(L"PASS: 动态集合与非法索引");
        glass_switch_tests::selection(WorkspaceBar());
        driver.note(L"PASS: Selection / SelectionItem、名称、序号与必选行为");
        glass_switch_tests::material(WorkspaceBar());
        driver.note(L"PASS: GlassSurface 与独立缩放层");
    });
    motion_capture::add(L"switch-layout-tests", [this](motion_capture::Driver& driver) {
        Audio().ShowSampleJobs();
        Root().UpdateLayout();
        glass_switch_tests::layout(Audio().Filter());
        glass_switch_tests::contrast_resources(Audio().Filter());
        glass_switch_tests::Access::reduced_press(Audio().Filter());
        glass_switch_tests::Access::hover(Audio().Filter());
        glass_switch_tests::Access::press_shadow(Audio().Filter());
        glass_switch_tests::Access::reduced_during_animation(Audio().Filter());
        glass_switch_tests::selection(Audio().Filter());
        ShowWorkspace(false);
        Lyrics().ShowSampleSongs();
        Root().UpdateLayout();
        const auto tabs = glass_switch_tests::find(Lyrics(), L"EditorTabs").as<MusicUnlock::GlassSwitch>();
        glass_switch_tests::selection(tabs);
        glass_switch_tests::layout(tabs);
        ShowWorkspace(true);
        driver.note(L"PASS: text scaling and capsule geometry");
    });
    motion_capture::add(L"switch", [this](motion_capture::Driver& driver) {
        driver.capture(1400);
        driver.note(L"icon segment: workspace switch");
        WorkspaceBar().SelectedIndex(1);
        driver.wait(500);
        WorkspaceBar().SelectedIndex(0);
        driver.wait(400);
    });
    motion_capture::add(L"filter", [this](motion_capture::Driver& driver) {
        driver.capture(900);
        Audio().ShowSampleJobs();
        driver.wait(600);
        Root().UpdateLayout();
        driver.note(L"text segment: queue filter");
        Audio().Filter().SelectedIndex(3);
        driver.wait(500);
        Audio().Filter().SelectedIndex(0);
        driver.wait(400);
    });
    // The queue has to hold something before its rows, icons and highlight have anything to react to.
    motion_capture::add(L"icons", [this](motion_capture::Driver& driver) {
        driver.capture(2200);
        driver.note(L"加入示例文件，添加图标弹跳，提示条弹跳");
        Audio().ShowSampleJobs();
        driver.wait(500);
        driver.note(L"换输出目录，文件夹图标弹跳");
        Audio().UseOutputDirectory(L"C:\\示例\\另一处输出");
        driver.wait(500);
        driver.note(L"开始转换，播放图标弹跳，波形流动");
        Audio().StartForCapture();
        driver.wait(600);
    });
    motion_capture::add(L"selection", [this](motion_capture::Driver& driver) {
        ShowWorkspace(false);
        driver.capture(1800);
        driver.note(L"示例结果出现，高亮滑到选中的那首");
        Lyrics().ShowSampleSongs();
        driver.wait(600);
        driver.note(L"换一首，高亮滑过去");
        Lyrics().SelectSample(1);
        driver.wait(600);
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
    const auto platform_switch = glass_switch_tests::find(dialog, L"ProviderBar").as<MusicUnlock::GlassSwitch>();
    glass_switch_tests::selection(platform_switch);
    glass_switch_tests::layout(platform_switch);
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
        preview::scale_text(dialog, 2.0);
        dialog.UpdateLayout();
        co_await resume_after(500ms);
        co_await wil::resume_foreground(dispatcher);
        co_await SavePreview(directory / (L"accounts-text200-" + std::wstring(theme.name) + L".png"));
        preview::scale_text(dialog, 0.5);
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
    std::ofstream geometry(file.wstring() + L".geometry.txt");
    glass_switch_tests::describe(Root(), geometry);
    // The workspace slide once animated the layout-owned Offset and left a workspace drawn over the title bar.
    for (const FrameworkElement view : {AudioView().as<FrameworkElement>(), LyricsWorkspace().as<FrameworkElement>()}) {
        if (view.Visibility() == Visibility::Visible &&
            view.TransformToVisual(Root()).TransformPoint({0, 0}).Y + 0.5 < AppTitleBar().ActualHeight()) {
            log_error(L"preview", E_FAIL, L"工作区盖住了标题栏");
            throw hresult_error(E_FAIL, L"工作区盖住了标题栏");
        }
    }
    // The window as the compositor draws it (PrintWindow's full-content mode), so composition clips, offsets and
    // opacity show up exactly as on screen; RenderTargetBitmap renders the XAML tree and skips them, which once
    // let a menu that never appeared on screen pass every preview.
    HWND hwnd = nullptr;
    this->try_as<::IWindowNative>()->get_WindowHandle(&hwnd);
    RECT frame{}, client{};
    POINT corner{};
    GetWindowRect(hwnd, &frame);
    GetClientRect(hwnd, &client);
    ClientToScreen(hwnd, &corner);
    const int frame_width = frame.right - frame.left, frame_height = frame.bottom - frame.top;
    const int width = client.right, height = client.bottom;
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    {
        BITMAPINFO info{};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), frame_width, -frame_height, 1, 32, BI_RGB};
        void* bits = nullptr;
        HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        const HGDIOBJ previous = SelectObject(dc, bitmap);
        const bool printed = PrintWindow(hwnd, dc, PW_RENDERFULLCONTENT);
        for (int y = 0; printed && y < height; ++y) {
            const auto* row = static_cast<const uint8_t*>(bits) +
                              (static_cast<size_t>(y + corner.y - frame.top) * frame_width + (corner.x - frame.left)) * 4;
            std::copy_n(row, static_cast<size_t>(width) * 4, pixels.data() + static_cast<size_t>(y) * width * 4);
        }
        SelectObject(dc, previous);
        DeleteObject(bitmap);
        DeleteDC(dc);
        if (!printed) throw hresult_error(HRESULT_FROM_WIN32(GetLastError()), L"PrintWindow 截图失败");
    }
    for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
    const double scale = Root().ActualWidth() > 0 ? width / Root().ActualWidth() : 1;
    // Windowed popups (flyouts, tooltips) are windows of their own and are not in that picture: draw each over it.
    std::vector<UIElement> layers;
    for (auto const& popup : Media::VisualTreeHelper::GetOpenPopupsForXamlRoot(Root().XamlRoot())) {
        if (const UIElement child = popup.Child()) {
            if (popup.IsConstrainedToRootBounds()) glass_switch_tests::describe(child, geometry);
            else layers.push_back(child);
        }
    }
    std::stable_partition(layers.begin(), layers.end(),
                          [](UIElement const& layer) { return !layer.try_as<Controls::ContentDialog>(); });
    for (const UIElement& child : layers) {
        glass_switch_tests::describe(child, geometry);
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
