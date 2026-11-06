#include "pch.h"

#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <chrono>
#include <filesystem>
#include <fstream>

#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#include <wil/cppwinrt_helpers.h>

namespace winrt::MusicUnlock::implementation {

using namespace Microsoft::UI;
using namespace Microsoft::UI::Xaml;
using namespace std::chrono_literals;

namespace {

std::wstring environment(const wchar_t* name) {
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if (length == 0) return {};
    std::wstring value(length, L'\0');
    value.resize(GetEnvironmentVariableW(name, value.data(), length));
    return value;
}

}  // namespace

MainWindow::MainWindow() {
    InitializeComponent();
    ExtendsContentIntoTitleBar(true);
    SetTitleBar(AppTitleBar());
    SystemBackdrop(Media::MicaBackdrop());
    auto window = AppWindow();
    window.Resize({1060, 720});
    if (auto presenter = window.Presenter().try_as<Windowing::OverlappedPresenter>()) {
        presenter.PreferredMinimumWidth(880);
        presenter.PreferredMinimumHeight(600);
    }
    if (auto directory = environment(L"MUSICUNLOCK_RENDER_DIR"); !directory.empty()) {
        PreviewBackdrop().Visibility(Visibility::Visible);
        Root().Loaded([this, directory](auto&&, auto&&) { RenderPreviews(directory); });
    }
}

fire_and_forget MainWindow::RenderPreviews(std::wstring directory) {
    using namespace Windows::Graphics::Imaging;
    using namespace Windows::Storage::Streams;
    auto strong = get_strong();
    auto dispatcher = DispatcherQueue();
    std::filesystem::create_directories(directory);
    for (auto [theme, name] : {std::pair{ElementTheme::Light, L"light"}, std::pair{ElementTheme::Dark, L"dark"}}) {
        Root().RequestedTheme(theme);
        co_await resume_after(600ms);
        co_await wil::resume_foreground(dispatcher);
        Media::Imaging::RenderTargetBitmap bitmap;
        co_await bitmap.RenderAsync(Root());
        const IBuffer pixels = co_await bitmap.GetPixelsAsync();
        InMemoryRandomAccessStream stream;
        const BitmapEncoder encoder = co_await BitmapEncoder::CreateAsync(BitmapEncoder::PngEncoderId(), stream);
        encoder.SetPixelData(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied,
                             static_cast<uint32_t>(bitmap.PixelWidth()), static_cast<uint32_t>(bitmap.PixelHeight()), 96, 96,
                             array_view<const uint8_t>(pixels.data(), pixels.Length()));
        co_await encoder.FlushAsync();
        const auto size = static_cast<uint32_t>(stream.Size());
        Buffer png(size);
        stream.Seek(0);
        co_await stream.ReadAsync(png, size, InputStreamOptions::None);
        co_await wil::resume_foreground(dispatcher);
        std::ofstream(std::filesystem::path(directory) / (std::wstring(L"window-") + name + L".png"), std::ios::binary)
            .write(reinterpret_cast<const char*>(png.data()), png.Length());
    }
    Application::Current().Exit();
}

}  // namespace winrt::MusicUnlock::implementation
