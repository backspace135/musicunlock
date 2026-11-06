#include "pch.h"

#include "LyricsView.xaml.h"
#if __has_include("LyricsView.g.cpp")
#include "LyricsView.g.cpp"
#endif

#include <algorithm>

#include "Accounts.h"
#include "musicunlock/text.h"

namespace winrt::MusicUnlock::implementation {

using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace {

constexpr mu::SearchKind search_kinds[] = {mu::SearchKind::song, mu::SearchKind::album, mu::SearchKind::playlist};

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

/// Platform errors are actionable LyricError text; anything else gets a generic reason.
std::string failure_text(const std::exception& error, const char* fallback) {
    const std::string_view what = error.what();
    if (dynamic_cast<const mu::LyricError*>(&error) && mu::is_valid_utf8(what) && !what.empty()) return std::string(what);
    return fallback;
}

}  // namespace

void LyricsView::InitializeComponent() {
    LyricsViewT::InitializeComponent();
    api_ = std::make_shared<const mu::MusicApi>(mu::make_system_transport());
    for (auto provider : mu::all_providers) ProviderBox().Items().Append(box_value(to_hstring(mu::provider_title(provider))));
    for (auto kind : search_kinds) KindBox().Items().Append(box_value(to_hstring(mu::search_kind_label(kind))));
    for (auto tab : mu::all_lyric_tabs) {
        SelectorBarItem item;
        item.Text(to_hstring(mu::lyric_tab_label(tab)));
        EditorTabs().Items().Append(item);
    }
    ShowTab(mu::LyricTab::original);
    ResultList().ItemsSource(rows_);
    Refresh();
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
    const bool signed_in = !accounts::cookie_header(workspace_.provider()).empty();
    SessionGlyph().Glyph(signed_in ? L"\xEA18" : L"\xE774");  // Shield / Globe
    SessionText().Text(signed_in ? L"登录会话已保存" : L"公开内容可免登录读取");

    Notice().Message(to_hstring(workspace_.notice()));
    Notice().IsOpen(!workspace_.notice().empty());

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
    EditorTabs().SelectedItem(EditorTabs().Items().GetAt(static_cast<uint32_t>(index_of(mu::all_lyric_tabs, tab))));
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

void LyricsView::OnNoticeClosed(InfoBar const&, InfoBarClosedEventArgs const&) { workspace_.dismiss_notice(); }

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

void LyricsView::OnTabChanged(SelectorBar const& bar, SelectorBarSelectionChangedEventArgs const&) {
    uint32_t index = 0;
    if (syncing_ || !bar.Items().IndexOf(bar.SelectedItem(), index)) return;
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
