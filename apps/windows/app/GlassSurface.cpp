#include "pch.h"

#include "GlassSurface.h"
#if __has_include("GlassSurface.g.cpp")
#include "GlassSurface.g.cpp"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <vector>
#include <functional>
#include "GlassField.h"
#include "Motion.h"

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
using namespace Microsoft::UI::Composition;
using Hosting::ElementCompositionPreview;
using Windows::UI::Color;

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

// 同一视图只创建一层背景；面板持有所有权，最后一块面板卸载时移除宿主和计时器。
class Backdrop {
public:
    static std::shared_ptr<Backdrop> acquire(FrameworkElement const& surface) {
        Grid root{nullptr};
        for (DependencyObject node = surface; node; node = VisualTreeHelper::GetParent(node)) {
            if (auto view = node.try_as<UserControl>(); view &&
                (get_class_name(view) == L"MusicUnlock.UnlockView" || get_class_name(view) == L"MusicUnlock.LyricsView")) {
                root = view.Content().try_as<Grid>();
                break;
            }
        }
        if (!root) return {};
        static std::vector<std::weak_ptr<Backdrop>> fields;
        std::erase_if(fields, [](auto const& field) { return field.expired(); });
        for (auto const& field : fields) {
            if (auto existing = field.lock(); existing && existing->root_.get() == root) return existing;
        }
        auto field = std::shared_ptr<Backdrop>(new Backdrop(root));
        fields.push_back(field);
        field->timer_.Tick([weak = std::weak_ptr<Backdrop>(field)](auto&&, auto&&) {
            if (auto self = weak.lock()) self->update();
        });
        field->update();
        field->timer_.Start();
        return field;
    }

    ~Backdrop() {
        timer_.Stop();
        ElementCompositionPreview::SetElementChildVisual(host_, nullptr);
        if (auto root = root_.get()) {
            uint32_t index;
            if (root.Children().IndexOf(host_, index)) root.Children().RemoveAt(index);
        }
    }

    void watch(std::function<bool()> refresh) { refresh_.push_back(std::move(refresh)); }

private:
    explicit Backdrop(Grid const& root) : root_(root) {
        host_.IsHitTestVisible(false);
        Automation::AutomationProperties::SetAccessibilityView(host_, Automation::Peers::AccessibilityView::Raw);
        Grid::SetRowSpan(host_, std::max(1u, root.RowDefinitions().Size()));
        Grid::SetColumnSpan(host_, std::max(1u, root.ColumnDefinitions().Size()));
        root.Children().InsertAt(0, host_);
        auto compositor = ElementCompositionPreview::GetElementVisual(host_).Compositor();
        field_ = compositor.CreateContainerVisual();
        field_.RelativeSizeAdjustment({1, 1});
        field_.Clip(compositor.CreateInsetClip());
        for (auto& visual : blobs_) {
            visual = compositor.CreateSpriteVisual();
            visual.AnchorPoint({0.5f, 0.5f});
            auto brush = compositor.CreateRadialGradientBrush();
            brush.EllipseCenter({0.5f, 0.5f});
            brush.EllipseRadius({0.5f, 0.5f});
            brush.ColorStops().Append(compositor.CreateColorGradientStop(0, Color{255, 255, 255, 255}));
            brush.ColorStops().Append(compositor.CreateColorGradientStop(1, Color{0, 255, 255, 255}));
            visual.Brush(brush);
            field_.Children().InsertAtTop(visual);
        }
        ElementCompositionPreview::SetElementChildVisual(host_, field_);
        timer_ = root.DispatcherQueue().CreateTimer();
    }

