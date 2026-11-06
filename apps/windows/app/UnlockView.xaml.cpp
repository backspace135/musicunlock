#include "pch.h"

#include "UnlockView.xaml.h"
#if __has_include("UnlockView.g.cpp")
#include "UnlockView.g.cpp"
#endif

#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Storage.h>

#include "Settings.h"
#include "Shell.h"
#include "detail.h"
#include "musicunlock/text.h"

namespace winrt::MusicUnlock::implementation {

using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace {

/// kgm.mask ships next to the executable; KGM / VPR files report that it is missing otherwise.
const mu::Bytes& kgm_mask() {
    static const mu::Bytes mask = [] {
        wchar_t exe[MAX_PATH];
        const DWORD length = GetModuleFileNameW(nullptr, exe, MAX_PATH);
        try {
            return mu::detail::read_file(std::filesystem::path(std::wstring(exe, length)).replace_filename(L"kgm.mask"));
        } catch (...) {
            return mu::Bytes{};
        }
    }();
    return mask;
}

/// Exception text for the row. Library messages are UTF-8; anything else falls back to a generic reason.
std::string failure_text(const std::exception& error) {
    const std::string_view what = error.what();
    return mu::is_valid_utf8(what) && !what.empty() ? std::string(what) : "转换失败，文件可能已损坏或无法读取。";
}

uint64_t job_id(IInspectable const& sender) {
    return unbox_value<uint64_t>(sender.as<FrameworkElement>().Tag());
}

Visibility shown(bool visible) { return visible ? Visibility::Visible : Visibility::Collapsed; }

/// A named element inside a row template (template names are not reachable through FindName).
FrameworkElement find_named(DependencyObject const& root, std::wstring_view name) {
    const int32_t count = Media::VisualTreeHelper::GetChildrenCount(root);
    for (int32_t i = 0; i < count; ++i) {
        const auto child = Media::VisualTreeHelper::GetChild(root, i);
        if (auto element = child.try_as<FrameworkElement>(); element && element.Name() == name) return element;
        if (auto found = find_named(child, name)) return found;
    }
    return nullptr;
}

constexpr mu::NamingPolicy naming_policies[] = {mu::NamingPolicy::original, mu::NamingPolicy::artist_title,
                                                mu::NamingPolicy::title};

}  // namespace

void UnlockView::InitializeComponent() {
    UnlockViewT::InitializeComponent();
    for (auto filter : mu::all_filters) {
        SelectorBarItem item;
        item.Text(to_hstring(mu::filter_label(filter)));
        FilterBar().Items().Append(item);
    }
    FilterBar().SelectedItem(FilterBar().Items().GetAt(0));
    const auto saved = settings::load();
    naming_ = saved.naming;
    for (auto policy : naming_policies) NamingBox().Items().Append(box_value(to_hstring(mu::naming_label(policy))));
    NamingBox().SelectedIndex(static_cast<int32_t>(std::find(std::begin(naming_policies), std::end(naming_policies), naming_) -
                                                   std::begin(naming_policies)));
    JobList().ItemsSource(items_);
    drift_ = motion::Loop::drift(Disc());
    hop_ = motion::Loop::hop(DropGlyph());
    SetOutputDirectory(saved.output_directory.value_or(shell::downloads_folder() / L"音匣输出"));
    ApplyMotion();
    Refresh();
}

void UnlockView::ApplyMotion() {
    for (const UIElement& element : {UIElement(EmptyQueue()), UIElement(EmptyFilter()), UIElement(FileList()),
                                     UIElement(FilterBar()), UIElement(StatusDetail()), UIElement(BatchProgress()),
                                     UIElement(RetryButton()), UIElement(RevealButton()), UIElement(StopButton()),
                                     UIElement(StartButton())}) {
        motion::crossfade(element);
    }
    motion::settle(Notice());
    motion::settle(ImportingPanel());
    motion::overlay(DropOverlay());
    // Rows drop in from above (motion::enter, per row) and fade out in place as they leave.
    Media::Animation::TransitionCollection transitions;
    if (motion::enabled()) transitions.Append(Media::Animation::AddDeleteThemeTransition());
    JobList().ItemContainerTransitions(transitions);
    if (!motion::enabled()) drift_.stop();
    else if (queue_.jobs().empty()) drift_.start();
}

void UnlockView::UsePreviewData() {
    persist_ = false;
    SetOutputDirectory(L"C:\\Users\\示例\\Downloads\\音匣输出");
}

void UnlockView::ShowSampleJobs() {
    mu::ImportScan scan;
    scan.files = {{L"C:\\示例\\远行的旋律.ncm", 28'400'000}, {L"C:\\示例\\午后唱片.mflac", 36'800'000},
                  {L"C:\\示例\\旧日时光.mgg", 8'600'000}};
    queue_.add(scan);
    const auto ids = queue_.begin_batch();
    queue_.job_succeeded(ids[1], {L"C:\\示例\\午后唱片.flac", "flac", {}});
    queue_.job_failed(ids[2], "此文件未内嵌解密密钥，目前无法离线转换");
    queue_.end_batch();
    Refresh();
}

void UnlockView::PreviewKeyboard() {
    JobList().UpdateLayout();
    if (auto first = JobList().ContainerFromIndex(0).try_as<Control>()) first.Focus(FocusState::Keyboard);
    if (auto second = JobList().ContainerFromIndex(1).try_as<Control>()) VisualStateManager::GoToState(second, L"PointerOver", false);
}

void UnlockView::PreviewRowMenu(uint32_t index) {
    if (auto row = JobList().ContainerFromIndex(index).try_as<ListViewItem>(); row && row.ContextFlyout()) {
        Primitives::FlyoutShowOptions options;
        options.Position(Windows::Foundation::Point{240, 40});
        row.ContextFlyout().ShowAt(row, options);
    }
}

void UnlockView::SetOutputDirectory(std::filesystem::path directory) {
    output_directory_ = std::move(directory);
    OutputName().Text(output_directory_.filename().c_str());
    OutputParent().Text(output_directory_.parent_path().c_str());
    ToolTipService::SetToolTip(OutputButton(), box_value(hstring(output_directory_.c_str())));
    Automation::AutomationProperties::SetName(OutputButton(), L"更改输出文件夹：" + hstring(output_directory_.c_str()));
}

void UnlockView::ShowNotice(std::string text) {
    notice_ = std::move(text);
    Refresh();
}

void UnlockView::Refresh() {
    const bool empty = queue_.jobs().empty();
    const bool running = queue_.running();
    const auto done = queue_.count(mu::JobState::done);
    const auto failed = queue_.count(mu::JobState::failed);
    const auto waiting = queue_.count(mu::JobState::waiting);

    CountText().Text(empty ? hstring() : to_hstring(std::to_string(queue_.jobs().size()) + " 个文件"));
    FilterBar().Visibility(shown(!empty));
    const hstring notice = to_hstring(notice_);
    if (!notice.empty() && (notice != Notice().Message() || Notice().Visibility() == Visibility::Collapsed)) Announce(notice);
    if (!notice.empty()) Notice().Message(notice);  // keeps the text while the banner fades out
    Notice().Visibility(shown(!notice.empty()));
    ImportingPanel().Visibility(shown(imports_ > 0));

    const bool no_matches = !empty && queue_.visible(filter_).empty();
    EmptyQueue().Visibility(shown(empty));
    if (empty) drift_.start();
    else drift_.stop();
    EmptyFilter().Visibility(shown(no_matches));
    FileList().Visibility(shown(!empty && !no_matches));
    EmptyFilterText().Text(L"暂无" + to_hstring(mu::filter_label(filter_)) + L"的文件");
    EmptyFilterGlyph().Glyph(filter_ == mu::QueueFilter::done ? L"\xE73E" : L"\xE7B8");
    SyncItems();

    ClearCompletedItem().IsEnabled(done > 0);
    ClearQueueItem().IsEnabled(!running && !empty);

    if (const hstring title = to_hstring(queue_.status_title()); title != status_title_) {
        status_title_ = title;
        StatusTitle().Text(title);
        // Polite live region: Narrator reads progress and the batch summary as they change.
        if (auto peer = Automation::Peers::FrameworkElementAutomationPeer::FromElement(StatusTitle())) {
            peer.RaiseAutomationEvent(Automation::Peers::AutomationEvents::LiveRegionChanged);
        }
    }
    StatusDetail().Text(to_hstring(queue_.status_detail()));
    StatusDetail().Visibility(shown(!running));
    BatchProgress().Visibility(shown(running));
    BatchProgress().Maximum(static_cast<double>(std::max<std::size_t>(1, queue_.batch_count())));
    BatchProgress().Value(static_cast<double>(queue_.batch_finished()));
    RetryButton().Visibility(shown(failed > 0 && !running));
    RevealButton().Visibility(shown(done > 0));
    StopButton().Visibility(shown(running));
    StopButton().IsEnabled(!queue_.stop_requested());
    StopButton().Content(box_value(queue_.stop_requested() ? L"正在停止…" : L"停止转换"));
    StartButton().Visibility(shown(!running));
    StartButton().IsEnabled(waiting > 0 && imports_ == 0);
    OutputButton().IsEnabled(!running);
    NamingBox().IsEnabled(!running);
    ChooseFilesButton().IsEnabled(imports_ == 0);
}

/// Patches the bound list in place so rows animate in and out instead of the whole list reloading.
void UnlockView::SyncItems() {
    const auto visible = queue_.visible(filter_);
    std::map<uint64_t, const mu::AudioJob*> wanted;
    for (const auto* job : visible) wanted[job->id] = job;
    for (uint32_t i = items_.Size(); i-- > 0;) {
        if (!wanted.contains(items_.GetAt(i).as<MusicUnlock::JobItem>().Id())) items_.RemoveAt(i);
    }
    std::erase_if(item_cache_, [&](const auto& entry) { return queue_.find(entry.first) == nullptr; });
    for (uint32_t i = 0; i < visible.size(); ++i) {
        const mu::AudioJob& job = *visible[i];
        auto& item = item_cache_[job.id];
        if (!item) {
            item = make_self<JobItem>(job);
        } else {
            const bool state_changed = item->job().state != job.state;
            item->Update(job);
            if (state_changed) PopState(*item, job.state == mu::JobState::done);
        }
        if (i < items_.Size() && items_.GetAt(i).as<MusicUnlock::JobItem>().Id() == job.id) continue;
        items_.InsertAt(i, *item);
        entering_.insert(job.id);
    }
}

void UnlockView::OnContainerContentChanging(ListViewBase const&, ContainerContentChangingEventArgs const& args) {
    if (args.InRecycleQueue()) return;
    const auto container = args.ItemContainer();
    const auto root = container.ContentTemplateRoot();
    if (!root) return;
    // The row menu sits on the template root; keyboard requests (Shift+F10) come from the focused container.
    container.ContextFlyout(root.ContextFlyout());
    if (auto background = find_named(root, L"RunningBackground")) motion::crossfade(background);
    if (entering_.erase(args.Item().as<MusicUnlock::JobItem>().Id()) > 0) motion::enter(root);
}

void UnlockView::PopState(MusicUnlock::JobItem const& item, bool bounce) {
    const auto container = JobList().ContainerFromItem(item).try_as<ListViewItem>();
    if (!container || !container.ContentTemplateRoot()) return;
    if (auto icon = find_named(container.ContentTemplateRoot(), L"StateIcon"); icon && icon.Visibility() == Visibility::Visible) {
        motion::pop(icon, bounce);
    }
}

void UnlockView::Announce(hstring const& text) {
    using namespace Automation::Peers;
    if (auto peer = FrameworkElementAutomationPeer::CreatePeerForElement(StatusTitle())) {
        peer.RaiseNotificationEvent(AutomationNotificationKind::ActionCompleted,
                                    AutomationNotificationProcessing::CurrentThenMostRecent, text, L"MusicUnlock.Queue");
    }
}

void UnlockView::OnFilterChanged(SelectorBar const& bar, SelectorBarSelectionChangedEventArgs const&) {
    uint32_t index = 0;
    if (bar.Items().IndexOf(bar.SelectedItem(), index)) filter_ = mu::all_filters[index];
    Refresh();
}

void UnlockView::OnNoticeClosing(InfoBar const&, InfoBarClosingEventArgs const& args) {
    // Hide it through Visibility instead, so it fades out like any other change.
    args.Cancel(true);
    notice_.clear();
    Refresh();
}

void UnlockView::OnClearCompleted(IInspectable const&, RoutedEventArgs const&) {
    queue_.clear_completed();
    Refresh();
}

void UnlockView::OnClearQueue(IInspectable const&, RoutedEventArgs const&) {
    queue_.clear();
    notice_.clear();
    Refresh();
}

void UnlockView::OnShowAll(IInspectable const&, RoutedEventArgs const&) {
    FilterBar().SelectedItem(FilterBar().Items().GetAt(0));
}

void UnlockView::OnRemoveJob(IInspectable const& sender, RoutedEventArgs const&) { RemoveJob(job_id(sender)); }

bool UnlockView::RemoveJob(uint64_t id) {
    if (!queue_.remove(id)) return false;
    Refresh();
    return true;
}

/// Delete removes the focused row and keeps keyboard focus in the list.
void UnlockView::OnJobListKeyDown(IInspectable const&, Input::KeyRoutedEventArgs const& args) {
    if (args.Key() != Windows::System::VirtualKey::Delete) return;
    const auto container = args.OriginalSource().try_as<ListViewItem>();
    if (!container) return;
    const int32_t index = JobList().IndexFromContainer(container);
    if (index < 0 || !RemoveJob(container.Content().as<MusicUnlock::JobItem>().Id())) return;
    args.Handled(true);
    JobList().UpdateLayout();
    if (items_.Size() == 0) {
        if (ChooseFilesButton().Visibility() == Visibility::Visible) ChooseFilesButton().Focus(FocusState::Keyboard);
        return;
    }
    const auto next = std::min<int32_t>(index, static_cast<int32_t>(items_.Size()) - 1);
    if (auto row = JobList().ContainerFromIndex(next).try_as<Control>()) row.Focus(FocusState::Keyboard);
}

void UnlockView::OnRevealSource(IInspectable const& sender, RoutedEventArgs const&) {
    if (const auto* job = queue_.find(job_id(sender))) shell::reveal(job->source);
}

void UnlockView::OnRevealOutput(IInspectable const& sender, RoutedEventArgs const&) {
    if (const auto* job = queue_.find(job_id(sender)); job && !job->output.empty()) shell::reveal(job->output);
}

void UnlockView::OnFindLyrics(IInspectable const& sender, RoutedEventArgs const&) {
    // The restored tag title, else the file name without extension (macOS searchForAudio).
    if (const auto* job = queue_.find(job_id(sender)); job && find_lyrics_) find_lyrics_(job->display_title());
}

}  // namespace winrt::MusicUnlock::implementation

