#include "pch.h"

#include "LyricRow.h"
#if __has_include("LyricRow.g.cpp")
#include "LyricRow.g.cpp"
#endif

#include "musicunlock/lyric_workspace.h"

namespace winrt::MusicUnlock::implementation {

void LyricRow::Update(bool checked, bool selected, bool enabled) {
    if (checked == checked_ && selected == selected_ && enabled == enabled_) return;
    checked_ = checked;
    selected_ = selected;
    enabled_ = enabled;
    // An empty name tells bindings that every property may have changed.
    property_changed_(*this, Microsoft::UI::Xaml::Data::PropertyChangedEventArgs(L""));
}

hstring LyricRow::Title() const { return to_hstring(song_ ? song_->title : collection_->title); }

hstring LyricRow::Subtitle() const {
    return to_hstring(song_ ? mu::LyricWorkspace::song_artist(*song_) : collection_->subtitle);
}

hstring LyricRow::Album() const { return song_ ? to_hstring(song_->album) : hstring(); }
hstring LyricRow::Duration() const { return song_ ? to_hstring(mu::format_duration(song_->duration_ms)) : hstring(); }

hstring LyricRow::Glyph() const {
    if (!collection_) return {};
    return collection_->kind == mu::SearchKind::album ? L"\xE93C" : L"\xE8FD";  // MusicAlbum, BulletedList
}

}  // namespace winrt::MusicUnlock::implementation
