#include "pch.h"

#include "Corners.h"

#include <algorithm>

#include <winrt/Microsoft.UI.Xaml.Hosting.h>

namespace corners {
namespace {

using winrt::Microsoft::UI::Xaml::Hosting::ElementCompositionPreview;

float clamped(float2 size, float radius) { return std::clamp(radius, 0.f, std::min(size.x, size.y) / 2.f); }

composition::CompositionRoundedRectangleGeometry rounded(Compositor const& compositor, float2 size, float radius) {
    auto geometry = compositor.CreateRoundedRectangleGeometry();
    geometry.Size(size);
    geometry.CornerRadius({clamped(size, radius), clamped(size, radius)});
    return geometry;
}

}  // namespace

CompositionGeometry geometry(Compositor const& compositor, float2 size, float radius) {
    return rounded(compositor, size, radius);
}

void clip(UIElement const& element, float radius) {
    auto visual = ElementCompositionPreview::GetElementVisual(element);
    auto compositor = visual.Compositor();
    auto clip = compositor.CreateGeometricClip();
    auto geometry = rounded(compositor, visual.Size(), radius);
    // Size follows the visual, so a layout change does not leave the clip behind.
    auto size = compositor.CreateExpressionAnimation(L"this.Target.Size");
    geometry.StartAnimation(L"Size", size);
    clip.Geometry(geometry);
    visual.Clip(clip);
}

ShapeVisual stroke(Compositor const& compositor, float2 size, float radius, Color color, float thickness) {
    auto visual = compositor.CreateShapeVisual();
    visual.Size(size);
    auto shape = compositor.CreateSpriteShape(rounded(compositor, size, radius));
    shape.StrokeBrush(compositor.CreateColorBrush(color));
    shape.StrokeThickness(thickness);
    shape.FillBrush(nullptr);
    visual.Shapes().Append(shape);
    return visual;
}

}  // namespace corners
