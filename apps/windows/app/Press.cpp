#include "pch.h"

#include "Press.h"
#if __has_include("Press.g.cpp")
#include "Press.g.cpp"
#endif

#include "GlassMotion.h"
#include "Motion.h"

#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <windows.ui.xaml.interop.h>

namespace winrt::MusicUnlock::implementation {
namespace {

using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Input;
using winrt::Windows::Foundation::IInspectable;

struct PressState : implements<PressState, Windows::Foundation::IInspectable> {
    bool pressed = false;
    bool watching = false;
    Media::Brush resting{nullptr};
};

DependencyProperty state_property = DependencyProperty::RegisterAttached(
    L"PressState", winrt::xaml_typename<IInspectable>(), winrt::xaml_typename<UIElement>(), PropertyMetadata{nullptr});

PressState& state_of(UIElement const& element) {
    if (auto existing = element.GetValue(state_property).try_as<PressState>()) return *existing;
    auto created = make_self<PressState>();
    element.SetValue(state_property, created.as<IInspectable>());
    return *created;
}

/// The text colour at `opacity`, so a press darkens a light button and lightens a dark one.
Media::Brush tint(Media::Brush const& foreground, double opacity) {
    Media::SolidColorBrush brush;
    brush.Opacity(opacity);
    if (auto solid = foreground.try_as<Media::SolidColorBrush>()) brush.Color(solid.Color());
    return brush;
}

/// Toward the element's centre. With motion off the scale stays put and only the opacity changes.
void apply(UIElement const& element, bool pressed) {
    const double scale = unbox_value<double>(element.GetValue(Press::ScaleProperty()));
    const double glass = unbox_value<double>(element.GetValue(Press::GlassProperty()));
    const float target = pressed && motion::enabled() && scale > 0 ? static_cast<float>(scale) : 1.f;
    const auto size = element.ActualSize();
    element.CenterPoint({size.x / 2, size.y / 2, 0});
    const auto compositor = Media::CompositionTarget::GetCompositorForCurrentThread();
    if (auto animation = glass_motion::vector3(compositor, glass_motion::pressable, L"Scale", {target, target, 1})) {
        element.StartAnimation(animation);
    }
    element.Scale({target, target, 1});
    element.Opacity(pressed ? 0.8 : 1);

    // Glass and primary buttons deepen by `glass` instead of only fading. A control has one background, so the
    // tint replaces it for the press and the resting brush comes back on release.
    if (glass <= 0) return;
    auto control = element.try_as<Controls::Control>();
    if (!control) return;
    auto& state = state_of(element);
    if (pressed) {
        if (!state.resting) state.resting = control.Background();
        control.Background(tint(control.Foreground(), glass));
    } else if (state.resting) {
        control.Background(state.resting);
        state.resting = nullptr;
    }
}

void set_pressed(UIElement const& element, bool pressed) {
    auto& state = state_of(element);
    if (state.pressed == pressed) return;
    state.pressed = pressed;
    apply(element, pressed);
}

}  // namespace

DependencyProperty Press::scale_property_ = DependencyProperty::RegisterAttached(
    L"Scale", winrt::xaml_typename<double>(), winrt::xaml_typename<MusicUnlock::Press>(),
    PropertyMetadata{box_value(0.0), PropertyChangedCallback{&Press::OnScaleChanged}});
DependencyProperty Press::glass_property_ = DependencyProperty::RegisterAttached(
    L"Glass", winrt::xaml_typename<double>(), winrt::xaml_typename<MusicUnlock::Press>(), PropertyMetadata{box_value(0.0)});

double Press::GetScale(DependencyObject const& element) { return unbox_value<double>(element.GetValue(scale_property_)); }
void Press::SetScale(DependencyObject const& element, double value) { element.SetValue(scale_property_, box_value(value)); }
double Press::GetGlass(DependencyObject const& element) { return unbox_value<double>(element.GetValue(glass_property_)); }
void Press::SetGlass(DependencyObject const& element, double value) { element.SetValue(glass_property_, box_value(value)); }

void Press::OnScaleChanged(DependencyObject const& sender, DependencyPropertyChangedEventArgs const&) {
    auto element = sender.as<UIElement>();
    auto& state = state_of(element);
    if (state.watching) return;
    state.watching = true;
    element.PointerPressed([](IInspectable const& source, PointerRoutedEventArgs const& args) {
        auto element = source.as<UIElement>();
        element.CapturePointer(args.Pointer());
        set_pressed(element, true);
    });
    element.PointerReleased([](IInspectable const& source, PointerRoutedEventArgs const& args) {
        auto element = source.as<UIElement>();
        element.ReleasePointerCapture(args.Pointer());
        set_pressed(element, false);
    });
    element.PointerCaptureLost([](IInspectable const& source, PointerRoutedEventArgs const&) {
        set_pressed(source.as<UIElement>(), false);
    });
    element.PointerCanceled([](IInspectable const& source, PointerRoutedEventArgs const&) {
        set_pressed(source.as<UIElement>(), false);
    });
    element.KeyDown([](IInspectable const& source, KeyRoutedEventArgs const& args) {
        if (args.Key() == Windows::System::VirtualKey::Space || args.Key() == Windows::System::VirtualKey::Enter) {
            set_pressed(source.as<UIElement>(), true);
        }
    });
    element.KeyUp([](IInspectable const& source, KeyRoutedEventArgs const& args) {
        if (args.Key() == Windows::System::VirtualKey::Space || args.Key() == Windows::System::VirtualKey::Enter) {
            set_pressed(source.as<UIElement>(), false);
        }
    });
}

}  // namespace winrt::MusicUnlock::implementation
