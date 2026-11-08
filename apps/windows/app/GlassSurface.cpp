#include "pch.h"

#include <winrt/MusicUnlock.h>
#include "GlassSurface.h"
#if __has_include("GlassSurface.g.cpp")
#include "GlassSurface.g.cpp"
#endif

#include <algorithm>

#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

namespace glass {

using namespace winrt;
using namespace Microsoft::UI;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;

// 预览器会把高对比度资源复制到祖先的 Light/Dark 字典，而不修改系统设置。
// 同时尊重这种局部资源覆盖，不能只依赖全局 AccessibilitySettings。
bool high_contrast(FrameworkElement const& element, bool system_contrast) {
    if (system_contrast) return true;
    if (!GetEnvironmentVariableW(L"MUSICUNLOCK_RENDER_DIR", nullptr, 0)) return false;
    for (DependencyObject node = element; node; node = VisualTreeHelper::GetParent(node)) {
        auto ancestor = node.try_as<FrameworkElement>();
        if (!ancestor) continue;
        auto themes = ancestor.Resources().ThemeDictionaries();
        auto light = themes.TryLookup(box_value(L"Light")).try_as<ResourceDictionary>();
        auto dark = themes.TryLookup(box_value(L"Dark")).try_as<ResourceDictionary>();
        if (!light || !dark) continue;
        auto light_brush = light.TryLookup(box_value(L"MuPanelBrush")).try_as<SolidColorBrush>();
        auto dark_brush = dark.TryLookup(box_value(L"MuPanelBrush")).try_as<SolidColorBrush>();
        if (light_brush && dark_brush && light_brush.Color() == dark_brush.Color() &&
            light_brush.Opacity() == 1 && dark_brush.Opacity() == 1) return true;
    }
    return false;
}

}  // namespace glass

