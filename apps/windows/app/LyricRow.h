#pragma once

#include "LyricRow.g.h"

#include <optional>

#include "musicunlock/lyrics.h"

namespace winrt::MusicUnlock::implementation {

struct LyricRow : LyricRowT<LyricRow> {
    explicit LyricRow(const mu::LyricSong& song) : song_(song), id_(to_hstring(song.id())) {}
    explicit LyricRow(const mu::MusicCollection& collection)
        : collection_(collection), id_(to_hstring(collection.id())) {}

    /// Updates the row state and notifies bindings when anything changed.
    void Update(bool checked, bool selected, bool enabled);

    hstring Id() const { return id_; }
    bool IsSong() const { return song_.has_value(); }
    bool IsCollection() const { return collection_.has_value(); }
    hstring Title() const;
    hstring Subtitle() const;
    hstring Album() const;
    hstring Duration() const;
    hstring Glyph() const;
    bool IsChecked() const { return checked_; }
    bool IsSelected() const { return selected_; }
    bool IsEnabled() const { return enabled_; }
    hstring CheckLabel() const { return L"选择 " + Title(); }
    hstring CheckAction() const { return checked_ ? L"取消选择" : L"加入批量导出"; }

    event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler) {
        return property_changed_.add(handler);
    }
    void PropertyChanged(event_token const& token) noexcept { property_changed_.remove(token); }

    const std::optional<mu::MusicCollection>& collection() const { return collection_; }

private:
    std::optional<mu::LyricSong> song_;
    std::optional<mu::MusicCollection> collection_;
    hstring id_;
    bool checked_ = false;
    bool selected_ = false;
    bool enabled_ = true;
    event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> property_changed_;
};

}  // namespace winrt::MusicUnlock::implementation
