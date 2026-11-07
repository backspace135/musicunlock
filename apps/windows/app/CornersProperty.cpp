#include "pch.h"

#include "CornersProperty.h"
#if __has_include("Corners.g.cpp")
#include "Corners.g.cpp"
#endif

#include "Corners.h"

#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

namespace winrt::MusicUnlock::implementation {
namespace {

using namespace Microsoft::UI::Xaml;
using Hosting::ElementCompositionPreview;

struct RadiusState : implements<RadiusState, Windows::Foundation::IInspectable> {
    float thickness = 0;
    bool prepared = false;
    Microsoft::UI::Composition::ShapeVisual outline{nullptr};
    Windows::UI::Color color{};
    float radius = -1;
    Windows::Foundation::Numerics::float2 size{};

    void apply(FrameworkElement const& element) {
        const float next_radius = static_cast<float>(MusicUnlock::Corners::GetRadius(element));
        corners::clip(element, next_radius);
        Media::Brush brush{nullptr};
        if (auto border = element.try_as<Controls::Border>()) {
            brush = border.BorderBrush();
            if (!prepared) {
                const auto edge = border.BorderThickness();
                thickness = static_cast<float>(edge.Left);
                const auto padding = border.Padding();
                // 移除原生描边的绘制，但保留原来的内容内缩和测量尺寸。
                border.Padding({padding.Left + edge.Left, padding.Top + edge.Top,
                                padding.Right + edge.Right, padding.Bottom + edge.Bottom});
                border.BorderThickness({});
            }
            if (border.CornerRadius() != CornerRadius{}) border.CornerRadius({});
        } else if (auto control = element.try_as<Controls::Control>()) {
            brush = control.BorderBrush();
            if (!prepared) {
                const auto edge = control.BorderThickness();
                thickness = static_cast<float>(edge.Left);
                const auto padding = control.Padding();
                control.Padding({padding.Left + edge.Left, padding.Top + edge.Top,
                                 padding.Right + edge.Right, padding.Bottom + edge.Bottom});
                control.BorderThickness({});
            }
            if (control.CornerRadius() != CornerRadius{}) control.CornerRadius({});
        } else if (auto shape = element.try_as<Shapes::Shape>()) {
            brush = shape.Stroke();
            if (!prepared) {
                thickness = static_cast<float>(shape.StrokeThickness());
                shape.StrokeThickness(0);
            }
        }
        prepared = true;
        const auto solid = brush.try_as<Media::SolidColorBrush>();
        const auto current = element.ActualSize();
        const auto next_color = solid ? solid.Color() : Windows::UI::Color{};
        if (outline && size.x == current.x && size.y == current.y && radius == next_radius &&
            color.A == next_color.A && color.R == next_color.R && color.G == next_color.G && color.B == next_color.B) return;
        if (!solid || thickness <= 0 || current.x <= 0 || current.y <= 0) {
            if (outline) {
                ElementCompositionPreview::SetElementChildVisual(element, nullptr);
                outline = nullptr;
            }
            return;
        }
        size = current;
        radius = next_radius;
        color = next_color;
        outline = corners::stroke(ElementCompositionPreview::GetElementVisual(element).Compositor(),
                                  size, radius, color, thickness);
        if (auto shape = element.try_as<Shapes::Shape>()) {
            const auto sprite = outline.Shapes().GetAt(0).as<Microsoft::UI::Composition::CompositionSpriteShape>();
            for (const auto dash : shape.StrokeDashArray()) sprite.StrokeDashArray().Append(static_cast<float>(dash));
        }
        // 路径只在边缘绘制且自身有裁剪，不再横穿内容或溢出到相邻元素。
        ElementCompositionPreview::SetElementChildVisual(element, outline);
    }
};

}  // namespace

DependencyProperty Corners::radius_property_ = DependencyProperty::RegisterAttached(
    L"Radius", xaml_typename<double>(), xaml_typename<MusicUnlock::Corners>(),
    PropertyMetadata{box_value(0.0), PropertyChangedCallback{&Corners::OnRadiusChanged}});

double Corners::GetRadius(UIElement const& element) { return unbox_value<double>(element.GetValue(radius_property_)); }

void Corners::SetRadius(UIElement const& element, double radius) { element.SetValue(radius_property_, box_value(radius)); }

void Corners::OnRadiusChanged(DependencyObject const& sender, DependencyPropertyChangedEventArgs const&) {
    static const auto state_property = DependencyProperty::RegisterAttached(
        L"RadiusState", xaml_typename<Windows::Foundation::IInspectable>(), xaml_typename<MusicUnlock::Corners>(),
        PropertyMetadata{nullptr});
    auto element = sender.try_as<FrameworkElement>();
    if (!element) return;
    auto stored = element.GetValue(state_property);
    if (!stored) {
        auto state = make_self<RadiusState>();
        stored = state.as<Windows::Foundation::IInspectable>();
        element.SetValue(state_property, stored);
        const auto update = [state](auto const& source, auto const&) { state->apply(source.template as<FrameworkElement>()); };
        element.Loaded(update);
        element.SizeChanged(update);
        element.ActualThemeChanged(update);
        // 模拟高对比度会替换画刷而不改变 ActualTheme；布局结束时只比较缓存，不重复建路径。
        element.LayoutUpdated([weak = make_weak(element), state](auto const&, auto const&) {
            if (auto current = weak.get(); current && current.IsLoaded()) state->apply(current);
        });
    }
    if (element.IsLoaded()) get_self<RadiusState>(stored)->apply(element);
}

}  // namespace winrt::MusicUnlock::implementation