    void update() {
        auto root = root_.get();
        if (!root || !root.XamlRoot()) return;
        const auto now = std::chrono::steady_clock::now();
        const double delta = std::chrono::duration<double>(now - last_).count();
        last_ = now;
        const bool contrast = high_contrast(root, accessibility_.HighContrast());
        const bool effects = settings_.AdvancedEffectsEnabled() && !contrast;
        const bool dark = root.ActualTheme() == ElementTheme::Dark;
        const bool preview = GetEnvironmentVariableW(L"MUSICUNLOCK_RENDER_DIR", nullptr, 0) != 0;
        bool visible = true;
        for (DependencyObject node = root; node; node = VisualTreeHelper::GetParent(node)) {
            if (auto element = node.try_as<UIElement>(); element && element.Visibility() != Visibility::Visible) visible = false;
        }
        const auto hwnd = GetWindowFromWindowId(root.XamlRoot().ContentIslandEnvironment().AppWindowId());
        const bool active = visible && GetForegroundWindow() == hwnd && !IsIconic(hwnd);
        const bool running = animate(active, motion::enabled(), effects, preview, contrast);
        // 暂停后只低频检查恢复条件，不写入视觉属性，也不累积暂停时长。
        timer_.Interval(std::chrono::milliseconds(running ? 33 : 250));
        const double time = clock_.advance(delta, was_running_ && running);
        was_running_ = running;
        const float width = static_cast<float>(root.ActualWidth());
        const float height = static_cast<float>(root.ActualHeight());
        if (!initialized_ || dark != dark_ || effects != effects_ || contrast != contrast_) {
            const auto color = contrast ? settings_.UIElementColor(Windows::UI::ViewManagement::UIElementType::Window) :
                (dark ? Color{255, 25, 25, 25} : Color{255, 242, 242, 242});
            host_.Background(SolidColorBrush(color));
            field_.IsVisible(effects);
            constexpr uint8_t greys[] = {255, 214, 232, 196, 246, 224};
            for (size_t i = 0; i < blobs_.size(); ++i) {
                const auto grey = static_cast<uint8_t>(dark ? greys[i] / 5 : greys[i]);
                auto stops = blobs_[i].Brush().as<CompositionRadialGradientBrush>().ColorStops();
                stops.GetAt(0).Color(Color{255, grey, grey, grey});
                stops.GetAt(1).Color(Color{0, grey, grey, grey});
            }
            dark_ = dark; effects_ = effects; contrast_ = contrast;
            std::erase_if(refresh_, [](auto const& refresh) { return !refresh(); });
        }
        if (!initialized_ || running || width != width_ || height != height_) {
            for (size_t i = 0; i < blobs_.size(); ++i) {
                const auto point = position(static_cast<int>(i), time);
                const float diameter = std::max(width, height) * (0.9f + 0.06f * static_cast<float>(i));
                if (!initialized_ || width != width_ || height != height_) blobs_[i].Size({diameter, diameter});
                blobs_[i].Offset({width * static_cast<float>(point.x), height * static_cast<float>(point.y), 0});
            }
            width_ = width; height_ = height;
        }
        initialized_ = true;
    }

    weak_ref<Grid> root_;
    Grid host_;
    ContainerVisual field_{nullptr};
    std::array<SpriteVisual, 6> blobs_{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    Microsoft::UI::Dispatching::DispatcherQueueTimer timer_{nullptr};
    Windows::UI::ViewManagement::UISettings settings_;
    Windows::UI::ViewManagement::AccessibilitySettings accessibility_;
    FieldClock clock_;
    std::vector<std::function<bool()>> refresh_;
    std::chrono::steady_clock::time_point last_ = std::chrono::steady_clock::now();
    bool initialized_ = false, was_running_ = false, dark_ = false, effects_ = false, contrast_ = false;
    float width_ = 0, height_ = 0;
};

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
    // 材质和按压着色在内容下方分层，不修改内部控件。
    const hstring markup =
        L"<ControlTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' TargetType='ContentControl'>"
        L"<Grid xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml'"
        L" Visibility='{Binding Content.Visibility, RelativeSource={RelativeSource TemplatedParent}, FallbackValue=Visible}'>"
        L"<Border x:Name='Plate'"
        L" BorderThickness='{TemplateBinding BorderThickness}' CornerRadius='{TemplateBinding CornerRadius}'"
        L"/><Border x:Name='Tint' IsHitTestVisible='False' Visibility='Collapsed'"
        L" Background='{ThemeResource MuTextBrush}' CornerRadius='{TemplateBinding CornerRadius}' />"
        L"<Border IsHitTestVisible='False' Background='{TemplateBinding Background}'"
        L" CornerRadius='{TemplateBinding CornerRadius}' />"
        L"<Border x:Name='Edge' IsHitTestVisible='False' BorderThickness='1'"
        L" CornerRadius='{TemplateBinding CornerRadius}'/>"
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
    const bool dark = ActualTheme() == ElementTheme::Dark;
    const bool contrast = ::glass::high_contrast(*this, accessibility_.HighContrast());
    const bool fallback = contrast || !settings_.AdvancedEffectsEnabled();
    const Windows::UI::Color color = contrast ? settings_.UIElementColor(Windows::UI::ViewManagement::UIElementType::Window) :
        (dark ? Windows::UI::Color{255, 40, 40, 40} : Windows::UI::Color{255, 250, 250, 250});
    if (auto plate = GetTemplateChild(L"Plate").try_as<Controls::Border>()) {
        if (kind_ == MusicUnlock::GlassKind::Panel && !on_glass_) {
            if (!acrylic_) {
                acrylic_ = Media::AcrylicBrush();
                acrylic_.TintOpacity(0.15);
                acrylic_.TintLuminosityOpacity(0.20);
                acrylic_.TintTransitionDuration(std::chrono::milliseconds(0));
            }
            acrylic_.TintColor(dark ? Windows::UI::Color{255, 40, 40, 40} : Windows::UI::Color{255, 255, 255, 255});
            acrylic_.FallbackColor(color);
            acrylic_.AlwaysUseFallback(fallback);
            plate.Background(acrylic_);
        } else {
            plate.Background(Media::SolidColorBrush(color));
            acrylic_ = nullptr;
        }
    }
    if (auto edge = GetTemplateChild(L"Edge").try_as<Controls::Border>()) {
        auto brush = Media::LinearGradientBrush();
        brush.StartPoint({0, 0});
        brush.EndPoint({0.3f, 1});
        Media::GradientStop top, bottom;
        top.Offset(0); bottom.Offset(1);
        top.Color(dark ? Windows::UI::Color{65, 255, 255, 255} : Windows::UI::Color{220, 255, 255, 255});
        bottom.Color(dark ? Windows::UI::Color{18, 255, 255, 255} : Windows::UI::Color{28, 0, 0, 0});
        brush.GradientStops().Append(top);
        brush.GradientStops().Append(bottom);
        edge.BorderBrush(contrast ? Media::Brush{Media::SolidColorBrush(settings_.UIElementColor(
            Windows::UI::ViewManagement::UIElementType::WindowText))} : Media::Brush{brush});
    }

