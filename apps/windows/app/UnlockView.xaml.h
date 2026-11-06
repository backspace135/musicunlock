#pragma once

#include "UnlockView.g.h"

#include <functional>
#include <map>
#include <set>

#include "JobItem.h"
#include "Motion.h"
#include "musicunlock/queue.h"

namespace winrt::MusicUnlock::implementation {

struct UnlockView : UnlockViewT<UnlockView> {
    UnlockView() = default;
    void InitializeComponent();

    /// Preview renders: a neutral sample output folder instead of the user's Downloads path.
    void UsePreviewData();
    /// Fills the queue with the same sample jobs as the macOS preview renderer.
    void ShowSampleJobs();

    /// System picker for files, or folders that are imported recursively (Ctrl+O / Ctrl+Shift+O).
    void ChooseFiles(bool folders);
    /// Scans in the background, then adds supported files (drops, pickers, command line).
    fire_and_forget Import(std::vector<std::filesystem::path> paths);
    /// Converts the waiting jobs one at a time in the background (Ctrl+Enter).
    fire_and_forget Start();
    /// Stops after the current file (Ctrl+.).
    void Stop();
    bool Busy() const { return queue_.running(); }
    bool Idle() const { return imports_ == 0 && !queue_.running(); }
    /// Preview renders of a real conversion write here instead of the user's folder (and save it, so the
    /// render also checks the settings file in MUSICUNLOCK_DATA_DIR).
    void UseOutputDirectory(std::filesystem::path directory) {
        SetOutputDirectory(std::move(directory));
        SaveSettings();
    }

    void OnChooseFiles(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { ChooseFiles(false); }
    void OnChooseOutput(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnNamingChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void OnStart(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { Start(); }
    void OnStop(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { Stop(); }
    void OnRetry(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnOpenOutput(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    fire_and_forget OnAbout(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    /// Sets the implicit animations again; the system animation setting may have changed (window activation).
    void ApplyMotion();

    // Control overrides: the whole workspace is the drop target.
    void OnDragOver(Microsoft::UI::Xaml::DragEventArgs const& args);
    void OnDragLeave(Microsoft::UI::Xaml::DragEventArgs const&);
    void OnDrop(Microsoft::UI::Xaml::DragEventArgs const& args) { ReceiveDrop(args); }

    void OnFilterChanged(Microsoft::UI::Xaml::Controls::SelectorBar const&,
                         Microsoft::UI::Xaml::Controls::SelectorBarSelectionChangedEventArgs const&);
    void OnNoticeClosing(Microsoft::UI::Xaml::Controls::InfoBar const&,
                         Microsoft::UI::Xaml::Controls::InfoBarClosingEventArgs const& args);
    void OnClearCompleted(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnClearQueue(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnShowAll(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnRemoveJob(IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnRevealOutput(IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnContainerContentChanging(Microsoft::UI::Xaml::Controls::ListViewBase const&,
                                    Microsoft::UI::Xaml::Controls::ContainerContentChangingEventArgs const& args);

    /// "查找这首歌的歌词": the window switches to the lyrics workspace and searches for the title.
    void OnFindLyricsRequested(std::function<void(std::string)> handler) { find_lyrics_ = std::move(handler); }
    void OnFindLyrics(IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    void Refresh();
    void SyncItems();
    void ShowNotice(std::string text);
    void ShowDropTarget(bool shown);
    /// Animates the state glyph of a realized row.
    void PopState(MusicUnlock::JobItem const& item, bool bounce);

    void SetOutputDirectory(std::filesystem::path directory);
    HWND OwnerWindow() const;
    fire_and_forget ReceiveDrop(Microsoft::UI::Xaml::DragEventArgs args);

    void SaveSettings() const;

    mu::NamingPolicy naming_ = mu::NamingPolicy::original;
    bool persist_ = true;  // preview renders never touch the saved settings
    int imports_ = 0;  // scans in flight
    mu::AudioQueue queue_;
    mu::QueueFilter filter_ = mu::QueueFilter::all;
    std::string notice_;
    std::filesystem::path output_directory_;
    Windows::Foundation::Collections::IObservableVector<IInspectable> items_ =
        single_threaded_observable_vector<IInspectable>();
    std::map<uint64_t, com_ptr<JobItem>> item_cache_;
    std::function<void(std::string)> find_lyrics_;
    std::set<uint64_t> entering_;  // rows inserted since the last layout; they settle in once realized
    motion::Loop drift_;
    motion::Loop hop_;
};

}  // namespace winrt::MusicUnlock::implementation

namespace winrt::MusicUnlock::factory_implementation {

struct UnlockView : UnlockViewT<UnlockView, implementation::UnlockView> {};

}  // namespace winrt::MusicUnlock::factory_implementation
