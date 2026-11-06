#include "pch.h"

#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

namespace winrt::MusicUnlock::implementation {

using namespace Microsoft::UI;
using namespace Microsoft::UI::Xaml;

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
}

}  // namespace winrt::MusicUnlock::implementation
