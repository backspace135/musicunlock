#include "pch.h"

#include "JobItem.h"
#if __has_include("JobItem.g.cpp")
#include "JobItem.g.cpp"
#endif

#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>

#include <shcore.h>
#include <shlwapi.h>

#include "musicunlock/text.h"

namespace winrt::MusicUnlock::implementation {

namespace {

Microsoft::UI::Xaml::Media::ImageSource decode_cover(const mu::Bytes& bytes) {
    if (bytes.empty()) return nullptr;
    // Synchronous memory stream: the async writers would block the UI thread on .get().
    com_ptr<IStream> memory;
    memory.attach(SHCreateMemStream(bytes.data(), static_cast<UINT>(bytes.size())));
    if (!memory) return nullptr;
    Windows::Storage::Streams::IRandomAccessStream stream{nullptr};
    if (FAILED(CreateRandomAccessStreamOverStream(memory.get(), BSOS_DEFAULT,
                                                  guid_of<Windows::Storage::Streams::IRandomAccessStream>(),
                                                  put_abi(stream)))) {
        return nullptr;
    }
    Microsoft::UI::Xaml::Media::Imaging::BitmapImage image;
    image.DecodePixelWidth(80);  // rows show 36 x 40 at up to 200% scale
    try {
        image.SetSource(stream);
    } catch (hresult_error const&) {
        return nullptr;  // not an image the system can decode; keep the placeholder
    }
    return image;
}

bool same(const mu::AudioJob& a, const mu::AudioJob& b) {
    return a.state == b.state && a.output == b.output && a.output_extension == b.output_extension &&
           a.title == b.title && a.detail == b.detail && a.cover == b.cover && a.source == b.source;
}

}  // namespace

void JobItem::Update(const mu::AudioJob& job) {
    const bool changed = job_.id == 0 || !same(job_, job);
    const bool cover_changed = job_.cover != job.cover;
    job_ = job;
    if (cover_changed) cover_ = decode_cover(job_.cover);
    // An empty name tells bindings that every property may have changed.
    if (changed) property_changed_(*this, Microsoft::UI::Xaml::Data::PropertyChangedEventArgs(L""));
}

hstring JobItem::Title() const { return to_hstring(job_.display_title()); }
hstring JobItem::SourceName() const { return to_hstring(mu::path_to_utf8(job_.source.filename())); }
hstring JobItem::Subtitle() const { return to_hstring(job_.subtitle()); }
hstring JobItem::Format() const { return to_hstring(job_.source_format()); }

hstring JobItem::OutputFormat() const {
    std::string ext = job_.output_extension;
    for (char& c : ext) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
    }
    return to_hstring("↳ " + ext);
}

hstring JobItem::StateLabel() const { return to_hstring(mu::state_label(job_.state)); }

hstring JobItem::StateGlyph() const {
    switch (job_.state) {
        case mu::JobState::done: return L"\xEC61";    // CompletedSolid
        case mu::JobState::failed: return L"\xEB90";  // StatusErrorFull
        case mu::JobState::running: return L"\xE895";  // Sync
        case mu::JobState::waiting: break;
    }
    return L"\xEA3A";  // CircleRing
}

hstring JobItem::Detail() const { return IsFailed() ? to_hstring(job_.detail) : hstring(); }
hstring JobItem::RemoveLabel() const { return L"移出队列：" + SourceName(); }
hstring JobItem::RevealLabel() const {
    return L"在资源管理器中显示 " + to_hstring(mu::path_to_utf8(job_.output.filename()));
}

}  // namespace winrt::MusicUnlock::implementation
