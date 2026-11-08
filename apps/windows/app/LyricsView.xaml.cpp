#include "pch.h"

#include "LyricsView.xaml.h"
#if __has_include("LyricsView.g.cpp")
#include "LyricsView.g.cpp"
#endif

#include <algorithm>
#include <cmath>

#include <winrt/Windows.ApplicationModel.DataTransfer.h>

#include "Accounts.h"
#include "Settings.h"
#include "Shell.h"
#include "musicunlock/text.h"

namespace winrt::MusicUnlock::implementation {

using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace {

constexpr mu::SearchKind search_kinds[] = {mu::SearchKind::song, mu::SearchKind::album, mu::SearchKind::playlist};
constexpr mu::LyricFormat lyric_formats[] = {mu::LyricFormat::lrc, mu::LyricFormat::srt, mu::LyricFormat::txt,
                                             mu::LyricFormat::vtt};
constexpr mu::LyricEncoding lyric_encodings[] = {mu::LyricEncoding::utf8, mu::LyricEncoding::utf8_bom,
                                                 mu::LyricEncoding::utf16le};
constexpr mu::LyricLayout lyric_layouts[] = {mu::LyricLayout::stagger, mu::LyricLayout::merge, mu::LyricLayout::separate};

Visibility shown(bool visible) { return visible ? Visibility::Visible : Visibility::Collapsed; }

template <typename Value, std::size_t Count>
int32_t index_of(const Value (&values)[Count], Value value) {
    return static_cast<int32_t>(std::find(std::begin(values), std::end(values), value) - std::begin(values));
}

std::string row_id(IInspectable const& sender) {
    return to_string(unbox_value<hstring>(sender.as<FrameworkElement>().Tag()));
}

/// TextBox stores line breaks as "\r"; the model and exports use "\n".
std::string normalized(std::string text) {
    text = mu::replace_all(std::move(text), "\r\n", "\n");
    std::replace(text.begin(), text.end(), '\r', '\n');
    return text;
}

/// Platform and file errors carry actionable UTF-8 text; anything else gets a generic reason.
std::string failure_text(const std::exception& error, const char* fallback) {
    const std::string_view what = error.what();
    const bool ours = dynamic_cast<const mu::LyricError*>(&error) || dynamic_cast<const mu::DecodeError*>(&error);
    return ours && mu::is_valid_utf8(what) && !what.empty() ? std::string(what) : fallback;
}

}  // namespace

void LyricsView::InitializeComponent() {
    LyricsViewT::InitializeComponent();
    api_ = std::make_shared<const mu::MusicApi>(mu::make_system_transport());
    for (auto provider : mu::all_providers) ProviderBox().Items().Append(box_value(to_hstring(mu::provider_title(provider))));
    for (auto kind : search_kinds) KindBox().Items().Append(box_value(to_hstring(mu::search_kind_label(kind))));
    for (auto tab : mu::all_lyric_tabs) EditorTabs().Labels().Append(to_hstring(mu::lyric_tab_label(tab)));
    EditorTabs().SelectedIndex(0);
    for (auto format : lyric_formats) {
        FormatBox().Items().Append(box_value(to_hstring(mu::format_label(format))));
        FormatOption().Items().Append(box_value(to_hstring(mu::format_label(format))));
    }
    for (auto encoding : lyric_encodings) EncodingOption().Items().Append(box_value(to_hstring(mu::encoding_label(encoding))));
    for (auto layout : lyric_layouts) LayoutOption().Items().Append(box_value(to_hstring(mu::layout_label(layout))));
    ShowTab(mu::LyricTab::original);
    ResultList().ItemsSource(rows_);
    const auto saved = settings::load_lyrics();
    workspace_.set_options(saved.options);
    workspace_.set_output_directory(saved.output_directory.value_or(shell::downloads_folder() / L"音匣输出" / L"歌词"));
    Refresh();
}

void LyricsView::UsePreviewData() {
    persist_ = false;
    workspace_.set_output_directory(L"C:\\Users\\示例\\Downloads\\音匣输出\\歌词");
    Refresh();
}

void LyricsView::ShowSampleSongs() {
    auto sample = [](std::string id, std::string title, std::int64_t duration) {
        mu::LyricSong song;
        song.song_id = std::move(id);
        song.title = std::move(title);
        song.artist = "音匣示例";
        song.album = "旅途中的歌";
        song.duration_ms = duration;
        return song;
    };
    // Through the same calls as a real search, without the network.
    workspace_.set_query("远行的旋律");
    workspace_.set_kind(mu::SearchKind::song);
    const auto request = workspace_.search();
    mu::SearchResult result;
    result.songs = {sample("example", "远行的旋律", 216'000), sample("example2", "午后的唱片", 187'000)};
    if (const auto lyrics = workspace_.search_finished(*request, std::move(result))) {
        workspace_.lyrics_loaded(*lyrics, {"[00:00.000] 远行的旋律\n[00:12.500] 把风装进行囊\n[00:17.800] 让脚步跟上远方\n"
                                           "[00:23.200] 城市慢慢退到身后\n[00:28.600] 我们向着光的方向",
                                           "[00:12.500] Pack the wind into a bag\n[00:17.800] Follow the road beyond", "", ""});
    }
    workspace_.select_all();
    Refresh();
}

HWND LyricsView::OwnerWindow() const {
    return Microsoft::UI::GetWindowFromWindowId(XamlRoot().ContentIslandEnvironment().AppWindowId());
}

void LyricsView::SaveSettings() const {
    if (persist_) settings::save_lyrics({workspace_.options(), workspace_.output_directory()});
}

void LyricsView::SetOptions(mu::LyricOptions options) {
    if (options == workspace_.options()) return;
    workspace_.set_options(std::move(options));
    SaveSettings();
    RefreshEditor();
    RefreshFooter();
}

void LyricsView::SyncOptions() {
    const auto& options = workspace_.options();
    syncing_ = true;
    FormatOption().SelectedIndex(index_of(lyric_formats, options.format));
    EncodingOption().SelectedIndex(index_of(lyric_encodings, options.encoding));
    if (to_string(FilenameOption().Text()) != options.filename) FilenameOption().Text(to_hstring(options.filename));
    const auto& folder = workspace_.output_directory();
    OutputOptionName().Text(folder.filename().c_str());
    ToolTipService::SetToolTip(OutputOption(), box_value(hstring(folder.c_str())));
    Automation::AutomationProperties::SetName(OutputOption(), L"更改歌词输出文件夹：" + hstring(folder.c_str()));
    OriginalOption().IsChecked(options.include_original);
    TranslationOption().IsChecked(options.include_translation);
    RomanizationOption().IsChecked(options.include_romanization);
    PinyinOption().IsChecked(options.include_pinyin);
    LayoutOption().SelectedIndex(index_of(lyric_layouts, options.layout));
    if (to_string(SeparatorOption().Text()) != options.separator) SeparatorOption().Text(to_hstring(options.separator));
    SeparatorOption().IsEnabled(options.layout == mu::LyricLayout::merge);
    PrecisionOption().SelectedIndex(options.precision - 1);
    OffsetOption().Value(static_cast<double>(options.offset));
    IgnoreEmptyOption().IsChecked(options.ignore_empty);
    SkipInstrumentalOption().IsChecked(options.skip_instrumental);
    WordTimingOption().IsChecked(options.word_timing);
    syncing_ = false;
}

void LyricsView::ReadOptions() {
    if (syncing_) return;
    auto checked = [](Primitives::ToggleButton const& box) { return box.IsChecked() && box.IsChecked().Value(); };
    auto options = workspace_.options();
    if (const int index = FormatOption().SelectedIndex(); index >= 0) options.format = lyric_formats[index];
    if (const int index = EncodingOption().SelectedIndex(); index >= 0) options.encoding = lyric_encodings[index];
    if (const int index = LayoutOption().SelectedIndex(); index >= 0) options.layout = lyric_layouts[index];
    if (const int index = PrecisionOption().SelectedIndex(); index >= 0) options.precision = index + 1;
    options.filename = to_string(FilenameOption().Text());
    options.separator = to_string(SeparatorOption().Text());
    // An emptied field reads as NaN; keep the last offset then.
    if (const double offset = OffsetOption().Value(); !std::isnan(offset)) {
        options.offset = std::llround(std::clamp(offset, -86'400'000.0, 86'400'000.0));
    }
    options.include_original = checked(OriginalOption());
    options.include_translation = checked(TranslationOption());
    options.include_romanization = checked(RomanizationOption());
    options.include_pinyin = checked(PinyinOption());
    options.ignore_empty = checked(IgnoreEmptyOption());
    options.skip_instrumental = checked(SkipInstrumentalOption());
    options.word_timing = checked(WordTimingOption());
    SeparatorOption().IsEnabled(options.layout == mu::LyricLayout::merge);
    SetOptions(std::move(options));
}

fire_and_forget LyricsView::OnShowSettings(IInspectable const&, RoutedEventArgs const&) {
    auto strong = get_strong();
    SyncOptions();
    SettingsDialog().XamlRoot(XamlRoot());
    try {
        co_await SettingsDialog().ShowAsync();
    } catch (hresult_error const&) {
        // Another dialog is already open.
    }
}

void LyricsView::Refresh() {
    const bool exporting = workspace_.exporting();
    const bool searching = workspace_.searching();
    syncing_ = true;
    ProviderBox().SelectedIndex(index_of(mu::all_providers, workspace_.provider()));
    KindBox().SelectedIndex(index_of(search_kinds, workspace_.kind()));
    if (to_string(QueryBox().Text()) != workspace_.query()) QueryBox().Text(to_hstring(workspace_.query()));
    syncing_ = false;
    QueryBox().IsEnabled(!exporting);
    SearchButton().Visibility(shown(!searching));
    SearchButton().IsEnabled(!exporting && !mu::trim_space(workspace_.query()).empty());
    CancelSearchButton().Visibility(shown(searching));
    const bool signed_in = accounts::signed_in(workspace_.provider());
    SessionGlyph().Glyph(signed_in ? L"\xEA18" : L"\xE774");  // Shield / Globe
    SessionText().Text(signed_in ? L"登录会话已保存" : L"公开内容可免登录读取");

    Notice().Message(to_hstring(workspace_.notice()));
    Notice().IsOpen(!workspace_.notice().empty());
    Notice().Visibility(shown(!workspace_.notice().empty()));

    const bool nothing = workspace_.songs().empty() && workspace_.collections().empty();
    ResultsTitle().Text(to_hstring(workspace_.results_title()));
    ResultsSubtitle().Text(to_hstring(workspace_.results_subtitle()));
    SelectAllButton().Visibility(shown(!workspace_.songs().empty()));
    SelectAllButton().Content(box_value(workspace_.all_checked() ? L"取消全选" : L"全选"));
    SelectAllButton().IsEnabled(!exporting);
    ResultsLoading().Visibility(shown(searching && nothing));
    ResultsLoadingText().Text(to_hstring(workspace_.loading_text()));
    ResultsEmpty().Visibility(shown(!searching && nothing));
    ResultList().Visibility(shown(!nothing));
    LoadMoreButton().Visibility(shown(workspace_.has_more()));
    LoadMoreButton().IsEnabled(!searching && !exporting);
    SyncRows();
    RefreshEditor();
    RefreshFooter();
}

void LyricsView::RefreshFooter() {
    const bool exporting = workspace_.exporting();
    const auto& failures = workspace_.failures();
    FailuresPanel().Visibility(shown(!failures.empty()));
    FailuresTitle().Text(to_hstring(workspace_.failures_title()));
    std::string lines;
    for (const auto& failure : failures) lines += (lines.empty() ? "" : "\n") + failure;
    FailuresText().Text(to_hstring(lines));

    ExportProgressPanel().Visibility(shown(exporting));
    ExportProgressText().Text(to_hstring(workspace_.progress_text()));
    ExportProgress().Maximum(static_cast<double>(std::max<std::size_t>(1, workspace_.export_total())));
    ExportProgress().Value(static_cast<double>(workspace_.progress()));
    FormatPanel().Visibility(shown(!exporting));
    syncing_ = true;
    FormatBox().SelectedIndex(index_of(lyric_formats, workspace_.options().format));
    syncing_ = false;

    MoreButton().Visibility(shown(!exporting));
    CopyItem().IsEnabled(!workspace_.preview().empty());
    ExportCheckedButton().Visibility(shown(!exporting && workspace_.checked_count() > 0));
    ExportCheckedButton().Content(box_value(to_hstring(workspace_.export_checked_label())));
    ExportCheckedButton().IsEnabled(!workspace_.searching());
    StopExportButton().Visibility(shown(exporting));
    StopExportButton().IsEnabled(!workspace_.stop_requested());
    StopExportButton().Content(box_value(workspace_.stop_requested() ? L"正在停止…" : L"停止导出"));
    ExportCurrentButton().Visibility(shown(!exporting));
    ExportCurrentButton().IsEnabled(workspace_.can_export_current());
}

/// Patches the bound list in place so rows keep their containers (and focus) across updates.
void LyricsView::SyncRows() {
    const bool enabled = !workspace_.exporting();
    std::map<std::string, com_ptr<LyricRow>> rows;
    std::vector<com_ptr<LyricRow>> wanted;
    for (const auto& collection : workspace_.collections()) {
        const auto id = collection.id();
        auto& row = row_cache_[id];
        if (!row) row = make_self<LyricRow>(collection);
        row->Update(false, false, enabled);
        wanted.push_back(rows[id] = row);
    }
    for (const auto& song : workspace_.songs()) {
        const auto id = song.id();
        auto& row = row_cache_[id];
        if (!row) row = make_self<LyricRow>(song);
        row->Update(workspace_.is_checked(id), workspace_.is_selected(id), enabled);
        wanted.push_back(rows[id] = row);
    }
    row_cache_ = std::move(rows);
    for (uint32_t i = rows_.Size(); i-- > 0;) {
        if (!row_cache_.contains(to_string(rows_.GetAt(i).as<MusicUnlock::LyricRow>().Id()))) rows_.RemoveAt(i);
    }
    for (uint32_t i = 0; i < wanted.size(); ++i) {
        if (i < rows_.Size() && rows_.GetAt(i).as<MusicUnlock::LyricRow>().Id() == wanted[i]->Id()) continue;
        uint32_t index = 0;
        if (rows_.IndexOf(*wanted[i], index)) rows_.RemoveAt(index);
        rows_.InsertAt(i, *wanted[i]);
    }
}

void LyricsView::ShowTab(mu::LyricTab tab) {
    tab_ = tab;
    syncing_ = true;
    EditorTabs().SelectedIndex(static_cast<int32_t>(index_of(mu::all_lyric_tabs, tab)));
    syncing_ = false;
}

void LyricsView::RefreshEditor() {
    const mu::LyricSong* song = workspace_.selected();
    EditorEmpty().Visibility(shown(!song));
    EditorPanel().Visibility(shown(song));
    if (!song) {
        shown_song_.clear();
        return;
    }
    // A newly selected song opens on its original text, like macOS.
    if (song->id() != shown_song_) {
        shown_song_ = song->id();
        ShowTab(mu::LyricTab::original);
    }
    SongTitle().Text(to_hstring(song->title));
    SongDetail().Text(to_hstring(mu::LyricWorkspace::song_detail(*song)));
    RetryButton().IsEnabled(workspace_.can_retry());

    const bool loading = workspace_.loading_lyrics();
    const bool preview = tab_ == mu::LyricTab::preview;
    const auto error = preview ? workspace_.preview_error() : std::nullopt;
    LyricsLoading().Visibility(shown(loading));
    PreviewError().Visibility(shown(!loading && error));
    PreviewScroll().Visibility(shown(!loading && preview && !error));
    EditorBox().Visibility(shown(!loading && !preview));
    if (error) PreviewErrorText().Text(to_hstring(*error));
    if (preview && !error) PreviewText().Text(to_hstring(workspace_.preview()));
    if (!preview) {
        const std::string expected = workspace_.text(tab_);
        if (normalized(expected) != normalized(to_string(EditorBox().Text()))) EditorBox().Text(to_hstring(expected));
        Automation::AutomationProperties::SetName(
            EditorBox(), L"编辑" + to_hstring(mu::lyric_tab_label(tab_)) + L"歌词，支持 LRC 时间戳");
    }
    EditorBox().IsReadOnly(workspace_.exporting());
    EditorHint().Text(preview ? L"预览与实际导出内容一致" : L"可直接编辑，修改会用于本次导出");
    LineCount().Text(to_hstring(std::to_string(workspace_.line_count()) + " 行"));
}

void LyricsView::OnProviderChanged(IInspectable const&, SelectionChangedEventArgs const&) {
    const int index = ProviderBox().SelectedIndex();
    if (syncing_ || index < 0) return;
    workspace_.set_provider(mu::all_providers[index]);
    Refresh();
}

void LyricsView::OnKindChanged(IInspectable const&, SelectionChangedEventArgs const&) {
    const int index = KindBox().SelectedIndex();
    if (syncing_ || index < 0) return;
    workspace_.set_kind(search_kinds[index]);
}

void LyricsView::OnQueryChanged(IInspectable const&, TextChangedEventArgs const&) {
    workspace_.set_query(to_string(QueryBox().Text()));
    SearchButton().IsEnabled(!workspace_.exporting() && !mu::trim_space(workspace_.query()).empty());
}

void LyricsView::OnQueryKeyDown(IInspectable const&, Input::KeyRoutedEventArgs const& args) {
    if (args.Key() != Windows::System::VirtualKey::Enter) return;
    args.Handled(true);
    Search(workspace_.search());
}

void LyricsView::OnCancelSearch(IInspectable const&, RoutedEventArgs const&) {
    search_cancel_.cancel();
    workspace_.cancel_search();
    Refresh();
}

void LyricsView::OnNoticeClosed(InfoBar const&, InfoBarClosedEventArgs const&) {
    workspace_.dismiss_notice();
    Notice().Visibility(Visibility::Collapsed);
}

void LyricsView::OnSelectAll(IInspectable const&, RoutedEventArgs const&) {
    workspace_.select_all();
    Refresh();
}

void LyricsView::OnResultClicked(IInspectable const&, ItemClickEventArgs const& args) {
    const auto row = get_self<LyricRow>(args.ClickedItem().as<MusicUnlock::LyricRow>());
    if (const auto& collection = row->collection()) Search(workspace_.open(*collection));
    else LoadLyrics(workspace_.select(to_string(row->Id())));
}

void LyricsView::OnPreviewSong(IInspectable const& sender, RoutedEventArgs const&) {
    LoadLyrics(workspace_.select(row_id(sender)));
}

void LyricsView::OnToggleSong(IInspectable const& sender, RoutedEventArgs const&) {
    workspace_.toggle(row_id(sender));
    Refresh();
}

void LyricsView::OnTabChanged(MusicUnlock::GlassSwitch const&, IInspectable const&) {
    const int32_t index = EditorTabs().SelectedIndex();
    if (syncing_ || index < 0 || index >= static_cast<int32_t>(std::size(mu::all_lyric_tabs))) return;
    tab_ = mu::all_lyric_tabs[index];
    RefreshEditor();
}

void LyricsView::OnEditorChanged(IInspectable const&, TextChangedEventArgs const&) {
    if (tab_ == mu::LyricTab::preview || !workspace_.selected() || workspace_.loading_lyrics()) return;
    std::string text = normalized(to_string(EditorBox().Text()));
    // Also raised after the view itself replaces the text; only real edits reach the model.
    if (text == normalized(workspace_.text(tab_))) return;
    workspace_.edit(tab_, std::move(text));
    RefreshEditor();
    RefreshFooter();
}

void LyricsView::OnFormatChanged(IInspectable const&, SelectionChangedEventArgs const&) {
    const int index = FormatBox().SelectedIndex();
    if (syncing_ || index < 0) return;
    auto options = workspace_.options();
    options.format = lyric_formats[index];
    SetOptions(std::move(options));
}

void LyricsView::OnCopy(IInspectable const&, RoutedEventArgs const&) {
    const std::string text = workspace_.copy_preview();
    if (text.empty()) return;
    Windows::ApplicationModel::DataTransfer::DataPackage package;
    package.SetText(to_hstring(text));
    Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(package);
    Refresh();
}

void LyricsView::OnOpenOutput(IInspectable const&, RoutedEventArgs const&) {
    std::error_code error;
    std::filesystem::create_directories(workspace_.output_directory(), error);
    shell::open_folder(workspace_.output_directory());
}

void LyricsView::OnChooseOutput(IInspectable const&, RoutedEventArgs const&) {
    if (workspace_.exporting()) return;
    if (auto folder = shell::pick_folder(OwnerWindow(), workspace_.output_directory(), L"选择歌词输出文件夹")) {
        workspace_.set_output_directory(*folder);
        SaveSettings();
        SyncOptions();
        Refresh();
    }
}

void LyricsView::FocusSearch() {
    // The workspace may have just been switched to; focus once it is laid out.
    DispatcherQueue().TryEnqueue([weak = get_weak()] {
        if (auto self = weak.get()) {
            self->QueryBox().Focus(FocusState::Keyboard);
            self->QueryBox().SelectAll();
        }
    });
}

fire_and_forget LyricsView::OpenLocal() {
    if (workspace_.exporting()) co_return;
    const auto file = shell::pick_lyric_file(OwnerWindow());
    if (!file) co_return;
    auto strong = get_strong();
    apartment_context ui;
    co_await resume_background();
    std::optional<std::string> text;
    std::string failure;
    try {
        text = mu::read_lyric_file(*file);
    } catch (const std::exception& error) {
        failure = failure_text(error, "无法读取这个歌词文件，请确认文件仍然存在。");
    }
    co_await ui;
    if (!text) workspace_.show_notice(std::move(failure));
    else if (workspace_.import_local(mu::path_to_utf8(file->stem()), std::move(*text))) lyrics_cancel_.cancel();
    Refresh();
}

void LyricsView::StopExport() {
    workspace_.request_stop();
    export_cancel_.cancel();
    RefreshFooter();
}

fire_and_forget LyricsView::Export(std::optional<mu::ExportBatch> batch) {
    if (batch) lyrics_cancel_.cancel();  // the batch freezes the visible edit
    Refresh();
    if (!batch) co_return;
    auto strong = get_strong();
    const auto api = api_;
    export_cancel_ = {};
    const auto cancel = export_cancel_;
    std::map<mu::MusicProvider, std::string> cookies;
    for (auto provider : mu::all_providers) cookies[provider] = accounts::cookie_header(provider);
    apartment_context ui;
    for (const auto& job : batch->jobs) {
        if (cancel.cancelled()) break;
        co_await resume_background();
        std::optional<mu::ExportOutcome> outcome;
        std::string failure;
        bool cancelled = false;
        try {
            outcome = mu::export_song(*api, job, batch->options, batch->directory, cookies[job.song.provider], cancel);
        } catch (const mu::Cancelled&) {
            cancelled = true;
        } catch (const std::exception& error) {
            failure = failure_text(error, "导出失败，请检查输出文件夹后重试。");
        }
        co_await ui;
        if (cancelled) break;
        if (outcome) workspace_.export_succeeded(job, *outcome);
        else workspace_.export_failed(job, std::move(failure));
        RefreshFooter();
    }
    workspace_.end_export();
    Refresh();
}

fire_and_forget LyricsView::Search(std::optional<mu::SearchRequest> request) {
    if (request) {
        search_cancel_.cancel();
        search_cancel_ = {};
        if (!request->more) lyrics_cancel_.cancel();  // a new search also clears the editor
    }
    Refresh();
    if (!request) co_return;
    auto strong = get_strong();
    const auto api = api_;
    const auto cancel = search_cancel_;
    const std::string cookie = accounts::cookie_header(request->provider);
    apartment_context ui;
    co_await resume_background();
    std::optional<mu::SearchResult> result;
    std::string failure;
    bool cancelled = false;
    try {
        result = mu::run_search(*api, *request, cookie, cancel);
    } catch (const mu::Cancelled&) {
        cancelled = true;
    } catch (const std::exception& error) {
        failure = failure_text(error, "搜索失败，请检查网络后重试。");
    }
    co_await ui;
    if (cancelled) co_return;
    if (result) LoadLyrics(workspace_.search_finished(*request, std::move(*result)));
    else workspace_.search_failed(*request, std::move(failure));
    Refresh();
}

fire_and_forget LyricsView::LoadLyrics(std::optional<mu::LyricsRequest> request) {
    if (request) {
        lyrics_cancel_.cancel();
        lyrics_cancel_ = {};
    }
    Refresh();
    if (!request) co_return;
    auto strong = get_strong();
    const auto api = api_;
    const auto cancel = lyrics_cancel_;
    const std::string cookie = accounts::cookie_header(request->song.provider);
    apartment_context ui;
    co_await resume_background();
    std::optional<mu::LyricDocument> document;
    std::string failure;
    bool cancelled = false;
    try {
        document = api->lyrics(request->song, cookie, cancel);
    } catch (const mu::Cancelled&) {
        cancelled = true;
    } catch (const std::exception& error) {
        failure = failure_text(error, "获取歌词失败，请检查网络后重试。");
    }
    co_await ui;
    if (cancelled) co_return;
    if (document) workspace_.lyrics_loaded(*request, std::move(*document));
    else workspace_.lyrics_failed(*request, std::move(failure));
    Refresh();
}

}  // namespace winrt::MusicUnlock::implementation