namespace winrt::MusicUnlock::implementation {
namespace {

using namespace Microsoft::UI::Xaml;

}  // namespace

DependencyProperty GlassSurface::kind_property_ = DependencyProperty::Register(
    L"Kind", xaml_typename<MusicUnlock::GlassKind>(), xaml_typename<MusicUnlock::GlassSurface>(),
    PropertyMetadata{box_value(MusicUnlock::GlassKind::Panel), PropertyChangedCallback{&GlassSurface::OnKindChanged}});
DependencyProperty GlassSurface::radius_property_ = DependencyProperty::Register(
    L"Radius", xaml_typename<double>(), xaml_typename<MusicUnlock::GlassSurface>(),
    PropertyMetadata{box_value(20.0), PropertyChangedCallback{&GlassSurface::OnRadiusChanged}});
DependencyProperty GlassSurface::tint_property_ = DependencyProperty::Register(
    L"TintOpacity", xaml_typename<double>(), xaml_typename<MusicUnlock::GlassSurface>(),
    PropertyMetadata{box_value(0.0), PropertyChangedCallback{&GlassSurface::OnTintChanged}});
DependencyProperty GlassSurface::on_glass_property_ = DependencyProperty::Register(
    L"OnGlass", xaml_typename<bool>(), xaml_typename<MusicUnlock::GlassSurface>(),
    PropertyMetadata{box_value(false), PropertyChangedCallback{&GlassSurface::OnGlassChanged}});

GlassSurface::GlassSurface() {
    // 实色表面、按压着色和细描边在内容下方分层，不修改内部控件。画刷用 ThemeResource，跟随元素自己的主题和
    // 高对比度字典。
    const hstring markup =
        L"<ControlTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' TargetType='ContentControl'>"
        L"<Grid x:Name='Root' xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml'"
        L" Visibility='{Binding Content.Visibility, RelativeSource={RelativeSource TemplatedParent}, FallbackValue=Visible}'>"
        L"<Border x:Name='Plate' Background='{ThemeResource MuSurfaceBrush}' BorderBrush='{ThemeResource MuSeparatorBrush}'"
        L" BorderThickness='{TemplateBinding BorderThickness}' CornerRadius='{TemplateBinding CornerRadius}'"
        L"/><Border x:Name='Tint' IsHitTestVisible='False' Visibility='Collapsed'"
        L" Background='{ThemeResource MuTextBrush}' CornerRadius='{TemplateBinding CornerRadius}' />"
        L"<Border IsHitTestVisible='False' Background='{TemplateBinding Background}'"
        L" CornerRadius='{TemplateBinding CornerRadius}' />"
        L"<ContentPresenter Padding='{TemplateBinding Padding}'"
        L" HorizontalContentAlignment='Stretch' VerticalContentAlignment='Stretch'"
        L" CornerRadius='{TemplateBinding CornerRadius}' /></Grid></ControlTemplate>";
    Template(Markup::XamlReader::Load(markup).as<Controls::ControlTemplate>());
    BorderThickness(ThicknessHelper::FromUniformLength(1));
    // Press 会暂时替换 Background；非空的透明画刷让松开时能正确恢复。
    Background(Media::SolidColorBrush(Windows::UI::Color{0, 0, 0, 0}));
    HorizontalContentAlignment(HorizontalAlignment::Stretch);
    VerticalContentAlignment(VerticalAlignment::Stretch);
    // 面板本身不进入 Tab 顺序。
    IsTabStop(false);
    Loaded({this, &GlassSurface::OnLoaded});
    Unloaded({this, &GlassSurface::OnUnloaded});
    ActualThemeChanged([this](auto&&, auto&&) { Apply(); });
    // 胶囊半径需要随布局高度更新。
    SizeChanged([this](auto&&, auto&&) { if (radius_ < 0) Apply(); });
}

void GlassSurface::Kind(MusicUnlock::GlassKind value) { SetValue(kind_property_, box_value(value)); }
void GlassSurface::Radius(double value) { SetValue(radius_property_, box_value(value)); }
void GlassSurface::TintOpacity(double value) { SetValue(tint_property_, box_value(value)); }
void GlassSurface::OnGlass(bool value) { SetValue(on_glass_property_, box_value(value)); }

void GlassSurface::OnKindChanged(DependencyObject const& sender, DependencyPropertyChangedEventArgs const& args) {
    auto self = sender.as<GlassSurface>();
    self->kind_ = unbox_value<MusicUnlock::GlassKind>(args.NewValue());
    self->Apply();
}
void GlassSurface::OnRadiusChanged(DependencyObject const& sender, DependencyPropertyChangedEventArgs const& args) {
    auto self = sender.as<GlassSurface>();
    self->radius_ = unbox_value<double>(args.NewValue());
    self->Apply();
}
void GlassSurface::OnTintChanged(DependencyObject const& sender, DependencyPropertyChangedEventArgs const& args) {
    auto self = sender.as<GlassSurface>();
    self->tint_ = unbox_value<double>(args.NewValue());
    self->ApplyTint();
}
void GlassSurface::OnGlassChanged(DependencyObject const& sender, DependencyPropertyChangedEventArgs const& args) {
    auto self = sender.as<GlassSurface>();
    self->on_glass_ = unbox_value<bool>(args.NewValue());
    self->Apply();
}

void GlassSurface::Apply() {
    if (!IsLoaded()) return;
    const bool contrast = ::glass::high_contrast(*this, accessibility_.HighContrast());

    // 负半径表示胶囊，随布局高度重新计算。
    const double height = ActualHeight();
    const float radius = radius_ < 0 && height > 0 ? static_cast<float>(height / 2.0) : static_cast<float>(std::max(radius_, 0.0));
    CornerRadius(CornerRadiusHelper::FromRadii(radius, radius, radius, radius));
    // 表面、着色、描边和内容共用连续圆角裁剪；负半径明确要求正圆端胶囊。
    if (auto root = GetTemplateChild(L"Root").try_as<FrameworkElement>())
        MusicUnlock::Corners::SetRadius(root, radius_ < 0 ? radius + 1.f : radius);

    // 面板一层柔和投影，菜单浮得更高；卡片上的控件和高对比度不加投影。
    if ((kind_ == MusicUnlock::GlassKind::Menu || kind_ == MusicUnlock::GlassKind::Panel) && !on_glass_ && !contrast) {
        if (!shadow_) shadow_ = Media::ThemeShadow();
        Shadow(shadow_);
        Microsoft::UI::Xaml::Hosting::ElementCompositionPreview::SetIsTranslationEnabled(*this, true);
        // 只改变 Z，不覆盖现有动效持有的 X/Y Translation。
        auto translation = Translation();
        Translation({translation.x, translation.y, kind_ == MusicUnlock::GlassKind::Menu ? 32.f : 6.f});
    } else {
        Shadow(nullptr);
        auto translation = Translation();
        Translation({translation.x, translation.y, 0});
    }

    ApplyTint();
}

void GlassSurface::ApplyTint() {
    if (!IsLoaded()) return;
    // TintOpacity 逐帧更新，只改着色层。
    if (auto tint = GetTemplateChild(L"Tint").try_as<Controls::Border>()) {
        tint.Opacity(std::clamp(tint_, 0.0, 1.0));
        tint.Visibility(tint_ > 0 && !::glass::high_contrast(*this, accessibility_.HighContrast()) ? Visibility::Visible : Visibility::Collapsed);
    }
}

void GlassSurface::OnLoaded(IInspectable const&, RoutedEventArgs const&) { Apply(); }

void GlassSurface::OnUnloaded(IInspectable const&, RoutedEventArgs const&) {
    Shadow(nullptr);
    shadow_ = nullptr;
}

}  // namespace winrt::MusicUnlock::implementation
