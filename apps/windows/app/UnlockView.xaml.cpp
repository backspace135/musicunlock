#include "pch.h"

#include "UnlockView.xaml.h"
#if __has_include("UnlockView.g.cpp")
#include "UnlockView.g.cpp"
#endif

#include "Shell.h"
#include "musicunlock/text.h"

namespace winrt::MusicUnlock::implementation {

using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace {

uint64_t job_id(IInspectable const& sender) {
    return unbox_value<uint64_t>(sender.as<FrameworkElement>().Tag());
}

Visibility shown(bool visible) { return visible ? Visibility::Visible : Visibility::Collapsed; }

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
    for (auto policy : naming_policies) NamingBox().Items().Append(box_value(to_hstring(mu::naming_label(policy))));
    NamingBox().SelectedIndex(0);
    JobList().ItemsSource(items_);
    SetOutputDirectory(shell::downloads_folder() / L"音匣输出");
    Refresh();
}

void UnlockView::UsePreviewData() { SetOutputDirectory(L"C:\\Users\\示例\\Downloads\\音匣输出"); }

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
    Notice().Message(to_hstring(notice_));
    Notice().IsOpen(!notice_.empty());
    ImportingPanel().Visibility(shown(importing_));

    const bool no_matches = !empty && queue_.visible(filter_).empty();
    EmptyQueue().Visibility(shown(empty));
    EmptyFilter().Visibility(shown(no_matches));
    FileList().Visibility(shown(!empty && !no_matches));
    EmptyFilterText().Text(L"暂无" + to_hstring(mu::filter_label(filter_)) + L"的文件");
    EmptyFilterGlyph().Glyph(filter_ == mu::QueueFilter::done ? L"\xE73E" : L"\xE7B8");
    SyncItems();

    ClearCompletedItem().IsEnabled(done > 0);
    ClearQueueItem().IsEnabled(!running && !empty);

    StatusTitle().Text(to_hstring(queue_.status_title()));
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
    StartButton().IsEnabled(waiting > 0 && !importing_);
    OutputButton().IsEnabled(!running);
    NamingBox().IsEnabled(!running);
    ChooseFilesButton().IsEnabled(!importing_);
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
        if (!item) item = make_self<JobItem>(job);
        else item->Update(job);
        if (i < items_.Size() && items_.GetAt(i).as<MusicUnlock::JobItem>().Id() == job.id) continue;
        items_.InsertAt(i, *item);
    }
}

void UnlockView::OnFilterChanged(SelectorBar const& bar, SelectorBarSelectionChangedEventArgs const&) {
    uint32_t index = 0;
    if (bar.Items().IndexOf(bar.SelectedItem(), index)) filter_ = mu::all_filters[index];
    Refresh();
}

void UnlockView::OnNoticeClosed(InfoBar const&, InfoBarClosedEventArgs const&) { notice_.clear(); }

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

void UnlockView::OnRemoveJob(IInspectable const& sender, RoutedEventArgs const&) {
    queue_.remove(job_id(sender));
    Refresh();
}

void UnlockView::OnRevealOutput(IInspectable const& sender, RoutedEventArgs const&) {
    if (const auto* job = queue_.find(job_id(sender)); job && !job->output.empty()) shell::reveal(job->output);
}

}  // namespace winrt::MusicUnlock::implementation
