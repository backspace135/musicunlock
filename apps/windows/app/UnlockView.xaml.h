#pragma once

#include "UnlockView.g.h"

#include <functional>
#include <map>
#include <set>

#include <winrt/Windows.UI.ViewManagement.h>

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
    /// Keyboard focus on the first row and the pointer over the second (focus and hover visuals).
    void PreviewKeyboard();
    /// Opens the context menu of a row.
    void PreviewRowMenu(uint32_t index);
    void PreviewDropTarget(bool shown) { ShowDropTarget(shown); }
    /// Motion capture: the banner's own show and hide path.
    void ShowNotice(std::string text);
    void DismissNotice() {
        notice_.clear();
        Refresh();
    }
    /// Stands in for the system text size (TextScaleFactor) while previews simulate large text; 0 ends it.
    void PreviewTextScale(double scale) {
        preview_text_scale_ = scale;
        ApplyTextScale();
    }
    /// "关于音匣与开源许可", not yet shown.
    Microsoft::UI::Xaml::Controls::ContentDialog AboutDialog();

    /// System picker for files, or folders that are imported recursively (Ctrl+O / Ctrl+Shift+O).
    void ChooseFiles(bool folders);
    /// Scans in the background, then adds supported files (drops, pickers, command line).
    fire_and_forget Import(std::vector<std::filesystem::path> paths);
    /// Converts the waiting jobs one at a time in the background (Ctrl+Enter).
    fire_and_forget Start();
    /// Motion capture only: marks a conversion as running and stopped, so the start icon and waveform animate
    /// without reading or writing any file.
    void StartForCapture();
    /// Stops after the current file (Ctrl+.).
    void Stop();
    bool Busy() const { return queue_.running(); }
    /// Called when the number of queued files changes, so the title bar can bounce its add-files icon.
    void OnQueueChanged(std::function<void()> handler) { queue_changed_ = std::move(handler); }
    bool Idle() const { return imports_ == 0 && !queue_.running(); }
    /// Preview renders of a real conversion write here instead of the user's folder (and save it, so the
    /// render also checks the settings file in MUSICUNLOCK_DATA_DIR).
    void UseOutputDirectory(std::filesystem::path directory) {
        SetOutputDirectory(std::move(directory));
        SaveSettings();
    }

    void OnChooseFiles(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { ChooseFiles(false); }
    void OnChooseOutput(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
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
    void OnRevealSource(IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnRevealOutput(IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnContainerContentChanging(Microsoft::UI::Xaml::Controls::ListViewBase const&,
                                    Microsoft::UI::Xaml::Controls::ContainerContentChangingEventArgs const& args);
    void OnJobListKeyDown(IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
    void OnQueueHeaderSizeChanged(IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&);
    void OnFooterSizeChanged(IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&) { FitFooter(); }

    /// "查找这首歌的歌词": the window switches to the lyrics workspace and searches for the title.
    void OnFindLyricsRequested(std::function<void(std::string)> handler) { find_lyrics_ = std::move(handler); }
    void OnFindLyrics(IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    void Refresh();
    void SyncItems();
    void ShowDropTarget(bool shown);
    void ApplyTextScale();
    void FitQueueHeader();
    /// The footer actions drop to their own line once they no longer fit beside the status (200 % text).
    void FitFooter();
    /// Speaks `text` through Narrator without moving focus (failures, notices).
    void Announce(hstring const& text);
    /// Animates the state glyph of a realized row.
    void PopState(MusicUnlock::JobItem const& item, bool bounce);
    bool RemoveJob(uint64_t id);
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
    std::size_t shown_jobs_ = 0;  // queue length last drawn
    std::function<void()> queue_changed_;  // the title bar bounces its add-files icon when the queue length changes
    bool shown_running_ = false;    // whether a conversion was on screen, so the start icon bounces at both ends
    std::filesystem::path output_directory_;
    Windows::Foundation::Collections::IObservableVector<IInspectable> items_ =
        single_threaded_observable_vector<IInspectable>();
    std::map<uint64_t, com_ptr<JobItem>> item_cache_;
    std::function<void(std::string)> find_lyrics_;
    std::set<uint64_t> entering_;  // rows inserted since the last layout; they settle in once realized
    std::map<IInspectable, bool> row_action_;  // per container: whether it last showed the reveal button
    hstring status_title_;
    motion::Loop drift_;
    motion::Loop hop_;
    motion::Wave wave_;
    Windows::UI::ViewManagement::UISettings ui_settings_;
    Windows::UI::ViewManagement::UISettings::TextScaleFactorChanged_revoker text_scale_changed_;
    double preview_text_scale_ = 0;
};

}  // namespace winrt::MusicUnlock::implementation

namespace winrt::MusicUnlock::factory_implementation {

struct UnlockView : UnlockViewT<UnlockView, implementation::UnlockView> {};

}  // namespace winrt::MusicUnlock::factory_implementation
