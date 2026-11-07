#include "pch.h"

#include "Corners.h"

#include "ContinuousPath.h"

#include <algorithm>

#include <d2d1.h>
#include <windows.graphics.interop.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>

namespace corners {
namespace {

using winrt::Microsoft::UI::Xaml::Hosting::ElementCompositionPreview;

/// 向合成器提供 Direct2D 几何，同时实现几何源标记接口与原生几何互操作接口。
struct GeometrySource : winrt::implements<GeometrySource, winrt::Windows::Graphics::IGeometrySource2D,
                                           ABI::Windows::Graphics::IGeometrySource2DInterop> {
    explicit GeometrySource(winrt::com_ptr<ID2D1Geometry> geometry) : geometry_(std::move(geometry)) {}

    IFACEMETHODIMP GetGeometry(ID2D1Geometry** value) noexcept override {
        geometry_.copy_to(value);
        return S_OK;
    }

    IFACEMETHODIMP TryGetGeometryUsingFactory(ID2D1Factory*, ID2D1Geometry**) noexcept override { return E_NOTIMPL; }

    winrt::com_ptr<ID2D1Geometry> geometry_;
};

winrt::com_ptr<ID2D1Factory> factory() {
    static winrt::com_ptr<ID2D1Factory> shared = [] {
        winrt::com_ptr<ID2D1Factory> created;
        winrt::check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, created.put()));
        return created;
    }();
    return shared;
}

/// 中心线内缩 inset，让描边落在填充形状内侧。
winrt::com_ptr<ID2D1PathGeometry> build(float width, float height, float radius, float inset) {
    const continuous::Path shape = continuous::path(width - 2.0 * inset, height - 2.0 * inset, radius - inset);
    winrt::com_ptr<ID2D1PathGeometry> geometry;
    winrt::check_hresult(factory()->CreatePathGeometry(geometry.put()));
    winrt::com_ptr<ID2D1GeometrySink> sink;
    winrt::check_hresult(geometry->Open(sink.put()));
    const auto point = [inset](continuous::Point value) {
        return D2D1::Point2F(static_cast<float>(value.x + inset), static_cast<float>(value.y + inset));
    };
    sink->BeginFigure(point(shape.start), D2D1_FIGURE_BEGIN_FILLED);
    for (std::size_t index = 0; index < shape.corners.size(); ++index) {
        // 每个角都是独立的三段曲线，角之间必须显式连直线。
        sink->AddLine(point(shape.corner_starts[index]));
        for (const continuous::Curve& curve : shape.corners[index]) {
            sink->AddBezier({point(curve.control1), point(curve.control2), point(curve.to)});
        }
    }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    winrt::check_hresult(sink->Close());
    return geometry;
}

composition::CompositionPathGeometry path_geometry(Compositor const& compositor, float2 size, float radius, float inset) {
    auto geometry = compositor.CreatePathGeometry();
    geometry.Path(composition::CompositionPath{winrt::make<GeometrySource>(build(size.x, size.y, radius, inset))});
    return geometry;
}

// 状态跟随元素释放，不用以强引用为键的全局表，避免列表复用后仍保留控件。
struct ClipState : winrt::implements<ClipState, winrt::Windows::Foundation::IInspectable> {
    float radius = -1;
    float applied_radius = -1;
    float2 size{};

    void apply(winrt::Microsoft::UI::Xaml::FrameworkElement const& element) {
        const auto current = element.ActualSize();
        if (current.x == size.x && current.y == size.y && radius == applied_radius) return;
        size = current;
        applied_radius = radius;
        auto visual = ElementCompositionPreview::GetElementVisual(element);
        visual.BorderMode(composition::CompositionBorderMode::Soft);
        if (size.x <= 0 || size.y <= 0) {
            visual.Clip(nullptr);
            return;
        }
        auto clip = visual.Compositor().CreateGeometricClip();
        clip.Geometry(path_geometry(visual.Compositor(), size, radius, 0.f));
        visual.Clip(clip);
    }
};

}  // namespace

CompositionGeometry geometry(Compositor const& compositor, float2 size, float radius) {
    return path_geometry(compositor, size, radius, 0.f);
}

void clip(UIElement const& element, float radius) {
    using namespace winrt::Microsoft::UI::Xaml;
    static const auto state_property = DependencyProperty::RegisterAttached(
        L"ClipState", winrt::xaml_typename<winrt::Windows::Foundation::IInspectable>(),
        winrt::xaml_typename<FrameworkElement>(), PropertyMetadata{nullptr});
    auto framework = element.try_as<FrameworkElement>();
    if (!framework) return;
    auto stored = framework.GetValue(state_property);
    if (!stored) {
        auto state = winrt::make_self<ClipState>();
        stored = state.as<winrt::Windows::Foundation::IInspectable>();
        framework.SetValue(state_property, stored);
        framework.SizeChanged([state](auto const& sender, auto const&) { state->apply(sender.template as<FrameworkElement>()); });
        framework.Loaded([state](auto const& sender, auto const&) { state->apply(sender.template as<FrameworkElement>()); });
    }
    auto state = winrt::get_self<ClipState>(stored);
    state->radius = std::max(radius, 0.f);
    state->apply(framework);
}

ShapeVisual stroke(Compositor const& compositor, float2 size, float radius, Color color, float thickness) {
    auto visual = compositor.CreateShapeVisual();
    visual.BorderMode(composition::CompositionBorderMode::Soft);
    visual.Size(size);
    // 中心线内缩半个线宽，并再次按外轮廓裁剪，保证细小圆角处也不会溢出。
    auto shape = compositor.CreateSpriteShape(path_geometry(compositor, size, radius, thickness / 2.f));
    shape.StrokeBrush(compositor.CreateColorBrush(color));
    shape.StrokeThickness(thickness);
    shape.FillBrush(nullptr);
    visual.Shapes().Append(shape);
    auto clip = compositor.CreateGeometricClip();
    clip.Geometry(geometry(compositor, size, radius));
    visual.Clip(clip);
    return visual;
}

}  // namespace corners
