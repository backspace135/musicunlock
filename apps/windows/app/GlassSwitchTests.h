#pragma once

#include "GlassSwitch.h"
#include "Motion.h"
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Provider.h>
#include <cmath>
#include <ostream>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>

namespace glass_switch_tests {

// 直接调用控件状态逻辑，不构造系统输入，不操作鼠标或用户桌面。
struct Access {
    static void tab_stops(winrt::MusicUnlock::GlassSwitch const& control) {
        using namespace winrt;
        auto self = get_self<MusicUnlock::implementation::GlassSwitch>(control);
        if (!control.IsTabStop()) throw hresult_error(E_FAIL, L"开关必须保留一个 Tab 停靠点");
        for (auto const& segment : self->segments_)
            if (segment.IsTabStop()) throw hresult_error(E_FAIL, L"分段不能额外加入 Tab 顺序");
    }
    static void press_shadow(winrt::MusicUnlock::GlassSwitch const& control) {
        using namespace winrt;
        auto self = get_self<MusicUnlock::implementation::GlassSwitch>(control);
        const bool original = control.OnGlass();
        control.OnGlass(true);
        motion::suspend(false);
        self->Press(true);
        if (!self->lens_shadow_.TryGetAnimationController(L"Opacity") ||
            !self->lens_shadow_.TryGetAnimationController(L"BlurRadius"))
            throw hresult_error(E_FAIL, L"按压阴影必须使用弹簧过渡，而非直接跳变");
        self->Press(false);
        motion::suspend(true);
        self->OnLayoutUpdated(nullptr, nullptr);
        if (self->lens_shadow_.TryGetAnimationController(L"Opacity") ||
            self->lens_shadow_.TryGetAnimationController(L"BlurRadius"))
            throw hresult_error(E_FAIL, L"减少动画后阴影弹簧必须停止");
        motion::suspend(false);
        control.OnGlass(original);
    }
    static void reduced_during_animation(winrt::MusicUnlock::GlassSwitch const& control) {
        using namespace winrt;
        using namespace Microsoft::UI::Xaml;
        motion::suspend(false);
        auto self = get_self<MusicUnlock::implementation::GlassSwitch>(control);
        const int32_t original = control.SelectedIndex();
        self->OnLayoutUpdated(nullptr, nullptr);
        control.SelectedIndex(original == 0 ? 1 : 0);
        auto press = self->GetTemplateChild(L"LensPress").as<FrameworkElement>();
        auto squash = self->GetTemplateChild(L"LensSquash").as<FrameworkElement>();
        press.Scale({1.1f, 1.1f, 1});
        squash.Scale({1.3f, 0.82f, 1});
        self->segments_.front().Scale({0.85f, 0.85f, 1});
        motion::suspend(true);
        self->OnLayoutUpdated(nullptr, nullptr);
        if (press.Scale().x != 1 || squash.Scale().x != 1 || self->segments_.front().Scale().x != 1)
            throw hresult_error(E_FAIL, L"切换减少动画后必须清除按压、挤压和图标缩放");
        if (self->lens_animation_ && self->lens_animation_.GetCurrentState() != Media::Animation::ClockState::Stopped)
            throw hresult_error(E_FAIL, L"切换减少动画后透镜动画仍在播放");
        control.SelectedIndex(original);
        motion::suspend(false);
    }
    static void hover(winrt::MusicUnlock::GlassSwitch const& control) {
        using namespace winrt;
        using namespace Microsoft::UI::Xaml::Media::Animation;
        auto self = get_self<MusicUnlock::implementation::GlassSwitch>(control);
        motion::suspend(false);
        self->hovered_ = 1;
        self->Restyle(true);
        if (self->color_animations_.empty()) throw hresult_error(E_FAIL, L"悬停缺少颜色过渡");
        const auto animation = self->color_animations_.front().Children().GetAt(0).as<ColorAnimation>();
        if (animation.Duration().TimeSpan != std::chrono::milliseconds(100) ||
            animation.EasingFunction().as<CubicEase>().EasingMode() != EasingMode::EaseOut)
            throw hresult_error(E_FAIL, L"悬停必须使用 100ms easeOut");
        motion::suspend(true);
        self->hovered_ = -1;
        self->Restyle(true);
        if (!self->color_animations_.empty()) throw hresult_error(E_FAIL, L"减少动画时不应保留悬停动画");
        motion::suspend(false);
    }
    static void reduced_press(winrt::MusicUnlock::GlassSwitch const& control) {
        using namespace winrt;
        auto self = get_self<MusicUnlock::implementation::GlassSwitch>(control);
        const bool original = control.OnGlass();
        control.OnGlass(true);
        motion::suspend(true);
        self->Press(true);
        if (std::abs(self->lens_shadow_.Opacity() - 0.16f) > 0.001f || self->lens_shadow_.BlurRadius() != 6.f)
            throw hresult_error(E_FAIL, L"减少动画时按压阴影未立即更新");
        self->Press(false);
        if (std::abs(self->lens_shadow_.Opacity() - 0.10f) > 0.001f || self->lens_shadow_.BlurRadius() != 2.f)
            throw hresult_error(E_FAIL, L"减少动画时松开阴影未立即恢复");
        motion::suspend(false);
        control.OnGlass(original);
    }
};

// 随应用自身离屏预览记录布局与 Composition 边界，不读取用户桌面。
inline void describe(winrt::Microsoft::UI::Xaml::DependencyObject const& node, std::ostream& out) {
    using namespace winrt;
    using namespace Microsoft::UI::Xaml;
    if (auto control = node.try_as<MusicUnlock::GlassSwitch>()) {
        out << to_string(control.Title()) << " selected=" << control.SelectedIndex() << '\n';
        const auto visit = [&](auto const& self, DependencyObject const& child) -> void {
            if (auto element = child.try_as<FrameworkElement>(); element && !element.Name().empty()) {
                const auto visual = Hosting::ElementCompositionPreview::GetElementVisual(element);
                const auto origin = element.TransformToVisual(control).TransformPoint({0, 0});
                if (element.Name() == L"LensHost" && element.Translation().x != 0)
                    throw hresult_error(E_FAIL, L"静态透镜位置不能仅依赖离屏渲染不更新的 Composition Translation");
                out << "  " << to_string(element.Name()) << " actual=" << element.ActualWidth() << ',' << element.ActualHeight()
                    << " origin=" << origin.X << ',' << origin.Y << " translation=" << element.Translation().x
                    << " visual=" << visual.Size().x << ',' << visual.Size().y;
                if (auto clip = visual.Clip().try_as<Microsoft::UI::Composition::CompositionGeometricClip>()) {
                    if (auto geometry = clip.Geometry().try_as<Microsoft::UI::Composition::CompositionRoundedRectangleGeometry>()) {
                        out << " clip=" << geometry.Size().x << ',' << geometry.Size().y;
                        if (std::abs(geometry.Size().x - visual.Size().x) > 0.5 ||
                            std::abs(geometry.Size().y - visual.Size().y) > 0.5)
                            throw hresult_error(E_FAIL, L"预览中剪裁尺寸与 Visual 尺寸不一致");
                    }
                }
                out << '\n';
            }
            for (int32_t i = 0; i < Media::VisualTreeHelper::GetChildrenCount(child); ++i)
                self(self, Media::VisualTreeHelper::GetChild(child, i));
        };
        visit(visit, control);
        return;
    }
    for (int32_t i = 0; i < Media::VisualTreeHelper::GetChildrenCount(node); ++i)
        describe(Media::VisualTreeHelper::GetChild(node, i), out);
}

inline void contrast_resources(winrt::MusicUnlock::GlassSwitch const& control) {
    using namespace winrt;
    using namespace Microsoft::UI::Xaml;
    ResourceDictionary theme;
    Media::SolidColorBrush ink(Windows::UI::Color{255, 17, 17, 17});
    theme.Insert(box_value(L"MuTextBrush"), ink);
    theme.Insert(box_value(L"MuSecondaryBrush"), ink);
    theme.Insert(box_value(L"MuSurfaceBrush"), Media::SolidColorBrush(Windows::UI::Color{255, 238, 238, 238}));
    const auto key = box_value(control.ActualTheme() == ElementTheme::Dark ? L"Dark" : L"Light");
    control.Resources().ThemeDictionaries().Insert(key, theme);
    // 触发重设样式，验证控件级资源优先于 Application 资源。
    control.OnGlass(!control.OnGlass());
    control.OnGlass(!control.OnGlass());
    const auto visit = [&](auto const& self, DependencyObject const& child) -> void {
        if (auto text = child.try_as<Controls::TextBlock>()) {
            if (auto brush = text.Foreground().try_as<Media::SolidColorBrush>(); !brush || brush.Color() != ink.Color())
                throw hresult_error(E_FAIL, L"开关未采用局部高对比度资源");
        }
        for (int32_t i = 0; i < Media::VisualTreeHelper::GetChildrenCount(child); ++i)
            self(self, Media::VisualTreeHelper::GetChild(child, i));
    };
    visit(visit, control);
    control.Resources().ThemeDictionaries().Remove(key);
    control.OnGlass(!control.OnGlass());
    control.OnGlass(!control.OnGlass());
}

inline winrt::Microsoft::UI::Xaml::FrameworkElement find(
    winrt::Microsoft::UI::Xaml::DependencyObject const& node, wchar_t const* name) {
    using namespace winrt::Microsoft::UI::Xaml;
    if (auto element = node.try_as<FrameworkElement>(); element && element.Name() == name) return element;
    for (int32_t i = 0; i < Media::VisualTreeHelper::GetChildrenCount(node); ++i) {
        if (auto found = find(Media::VisualTreeHelper::GetChild(node, i), name)) return found;
    }
    return nullptr;
}

inline void material(winrt::MusicUnlock::GlassSwitch const& control) {
    using namespace winrt;
    const auto track = find(control, L"TrackGlass").try_as<MusicUnlock::GlassSurface>();
    const auto lens = find(control, L"LensGlass").try_as<MusicUnlock::GlassSurface>();
    if (!track || !lens || track.Kind() != MusicUnlock::GlassKind::Control || lens.Kind() != MusicUnlock::GlassKind::Control)
        throw hresult_error(E_FAIL, L"轨道和透镜必须接入 GlassSurface Control");
    if (std::abs(lens.TintOpacity() - 0.08) > 0.00001) throw hresult_error(E_FAIL, L"透镜色调必须为 0.08");
    if (!find(control, L"LensPress") || !find(control, L"LensSquash"))
        throw hresult_error(E_FAIL, L"按压和拉伸必须使用独立层");
    if (!Microsoft::UI::Xaml::Hosting::ElementCompositionPreview::GetElementChildVisual(find(control, L"LensShadow")))
        throw hresult_error(E_FAIL, L"模板重建后阴影未接回新的透镜层");
    for (auto name : {L"TrackGlass", L"LensFill"}) {
        const auto element = find(control, name);
        if (element.ActualWidth() <= 0) continue;
        const auto visual = Microsoft::UI::Xaml::Hosting::ElementCompositionPreview::GetElementVisual(element);
        const auto clip = visual.Clip().try_as<Microsoft::UI::Composition::CompositionGeometricClip>();
        // 连续圆角是路径几何，没有 Size 可比；尺寸由 corners::clip 自己跟随 SizeChanged 维护。
        if (!clip || !clip.Geometry().try_as<Microsoft::UI::Composition::CompositionPathGeometry>())
            throw hresult_error(E_FAIL, L"轨道和透镜必须使用连续圆角裁剪");
    }
}

inline void layout(winrt::MusicUnlock::GlassSwitch const& control) {
    using namespace winrt;
    using namespace Microsoft::UI::Xaml;
    const auto panel = find(control, L"Segments").as<Controls::Grid>();
    const auto segment = panel.Children().GetAt(control.SelectedIndex()).as<Controls::Border>();
    const auto lens = find(control, L"LensHost");
    if (control.Fill()) {
        const auto width = panel.Children().GetAt(0).as<FrameworkElement>().ActualWidth();
        // Star 列按设备像素分配余数，相邻段允许相差一个物理像素。
        const double tolerance = 1.0 / control.XamlRoot().RasterizationScale() + 0.01;
        for (auto const& child : panel.Children())
            if (std::abs(child.as<FrameworkElement>().ActualWidth() - width) > tolerance)
                throw hresult_error(E_FAIL, L"Fill 模式的各段必须等宽：" + to_hstring(width) + L" / " +
                    to_hstring(child.as<FrameworkElement>().ActualWidth()));
    }
    const auto text = segment.Child().try_as<Controls::TextBlock>();
    const double original = text ? text.FontSize() : 0;
    if (text) text.FontSize(original * 2);
    control.UpdateLayout();
    if (std::abs(lens.Width() - segment.ActualWidth()) > 0.5 ||
        std::abs(lens.Height() - segment.ActualHeight()) > 0.5)
        throw hresult_error(E_FAIL, L"重布局后透镜尺寸未跟随选中段");
    const auto shape = find(control, L"LensFill").as<Shapes::Rectangle>();
    if (std::abs(shape.RadiusX() - lens.Height() / 2) > 0.5 || std::abs(shape.RadiusY() - lens.Height() / 2) > 0.5)
        throw hresult_error(E_FAIL, L"胶囊必须使用半高作为两个方向的相同半径");
    if (text) text.FontSize(original);
    control.UpdateLayout();
    const auto original_template = control.Template();
    control.Template(nullptr);
    control.ApplyTemplate();
    control.Template(original_template);
    control.ApplyTemplate();
    control.UpdateLayout();
    material(control);
}

inline void collection_state() {
    using namespace winrt;
    using namespace Microsoft::UI::Xaml;
    MusicUnlock::GlassSwitch control;
    const auto property = MusicUnlock::implementation::GlassSwitch::SelectedIndexProperty();
    const auto expect = [&](int32_t index) {
        if (control.SelectedIndex() != index || unbox_value<int32_t>(control.GetValue(property)) != index)
            throw hresult_error(E_FAIL, L"SelectedIndex 缓存、依赖属性与集合不一致");
    };
    expect(-1);
    control.Labels().ReplaceAll({L"第一段", L"第二段", L"第三段"});
    expect(0);
    control.SelectedIndex(99);
    expect(2);
    control.SelectedIndex(-9);
    expect(0);
    control.SelectedIndex(2);
    control.Labels().RemoveAtEnd();
    expect(1);
    control.Glyphs().ReplaceAll({L"\uE785", L"\uE8E4"});
    expect(1);
    control.Labels().Clear();
    expect(-1);
    control.SelectedIndex(4);
    expect(-1);
    control.Labels().Append(L"恢复段");
    expect(0);
}

// 在独立桌面的真实 WinUI 控件上测试，不使用输入注入或用户桌面。
inline void selection(winrt::MusicUnlock::GlassSwitch const& control) {
    using namespace winrt;
    using namespace Microsoft::UI::Xaml::Automation;
    Access::tab_stops(control);
    const auto require = [](bool condition, wchar_t const* message) {
        if (!condition) throw hresult_error(E_FAIL, message);
    };
    const auto peer = Peers::FrameworkElementAutomationPeer::CreatePeerForElement(control);
    const auto provider = peer.GetPattern(Peers::PatternInterface::Selection).try_as<Provider::ISelectionProvider>();
    require(static_cast<bool>(provider), L"GlassSwitch 缺少 Selection 模式");
    require(!provider.CanSelectMultiple() && provider.IsSelectionRequired(), L"开关必须保持单选");
    const auto children = peer.GetChildren();
    require(children && children.Size() == control.Labels().Size(), L"自动化子项数量不等于段数");
    const int32_t initial = control.SelectedIndex();
    for (uint32_t i = 0; i < children.Size(); ++i) {
        const auto child = children.GetAt(i);
        const auto item = child.GetPattern(Peers::PatternInterface::SelectionItem).try_as<Provider::ISelectionItemProvider>();
        require(static_cast<bool>(item), L"段缺少 SelectionItem 模式");
        require(child.GetName() == control.Labels().GetAt(i), L"段名称不匹配");
        require(child.GetPositionInSet() == static_cast<int32_t>(i + 1), L"段序号不匹配");
        require(child.GetSizeOfSet() == static_cast<int32_t>(children.Size()), L"段总数不匹配");
        require(static_cast<bool>(item.SelectionContainer()), L"段缺少选择容器");
        item.Select();
        require(control.SelectedIndex() == static_cast<int32_t>(i) && item.IsSelected(), L"自动化选择未生效");
        require(provider.GetSelection().size() == 1, L"Selection 必须返回一个选中项");
        item.RemoveFromSelection();
        require(item.IsSelected(), L"必选开关不能取消最后一个选中项");
    }
    control.SelectedIndex(initial);
}

}  // namespace glass_switch_tests
