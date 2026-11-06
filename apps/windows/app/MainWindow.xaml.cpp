#include "pch.h"

#include "MainWindow.xaml.h"

#include "Shell.h"
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

void MainWindow::InitializeComponent() {
    MainWindowT::InitializeComponent();
    ExtendsContentIntoTitleBar(true);
    SetTitleBar(AppTitleBar());
    SystemBackdrop(Media::MicaBackdrop());
    // AppWindow sizes are physical pixels; DESIGN.md sizes are effective pixels.
    HWND hwnd = nullptr;
    this->try_as<::IWindowNative>()->get_WindowHandle(&hwnd);
    const double scale = GetDpiForWindow(hwnd) / 96.0;
    auto pixels = [scale](int value) { return static_cast<int32_t>(value * scale + 0.5); };
    auto window = AppWindow();
    window.Resize({pixels(1060), pixels(720)});
    if (auto presenter = window.Presenter().try_as<Windowing::OverlappedPresenter>()) {
        presenter.PreferredMinimumWidth(pixels(880));
        presenter.PreferredMinimumHeight(pixels(600));
    }
    // Ctrl+. stops a batch. VK_OEM_PERIOD has no VirtualKey name, so it cannot be written in XAML.
    Xaml::Input::KeyboardAccelerator stop;
    stop.Modifiers(Windows::System::VirtualKeyModifiers::Control);
    stop.Key(static_cast<Windows::System::VirtualKey>(VK_OEM_PERIOD));
    stop.Invoked({this, &MainWindow::OnAcceleratorInvoked});
    Root().KeyboardAccelerators().Append(stop);
    Audio().Import(shell::command_line_paths());
    if (auto directory = environment(L"MUSICUNLOCK_RENDER_DIR"); !directory.empty()) {
        PreviewBackdrop().Visibility(Visibility::Visible);
        get_self<UnlockView>(AudioView())->UsePreviewData();
        Root().Loaded([this, directory](auto&&, auto&&) { RenderPreviews(directory); });
    }
}

void MainWindow::OnWorkspaceChanged(Controls::SelectorBar const&, Controls::SelectorBarSelectionChangedEventArgs const&) {
    const bool audio = WorkspaceBar().SelectedItem() == AudioItem();
    AudioView().Visibility(audio ? Visibility::Visible : Visibility::Collapsed);
    AudioActions().Visibility(audio ? Visibility::Visible : Visibility::Collapsed);
    LyricsPlaceholder().Visibility(audio ? Visibility::Collapsed : Visibility::Visible);
}

void MainWindow::ShowWorkspace(bool audio) { WorkspaceBar().SelectedItem(audio ? AudioItem() : LyricsItem()); }

void MainWindow::OnAddFiles(IInspectable const&, RoutedEventArgs const&) { Audio().ChooseFiles(false); }
void MainWindow::OnAddFolder(IInspectable const&, RoutedEventArgs const&) { Audio().ChooseFiles(true); }

void MainWindow::OnAcceleratorInvoked(Xaml::Input::KeyboardAccelerator const& accelerator,
                                      Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args) {
    args.Handled(true);
    const bool shift = (accelerator.Modifiers() & Windows::System::VirtualKeyModifiers::Shift) !=
                       Windows::System::VirtualKeyModifiers::None;
    switch (accelerator.Key()) {
        case Windows::System::VirtualKey::O:
            ShowWorkspace(true);
            Audio().ChooseFiles(shift);
            break;
        case Windows::System::VirtualKey::Enter:
            if (AudioView().Visibility() == Visibility::Visible) Audio().Start();
            break;
        case Windows::System::VirtualKey::Number1: ShowWorkspace(true); break;
        case Windows::System::VirtualKey::Number2: ShowWorkspace(false); break;
        default:  // Ctrl+.
            Audio().Stop();
            break;
    }
}

fire_and_forget MainWindow::RenderPreviews(std::wstring directory) {
    using namespace Windows::Graphics::Imaging;
    using namespace Windows::Storage::Streams;
    auto strong = get_strong();
    auto dispatcher = DispatcherQueue();
    std::filesystem::create_directories(directory);
    struct Shot {
        const wchar_t* name;
        ElementTheme theme;
    };
    // Same set as the macOS PreviewRenderer: empty and populated queue, light and dark.
    const Shot shots[] = {{L"empty-light", ElementTheme::Light}, {L"empty-dark", ElementTheme::Dark},
                          {L"queue-light", ElementTheme::Light}, {L"queue-dark", ElementTheme::Dark}};
    bool samples = false;
    for (const Shot shot : shots) {
        if (!samples && std::wstring_view(shot.name).starts_with(L"queue")) {
            get_self<UnlockView>(AudioView())->ShowSampleJobs();
            samples = true;
        }
        Root().RequestedTheme(shot.theme);
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
        std::ofstream(std::filesystem::path(directory) / (std::wstring(shot.name) + L".png"), std::ios::binary)
            .write(reinterpret_cast<const char*>(png.data()), png.Length());
    }
    Application::Current().Exit();
}

}  // namespace winrt::MusicUnlock::implementation
