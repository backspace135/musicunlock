#pragma once

#include "UnlockView.g.h"

#include <map>

#include "JobItem.h"
#include "musicunlock/queue.h"

namespace winrt::MusicUnlock::implementation {

struct UnlockView : UnlockViewT<UnlockView> {
    UnlockView() = default;
    void InitializeComponent();

    /// Preview renders: a neutral sample output folder instead of the user's Downloads path.
    void UsePreviewData();
    /// Fills the queue with the same sample jobs as the macOS preview renderer.
    void ShowSampleJobs();

    void OnFilterChanged(Microsoft::UI::Xaml::Controls::SelectorBar const&,
                         Microsoft::UI::Xaml::Controls::SelectorBarSelectionChangedEventArgs const&);
    void OnNoticeClosed(Microsoft::UI::Xaml::Controls::InfoBar const&,
                        Microsoft::UI::Xaml::Controls::InfoBarClosedEventArgs const&);
    void OnClearCompleted(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnClearQueue(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnShowAll(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnRemoveJob(IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnRevealOutput(IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    void Refresh();
    void SyncItems();
    void ShowNotice(std::string text);
    void SetOutputDirectory(std::filesystem::path directory);

    mu::AudioQueue queue_;
    mu::QueueFilter filter_ = mu::QueueFilter::all;
    std::string notice_;
    bool importing_ = false;
    std::filesystem::path output_directory_;
    Windows::Foundation::Collections::IObservableVector<IInspectable> items_ =
        single_threaded_observable_vector<IInspectable>();
    std::map<uint64_t, com_ptr<JobItem>> item_cache_;
};

}  // namespace winrt::MusicUnlock::implementation

namespace winrt::MusicUnlock::factory_implementation {

struct UnlockView : UnlockViewT<UnlockView, implementation::UnlockView> {};

}  // namespace winrt::MusicUnlock::factory_implementation