namespace winrt::MusicUnlock::implementation {

HWND UnlockView::OwnerWindow() const {
    return Microsoft::UI::GetWindowFromWindowId(XamlRoot().ContentIslandEnvironment().AppWindowId());
}

void UnlockView::ChooseFiles(bool folders) {
    if (imports_ > 0) return;
    auto paths = shell::pick_audio(OwnerWindow(), folders);
    if (!paths.empty()) Import(std::move(paths));
}

fire_and_forget UnlockView::Import(std::vector<std::filesystem::path> paths) {
    if (paths.empty()) co_return;
    auto strong = get_strong();
    apartment_context ui;
    ++imports_;
    Refresh();
    co_await resume_background();
    mu::ImportScan scan = mu::scan_import(paths);
    co_await ui;
    --imports_;
    notice_ = queue_.add(scan);
    FilterBar().SelectedItem(FilterBar().Items().GetAt(0));
    Refresh();
}

fire_and_forget UnlockView::Start() {
    if (queue_.running() || imports_ > 0 || queue_.count(mu::JobState::waiting) == 0) co_return;
    auto strong = get_strong();
    std::error_code error;
    std::filesystem::create_directories(output_directory_, error);
    if (!std::filesystem::is_directory(output_directory_)) {
        ShowNotice("无法创建输出目录：" + mu::path_to_utf8(output_directory_));
        co_return;
    }
    const auto directory = output_directory_;
    const auto naming = naming_;
    const auto ids = queue_.begin_batch();
    notice_.clear();
    Refresh();
    apartment_context ui;
    for (const auto id : ids) {
        if (queue_.stop_requested()) break;
        const mu::AudioJob* job = queue_.find(id);
        if (!job) continue;  // removed while waiting
        const auto source = job->source;
        queue_.job_started(id);
        Refresh();
        co_await resume_background();
        std::optional<mu::ConversionOutput> output;
        std::string failure;
        try {
            output = mu::convert_file(source, directory, naming, kgm_mask());
        } catch (const std::exception& reason) {
            failure = failure_text(reason);
        }
        co_await ui;
        if (output) {
            queue_.job_succeeded(id, *output);
        } else {
            if (const mu::AudioJob* failed = queue_.find(id)) {
                Announce(to_hstring(failed->display_title() + "：未完成，" + failure));
            }
            queue_.job_failed(id, std::move(failure));
        }
        Refresh();
    }
    notice_ = queue_.end_batch();
    Refresh();
}

void UnlockView::Stop() {
    queue_.request_stop();
    Refresh();
}

void UnlockView::SaveSettings() const {
    if (persist_) settings::save({output_directory_, naming_});
}

void UnlockView::OnChooseOutput(IInspectable const&, RoutedEventArgs const&) {
    if (auto folder = shell::pick_folder(OwnerWindow(), output_directory_)) {
        SetOutputDirectory(*folder);
        SaveSettings();
    }
}

void UnlockView::OnNamingChanged(IInspectable const&, SelectionChangedEventArgs const&) {
    const int index = NamingBox().SelectedIndex();
    if (index < 0 || naming_policies[index] == naming_) return;
    naming_ = naming_policies[index];
    SaveSettings();
}

void UnlockView::OnRetry(IInspectable const&, RoutedEventArgs const&) {
    if (!queue_.retry_failed()) return;
    FilterBar().SelectedItem(FilterBar().Items().GetAt(0));
    Start();
}

void UnlockView::OnOpenOutput(IInspectable const&, RoutedEventArgs const&) { shell::open_folder(output_directory_); }

fire_and_forget UnlockView::OnAbout(IInspectable const&, RoutedEventArgs const&) {
    auto strong = get_strong();
    if (co_await AboutDialog().ShowAsync() == ContentDialogResult::Primary) shell::open_bundled(L"THIRD_PARTY_NOTICES.txt");
}

ContentDialog UnlockView::AboutDialog() {
    ContentDialog dialog;
    dialog.XamlRoot(XamlRoot());
    dialog.Title(box_value(L"音匣 MusicUnlock"));
    TextBlock text;
    text.TextWrapping(TextWrapping::Wrap);
    text.IsTextSelectionEnabled(true);
    text.Text(L"原生 Windows 音频解封装与歌词工具\nWinUI 3 · C++/WinRT\n音频本地处理，歌词直连音乐平台\n\n"
              L"音匣采用 MIT 许可证\n© 2026 backspace135 and MusicUnlock contributors\n\n"
              L"解密算法移植自 Unlock Music\n© 2019–2021 MengYX（MIT）\n\n"
              L"歌词功能参照 MusicLyricApp 的工作流独立实现。\n"
              L"TEA 算法包含 Go Authors 的 BSD-3-Clause 实现衍生代码。完整许可随应用分发。");
    dialog.Content(text);
    dialog.PrimaryButtonText(L"查看开源许可");
    dialog.CloseButtonText(L"好");
    dialog.DefaultButton(ContentDialogButton::Close);
    return dialog;
}

void UnlockView::OnDragOver(DragEventArgs const& args) {
    if (!args.DataView().Contains(Windows::ApplicationModel::DataTransfer::StandardDataFormats::StorageItems())) return;
    args.AcceptedOperation(Windows::ApplicationModel::DataTransfer::DataPackageOperation::Copy);
    args.DragUIOverride().Caption(L"加入转换队列");
    ShowDropTarget(true);
}

void UnlockView::OnDragLeave(DragEventArgs const&) { ShowDropTarget(false); }

/// The overlay covers the workspace, its icon hops, and the empty-state disc lifts and tilts.
void UnlockView::ShowDropTarget(bool visible) {
    if ((DropOverlay().Visibility() == Visibility::Visible) == visible) return;
    DropOverlay().Visibility(shown(visible));
    motion::lift(DiscLift(), visible);
    if (visible) hop_.start();
    else hop_.stop();
}

fire_and_forget UnlockView::ReceiveDrop(DragEventArgs args) {
    ShowDropTarget(false);
    if (!args.DataView().Contains(Windows::ApplicationModel::DataTransfer::StandardDataFormats::StorageItems())) co_return;
    auto strong = get_strong();
    auto deferral = args.GetDeferral();
    const auto items = co_await args.DataView().GetStorageItemsAsync();
    deferral.Complete();
    std::vector<std::filesystem::path> paths;
    for (const auto& item : items) {
        if (!item.Path().empty()) paths.emplace_back(std::wstring(item.Path()));
    }
    Import(std::move(paths));
}

}  // namespace winrt::MusicUnlock::implementation
