#pragma once
// Continuous-curvature rounded rectangles. This is the placeholder: ordinary circular corners
// (CompositionRoundedRectangleGeometry). Issue #61 replaces the geometry with the superellipse
// without changing these signatures.

#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.UI.h>

namespace corners {

namespace composition = winrt::Microsoft::UI::Composition;
using composition::CompositionGeometry;
using composition::Compositor;
using composition::ShapeVisual;
using winrt::Microsoft::UI::Xaml::UIElement;
using winrt::Windows::Foundation::Numerics::float2;
using winrt::Windows::UI::Color;

/// Rounded rectangle in the element's coordinate space. radius is clamped to min(w, h) / 2,
/// which makes the shape a capsule with semicircular ends.
CompositionGeometry geometry(Compositor const& compositor, float2 size, float radius);

/// Clips the element's visual to that shape and keeps the clip in step with the element's size.
void clip(UIElement const& element, float radius);

/// A 1 px stroke of the same shape. Clipping cannot reshape a Border's stroke, so callers place
/// the returned visual on their own layer.
ShapeVisual stroke(Compositor const& compositor, float2 size, float radius, Color color, float thickness = 1.f);

}  // namespace corners
