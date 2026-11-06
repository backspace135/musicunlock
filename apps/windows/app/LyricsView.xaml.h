#pragma once

#include "LyricsView.g.h"

#include <map>
#include <memory>

#include "LyricRow.h"
#include "musicunlock/lyric_workspace.h"

namespace winrt::MusicUnlock::implementation {

struct LyricsView : LyricsViewT<LyricsView> {
    LyricsView() = default;
    void InitializeComponent();

    void OnProviderChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void OnKindChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void OnQueryChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
    void OnQueryKeyDown(IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
    void OnSearch(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { Search(workspace_.search()); }
    void OnCancelSearch(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnLoadMore(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { Search(workspace_.search(true)); }
    void OnNoticeClosed(Microsoft::UI::Xaml::Controls::InfoBar const&, Microsoft::UI::Xaml::Controls::InfoBarClosedEventArgs const&);
    void OnSelectAll(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnResultClicked(IInspectable const&, Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args);
    void OnPreviewSong(IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnToggleSong(IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnRetryLyrics(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { LoadLyrics(workspace_.retry_lyrics()); }
    void OnTabChanged(Microsoft::UI::Xaml::Controls::SelectorBar const&,
                      Microsoft::UI::Xaml::Controls::SelectorBarSelectionChangedEventArgs const&);
    void OnEditorChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);

private:
    void Refresh();
    void RefreshEditor();
    void SyncRows();
    void ShowTab(mu::LyricTab tab);

    /// Runs a search or link resolution in the background (or shows why it cannot start).
    fire_and_forget Search(std::optional<mu::SearchRequest> request);
    fire_and_forget LoadLyrics(std::optional<mu::LyricsRequest> request);

    mu::LyricWorkspace workspace_;
    std::shared_ptr<const mu::MusicApi> api_;
    mu::CancelToken search_cancel_;
    mu::CancelToken lyrics_cancel_;
    mu::LyricTab tab_ = mu::LyricTab::original;
    std::string shown_song_;  // the song the editor tabs were last reset for
    bool syncing_ = false;    // controls are being updated from the model
    Windows::Foundation::Collections::IObservableVector<IInspectable> rows_ =
        single_threaded_observable_vector<IInspectable>();
    std::map<std::string, com_ptr<LyricRow>> row_cache_;
};

}  // namespace winrt::MusicUnlock::implementation

namespace winrt::MusicUnlock::factory_implementation {

struct LyricsView : LyricsViewT<LyricsView, implementation::LyricsView> {};

}  // namespace winrt::MusicUnlock::factory_implementation
