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

    /// Opens a local LRC or TXT as an editable song (Ctrl+O in this workspace).
    fire_and_forget OpenLocal();
    /// Exports the selected song (Ctrl+S).
    void ExportCurrent() { Export(workspace_.export_current()); }
    /// Stops the batch after the current song; finished files stay (Ctrl+.).
    void StopExport();
    bool Busy() const { return workspace_.exporting(); }

    void OnProviderChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void OnKindChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void OnQueryChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
    void OnQueryKeyDown(IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
    void OnSearch(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { Search(workspace_.search()); }
    void OnCancelSearch(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnLoadMore(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { Search(workspace_.search(true)); }
    void OnNoticeClosed(Microsoft::UI::Xaml::Controls::InfoBar const&, Microsoft::UI::Xaml::Controls::InfoBarClosedEventArgs const&);
    void OnSelectAll(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnOpenLocal(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { OpenLocal(); }
    void OnResultClicked(IInspectable const&, Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args);
    void OnPreviewSong(IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnToggleSong(IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnRetryLyrics(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { LoadLyrics(workspace_.retry_lyrics()); }
    void OnTabChanged(Microsoft::UI::Xaml::Controls::SelectorBar const&,
                      Microsoft::UI::Xaml::Controls::SelectorBarSelectionChangedEventArgs const&);
    void OnEditorChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
    void OnFormatChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void OnCopy(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnOpenOutput(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnChooseOutput(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnExportCurrent(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { ExportCurrent(); }
    void OnExportChecked(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { Export(workspace_.export_checked()); }
    void OnStopExport(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { StopExport(); }
    fire_and_forget OnShowSettings(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnOptionSelected(IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&) { ReadOptions(); }
    void OnOptionTextChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&) { ReadOptions(); }
    void OnOptionToggled(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { ReadOptions(); }
    void OnOffsetChanged(Microsoft::UI::Xaml::Controls::NumberBox const&,
                         Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const&) {
        ReadOptions();
    }

private:
    void Refresh();
    void RefreshEditor();
    void RefreshFooter();
    void SyncRows();
    void ShowTab(mu::LyricTab tab);

    /// Runs a search or link resolution in the background (or shows why it cannot start).
    fire_and_forget Search(std::optional<mu::SearchRequest> request);
    fire_and_forget LoadLyrics(std::optional<mu::LyricsRequest> request);
    /// Exports the batch one song at a time in the background.
    fire_and_forget Export(std::optional<mu::ExportBatch> batch);
    void SetOptions(mu::LyricOptions options);
    /// Settings dialog: model to controls, and controls to model.
    void SyncOptions();
    void ReadOptions();
    void SaveSettings() const;
    HWND OwnerWindow() const;

    mu::LyricWorkspace workspace_;
    std::shared_ptr<const mu::MusicApi> api_;
    mu::CancelToken search_cancel_;
    mu::CancelToken lyrics_cancel_;
    mu::CancelToken export_cancel_;
    mu::LyricTab tab_ = mu::LyricTab::original;
    std::string shown_song_;  // the song the editor tabs were last reset for
    bool persist_ = true;     // preview renders never touch the saved settings
    bool syncing_ = false;    // controls are being updated from the model
    Windows::Foundation::Collections::IObservableVector<IInspectable> rows_ =
        single_threaded_observable_vector<IInspectable>();
    std::map<std::string, com_ptr<LyricRow>> row_cache_;
};

}  // namespace winrt::MusicUnlock::implementation

namespace winrt::MusicUnlock::factory_implementation {

struct LyricsView : LyricsViewT<LyricsView, implementation::LyricsView> {};

}  // namespace winrt::MusicUnlock::factory_implementation
