#pragma once
// 与 SwiftUI RoundedRectangle(style: .continuous) 控制点一致的连续圆角。
// 详见 ContinuousPath.h；保留 #59 确定的公共签名。

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

/// 元素坐标系中的连续圆角；半径超过短边一半时夹成圆端胶囊。
CompositionGeometry geometry(Compositor const& compositor, float2 size, float radius);

/// 裁剪元素自身及其内容，尺寸或半径改变时重建，同尺寸复用。
void clip(UIElement const& element, float radius);

/// 沿同形状内侧绘制描边。调用方把返回的视觉放在自己的图层上。
ShapeVisual stroke(Compositor const& compositor, float2 size, float radius, Color color, float thickness = 1.f);

}  // namespace corners
