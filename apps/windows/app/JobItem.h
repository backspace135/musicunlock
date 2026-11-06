#pragma once

#include "JobItem.g.h"

#include "musicunlock/queue.h"

namespace winrt::MusicUnlock::implementation {

struct JobItem : JobItemT<JobItem> {
    explicit JobItem(const mu::AudioJob& job) { Update(job); }

    /// Replaces the snapshot and notifies bindings when anything changed.
    void Update(const mu::AudioJob& job);

    uint64_t Id() const { return job_.id; }
    hstring Title() const;
    hstring SourceName() const;
    hstring Subtitle() const;
    hstring Format() const;
    hstring OutputFormat() const;
    bool HasOutputFormat() const { return !job_.output_extension.empty(); }
    hstring StateLabel() const;
    hstring StateGlyph() const;
    bool IsRunning() const { return job_.state == mu::JobState::running; }
    bool ShowsStateGlyph() const { return !IsRunning(); }
    bool IsFailed() const { return job_.state == mu::JobState::failed; }
    hstring Detail() const;
    bool HasOutput() const { return !job_.output.empty(); }
    bool CanRemove() const { return !HasOutput(); }
    bool HasCover() const { return cover_ != nullptr; }
    Microsoft::UI::Xaml::Media::ImageSource Cover() const { return cover_; }
    hstring RemoveLabel() const;
    hstring RevealLabel() const;
    /// IStringable: the row's name for Narrator (ListView reads the item's ToString).
    hstring ToString() const { return to_hstring(job_.spoken_summary()); }

    event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler) {
        return property_changed_.add(handler);
    }
    void PropertyChanged(event_token const& token) noexcept { property_changed_.remove(token); }

    const mu::AudioJob& job() const { return job_; }

private:
    mu::AudioJob job_;
    Microsoft::UI::Xaml::Media::ImageSource cover_{nullptr};
    event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> property_changed_;
};

}  // namespace winrt::MusicUnlock::implementation