    // 负半径表示胶囊，随布局高度重新计算。
    const double height = ActualHeight();
    const float radius = radius_ < 0 && height > 0 ? static_cast<float>(height / 2.0) : static_cast<float>(std::max(radius_, 0.0));
    CornerRadius(CornerRadiusHelper::FromRadii(radius, radius, radius, radius));

    if ((kind_ == MusicUnlock::GlassKind::Menu || kind_ == MusicUnlock::GlassKind::Panel) && !on_glass_ && !contrast) {
        if (!menu_shadow_) menu_shadow_ = Media::ThemeShadow();
        Shadow(menu_shadow_);
        Microsoft::UI::Xaml::Hosting::ElementCompositionPreview::SetIsTranslationEnabled(*this, true);
        // 只改变 Z，不覆盖现有动效持有的 X/Y Translation。
        auto translation = Translation();
        Translation({translation.x, translation.y, kind_ == MusicUnlock::GlassKind::Menu ? 32.f : 12.f});
    } else {
        Shadow(nullptr);
        auto translation = Translation();
        Translation({translation.x, translation.y, 0});
    }

    ApplyTint();
}

void GlassSurface::ApplyTint() {
    if (!IsLoaded()) return;
    // TintOpacity 的逐帧更新不重建材质和边缘画刷。
    if (auto tint = GetTemplateChild(L"Tint").try_as<Controls::Border>()) {
        tint.Opacity(std::clamp(tint_, 0.0, 1.0));
        tint.Visibility(tint_ > 0 && !::glass::high_contrast(*this, accessibility_.HighContrast()) ? Visibility::Visible : Visibility::Collapsed);
    }
}

void GlassSurface::OnLoaded(IInspectable const&, RoutedEventArgs const&) {
    if (!backdrop_) {
        backdrop_ = ::glass::Backdrop::acquire(*this);
        if (backdrop_) backdrop_->watch([weak = get_weak()] {
            if (auto self = weak.get(); self && self->IsLoaded()) { self->Apply(); return true; }
            return false;
        });
    }
    auto refresh = [weak = get_weak(), queue = DispatcherQueue()](auto&&, auto&&) {
        queue.TryEnqueue([weak] { if (auto self = weak.get()) self->Apply(); });
    };
    effects_changed_ = settings_.AdvancedEffectsEnabledChanged(auto_revoke, refresh);
    colors_changed_ = settings_.ColorValuesChanged(auto_revoke, refresh);
    Apply();
}

void GlassSurface::OnUnloaded(IInspectable const&, RoutedEventArgs const&) {
    effects_changed_.revoke();
    colors_changed_.revoke();
    backdrop_.reset();
    if (auto plate = GetTemplateChild(L"Plate").try_as<Controls::Border>()) plate.Background(nullptr);
    acrylic_ = nullptr;
    Shadow(nullptr);
    menu_shadow_ = nullptr;
}

}  // namespace winrt::MusicUnlock::implementation
