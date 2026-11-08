#include "pch.h"

#include "MorphGroup.h"
#if __has_include("MorphGroup.g.cpp")
#include "MorphGroup.g.cpp"
#endif

#include "GlassMotion.h"
#include "Motion.h"

#include <winrt/Windows.Foundation.Numerics.h>

#include <cmath>

#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <windows.ui.xaml.interop.h>

namespace winrt::MusicUnlock::implementation {
namespace {

using namespace Microsoft::UI::Xaml;
using winrt::Windows::Foundation::Numerics::float3;

bool shown(UIElement const& element) { return element.Visibility() == Visibility::Visible; }

/// The element's natural width, measured if it has not been laid out yet.
float width_of(FrameworkElement const& element) {
    if (element.ActualWidth() > 0) return static_cast<float>(element.ActualWidth());
    element.Measure({std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()});
    return element.DesiredSize().Width;
}

/// Springs a scalar visual property. Assigns it directly when motion is off.
void spring(UIElement const& element, wchar_t const* property, float to) {
    const auto compositor = Media::CompositionTarget::GetCompositorForCurrentThread();
    if (auto animation = glass_motion::scalar(compositor, glass_motion::footer, property, to)) element.StartAnimation(animation);
}

}  // namespace

DependencyProperty MorphGroup::shared_property_ = DependencyProperty::Register(
    L"Shared", winrt::xaml_typename<UIElement>(), winrt::xaml_typename<MusicUnlock::MorphGroup>(),
    PropertyMetadata{nullptr, PropertyChangedCallback{&MorphGroup::OnSharedChanged}});

MorphGroup::MorphGroup() {
    Orientation(Controls::Orientation::Horizontal);
    Spacing(12);
    VerticalAlignment(VerticalAlignment::Center);
}

void MorphGroup::Shared(UIElement const& value) { SetValue(shared_property_, value); }

void MorphGroup::OnSharedChanged(DependencyObject const& sender, DependencyPropertyChangedEventArgs const&) {
    sender.as<MorphGroup>()->shared_ = sender.as<MorphGroup>()->Shared();
}

/// Called by the views after they change a child's Visibility, since that raises no callback of its own.
void MorphGroup::Update() {
    const bool first = !arranged_;
    arranged_ = true;
    UIElement primary{nullptr};
    for (auto const& child : Children()) primary = child.as<UIElement>();
    if (!primary) return;

    for (auto const& child : Children()) {
        auto element = child.as<UIElement>();
        if (element == primary || element == shared_) continue;
        const bool visible = shown(element);
        const bool was = seen_.contains(element) ? seen_[element] : visible;
        seen_[element] = visible;
        if (first || visible == was) continue;
        // The child keeps its layout slot either way, so the others never jump. It travels between its own place
        // and the primary's, scaled down, and only then gives the slot up — by staying collapsed when hidden.
        element.UpdateLayout();
        const float travel = width_of(element.as<FrameworkElement>()) + static_cast<float>(Spacing());
        const auto compositor = Media::CompositionTarget::GetCompositorForCurrentThread();
        const auto centre = element.ActualSize();
        element.CenterPoint({centre.x / 2, centre.y / 2, 0});
        if (visible) {
            element.Translation({travel, 0, 0});
            element.Scale({0.9f, 0.9f, 1});
            element.Opacity(0);
        }
        const float3 target_move = visible ? float3{0, 0, 0} : float3{travel, 0, 0};
        const float3 target_scale = visible ? float3{1, 1, 1} : float3{0.9f, 0.9f, 1};
        if (auto move = glass_motion::vector3(compositor, glass_motion::footer, L"Translation", target_move)) {
            element.StartAnimation(move);
        }
        if (auto grow = glass_motion::vector3(compositor, glass_motion::footer, L"Scale", target_scale)) element.StartAnimation(grow);
        spring(element, L"Opacity", visible ? 1.f : 0.f);
        if (!motion::enabled()) {
            element.Translation(target_move);
            element.Scale(target_scale);
            element.Opacity(visible ? 1 : 0);
        }
    }

    // One slot, two contents: the width springs from the old content to the new and the two cross-fade.
    if (!shared_) return;
    auto primary_element = primary.as<FrameworkElement>();
    auto shared_element = shared_.as<FrameworkElement>();
    const bool sharing = shown(shared_);
    const float resting = sharing ? width_of(shared_element) : width_of(primary_element);
    if (resting <= 0) return;
    if (!first && std::fabs(resting - slot_width_) > 0.5) {
        Width(slot_width_);
        const auto compositor = Media::CompositionTarget::GetCompositorForCurrentThread();
        (void)compositor;
        // Width is layout, so it steps; the visual scale covers the difference and settles as the layout catches up.
        const float factor = static_cast<float>(slot_width_ / resting);
        Scale({factor, 1, 1});
        if (auto settle = glass_motion::vector3(Media::CompositionTarget::GetCompositorForCurrentThread(), glass_motion::footer,
                                                L"Scale", {1, 1, 1})) {
            StartAnimation(settle);
        }
    }
    Width(resting);
    slot_width_ = resting;
}

}  // namespace winrt::MusicUnlock::implementation
