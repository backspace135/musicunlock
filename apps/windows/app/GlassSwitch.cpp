#include "pch.h"

#include "GlassSwitch.h"
#if __has_include("GlassSwitch.g.cpp")
#include "GlassSwitch.g.cpp"
#endif

#include "GlassMotion.h"
#include "Corners.h"
#include "GlassSwitchAutomationPeer.h"
#include "Motion.h"

#include <algorithm>
#include <limits>

#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Interop.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

namespace winrt::MusicUnlock::implementation {

using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Hosting;
using namespace Microsoft::UI::Xaml::Media;
using winrt::Windows::Foundation::Numerics::float3;
using namespace std::chrono_literals;

namespace {

constexpr double kIconWidth = 46.0;
constexpr double kIconHeight = 28.0;
constexpr double kTextHeight = 24.0;

Brush find_brush(ResourceDictionary const& resources, hstring const& theme, wchar_t const* key) {
    if (auto themed = resources.ThemeDictionaries().TryLookup(box_value(theme)).try_as<ResourceDictionary>()) {
        if (auto brush = themed.TryLookup(box_value(key)).try_as<Brush>()) return brush;
    }
    for (auto const& merged : resources.MergedDictionaries()) {
        if (auto brush = find_brush(merged, theme, key)) return brush;
    }
    return resources.TryLookup(box_value(key)).try_as<Brush>();
}

bool high_contrast() { return Windows::UI::ViewManagement::AccessibilitySettings().HighContrast(); }

bool transparency_reduced() { return !Windows::UI::ViewManagement::UISettings().AdvancedEffectsEnabled() || high_contrast(); }

bool preview_render() { return GetEnvironmentVariableW(L"MUSICUNLOCK_RENDER_DIR", nullptr, 0) > 0; }

// 无法显示玻璃时使用实心胶囊降级。
bool plain_lens() { return transparency_reduced() || preview_render(); }

// #59 的助手目前把后续关键帧写成 to * sample，会在非零起点时跳回零附近。
// 保留公共 lens / press 参数，在控件内以 StartingValue 为起点重写采样表达式，不改公共实现。
Microsoft::UI::Composition::CompositionAnimation retarget(
    Microsoft::UI::Composition::Compositor const& compositor, spring::Spring spring, wchar_t const* property, float3 to) {
    auto result = glass_motion::vector3(compositor, spring, property, to);
    if (!result) return nullptr;
    auto animation = result.as<Microsoft::UI::Composition::Vector3KeyFrameAnimation>();
    animation.SetVector3Parameter(L"destination", to);
    for (auto const& [progress, value] : spring::keyframes(spring)) {
        const auto expression = L"this.StartingValue + (destination - this.StartingValue) * " + std::to_wstring(value);
        animation.InsertExpressionKeyFrame(progress, expression);
    }
    return animation;
}

}  // namespace

DependencyProperty GlassSwitch::selected_property_ = DependencyProperty::Register(
    L"SelectedIndex", xaml_typename<int32_t>(), xaml_typename<MusicUnlock::GlassSwitch>(),
    PropertyMetadata{box_value(-1), PropertyChangedCallback{&GlassSwitch::OnSelectedChanged}});
DependencyProperty GlassSwitch::on_glass_property_ = DependencyProperty::Register(
    L"OnGlass", xaml_typename<bool>(), xaml_typename<MusicUnlock::GlassSwitch>(),
    PropertyMetadata{box_value(false), PropertyChangedCallback{&GlassSwitch::OnGlassChanged}});
DependencyProperty GlassSwitch::fill_property_ = DependencyProperty::Register(
    L"Fill", xaml_typename<bool>(), xaml_typename<MusicUnlock::GlassSwitch>(),
    PropertyMetadata{box_value(false), PropertyChangedCallback{&GlassSwitch::OnFillChanged}});
DependencyProperty GlassSwitch::title_property_ = DependencyProperty::Register(
    L"Title", xaml_typename<hstring>(), xaml_typename<MusicUnlock::GlassSwitch>(),
    PropertyMetadata{box_value(L""), PropertyChangedCallback{&GlassSwitch::OnTitleChanged}});

GlassSwitch::GlassSwitch() {
    DefaultStyleKey(box_value(L"MusicUnlock.GlassSwitch"));
    labels_ = single_threaded_observable_vector<hstring>();
    glyphs_ = single_threaded_observable_vector<hstring>();
    const auto changed = [weak = get_weak()](auto const& sender, auto const& args) {
        if (auto self = weak.get()) self->OnLabelsChanged(sender, args);
    };
    labels_.VectorChanged(changed);
    glyphs_.VectorChanged(changed);
    SizeChanged({this, &GlassSwitch::OnSized});
    ActualThemeChanged({this, &GlassSwitch::OnThemeChanged});
    LayoutUpdated({this, &GlassSwitch::OnLayoutUpdated});
    Padding(ThicknessHelper::FromUniformLength(3));
    IsTabStop(true);
    UseSystemFocusVisuals(true);
    motion_reduced_ = ReducedMotion();
    // 旧系统没有 IUISettings6；布局检查仍能在下一次交互时应用减少动画。
    if (ui_settings_.try_as<Windows::UI::ViewManagement::IUISettings6>()) {
        motion_event_ = ui_settings_.AnimationsEnabledChanged(auto_revoke,
            [weak = get_weak(), dispatcher = DispatcherQueue()](auto const&, auto const&) {
                dispatcher.TryEnqueue([weak] { if (auto self = weak.get()) self->RefreshMotion(); });
            });
    }
}

void GlassSwitch::SelectedIndex(int32_t value) { SetValue(selected_property_, box_value(value)); }
void GlassSwitch::OnGlass(bool value) { SetValue(on_glass_property_, box_value(value)); }
void GlassSwitch::Fill(bool value) { SetValue(fill_property_, box_value(value)); }
void GlassSwitch::Title(hstring const& value) { SetValue(title_property_, box_value(value)); }

void GlassSwitch::OnSelectedChanged(DependencyObject const& sender, DependencyPropertyChangedEventArgs const& args) {
    auto self = sender.as<GlassSwitch>();
    const auto requested = unbox_value<int32_t>(args.NewValue());
    const auto count = static_cast<int32_t>(self->labels_.Size());
    const auto next = count ? std::clamp(requested, 0, count - 1) : -1;
    if (requested != next) {
        self->SetValue(selected_property_, box_value(next));
        return;
    }
    if (next == self->selected_) return;
    const auto previous = self->selected_;
    self->selected_ = next;
    if (self->syncing_) return;
    self->Restyle();
    self->MoveLens(true);
    self->Squash();
    self->Bounce(next);
    Automation::AutomationProperties::SetName(*self, self->SpokenName());
    // 不在初始化或集合重建期间主动创建 UIA 树；只通知已连接的对等体。
    if (auto peer = Automation::Peers::FrameworkElementAutomationPeer::FromElement(*self)) {
        const auto children = peer.GetChildren();
        if (children) {
            if (previous >= 0 && previous < static_cast<int32_t>(children.Size()))
                children.GetAt(previous).RaisePropertyChangedEvent(
                    Automation::SelectionItemPatternIdentifiers::IsSelectedProperty(), box_value(true), box_value(false));
            if (next >= 0 && next < static_cast<int32_t>(children.Size())) {
                const auto child = children.GetAt(next);
                child.RaisePropertyChangedEvent(Automation::SelectionItemPatternIdentifiers::IsSelectedProperty(),
                                               box_value(false), box_value(true));
                child.RaiseAutomationEvent(Automation::Peers::AutomationEvents::SelectionItemPatternOnElementSelected);
            }
        }
    }
    self->selection_changed_(*self, nullptr);
}
void GlassSwitch::OnGlassChanged(DependencyObject const& sender, DependencyPropertyChangedEventArgs const&) {
    auto self = sender.as<GlassSwitch>();
    self->on_glass_ = unbox_value<bool>(sender.GetValue(on_glass_property_));
    self->Restyle();
}
void GlassSwitch::OnFillChanged(DependencyObject const& sender, DependencyPropertyChangedEventArgs const&) {
    auto self = sender.as<GlassSwitch>();
    self->fill_ = unbox_value<bool>(sender.GetValue(fill_property_));
    self->Rebuild();
}
void GlassSwitch::OnTitleChanged(DependencyObject const& sender, DependencyPropertyChangedEventArgs const&) {
    auto self = sender.as<GlassSwitch>();
    self->title_ = unbox_value<hstring>(sender.GetValue(title_property_));
    Automation::AutomationProperties::SetName(*self, self->SpokenName());
}

event_token GlassSwitch::SelectionChanged(
    Windows::Foundation::TypedEventHandler<MusicUnlock::GlassSwitch, IInspectable> const& handler) {
    return selection_changed_.add(handler);
}
void GlassSwitch::SelectionChanged(event_token const& token) noexcept { selection_changed_.remove(token); }

void GlassSwitch::OnApplyTemplate() {
    StopMotion();
    surface_events_.clear();
    for (auto const& animation : color_animations_) animation.Stop();
    color_animations_.clear();
    if (lens_animation_) lens_animation_.Stop();
    lens_animation_ = nullptr;
    lens_shadow_ = nullptr;
    shadow_visual_ = nullptr;
    shadow_opacity_ = shadow_blur_ = -1.f;
    segments_panel_ = GetTemplateChild(L"Segments").try_as<Grid>();
    lens_ = GetTemplateChild(L"LensHost").try_as<Grid>();
    lens_position_ = Media::TranslateTransform();
    if (lens_) lens_.RenderTransform(lens_position_);
    for (auto const* name : {L"TrackGlass", L"TrackFill", L"LensGlass", L"LensFill"}) {
        if (auto element = GetTemplateChild(name).try_as<FrameworkElement>())
            surface_events_.push_back(element.SizeChanged(auto_revoke, [weak = get_weak()](auto const& sender, auto const& args) {
                if (auto self = weak.get()) self->OnSurfaceSized(sender, args);
            }));
    }
    Rebuild();
}

Automation::Peers::AutomationPeer GlassSwitch::OnCreateAutomationPeer() {
    return make<GlassSwitchAutomationPeer>(static_cast<FrameworkElement>(*this));
}

void GlassSwitch::OnLabelsChanged(Windows::Foundation::Collections::IObservableVector<hstring> const&,
                                  Windows::Foundation::Collections::IVectorChangedEventArgs const&) {
    const auto count = static_cast<int32_t>(labels_.Size());
    SelectedIndex(count ? std::clamp(selected_, 0, count - 1) : -1);
    Rebuild();
}

void GlassSwitch::Rebuild() {
    if (!segments_panel_) return;
    syncing_ = true;
    segments_panel_.Children().Clear();
    segments_panel_.ColumnDefinitions().Clear();
    segments_.clear();
    segment_peers_ = single_threaded_vector<Automation::Peers::AutomationPeer>();

    const bool icons = glyphs_.Size() == labels_.Size() && labels_.Size() > 0;
    for (uint32_t i = 0; i < labels_.Size(); ++i) {
        ColumnDefinition column;
        column.Width(fill_ ? GridLengthHelper::FromValueAndType(1, GridUnitType::Star) : GridLengthHelper::Auto());
        segments_panel_.ColumnDefinitions().Append(column);

        // 段只由开关的 GetChildrenCore 提供对等体，不额外加入 Tab 顺序。
        Controls::Border segment;
        segment.Tag(box_value(static_cast<int32_t>(i)));
        segment.IsTabStop(false);
        segment.HorizontalAlignment(HorizontalAlignment::Stretch);
        segment.VerticalAlignment(VerticalAlignment::Center);
        Automation::AutomationProperties::SetName(segment, labels_.GetAt(i));
        if (icons) {
            segment.Width(kIconWidth);
            segment.Height(kIconHeight);
            FontIcon glyph;
            glyph.Glyph(glyphs_.GetAt(i));
            glyph.FontSize(14);
            glyph.HorizontalAlignment(HorizontalAlignment::Center);
            glyph.VerticalAlignment(VerticalAlignment::Center);
            segment.Child(glyph);
            Controls::ToolTipService::SetToolTip(segment, box_value(labels_.GetAt(i)));
        } else {
            segment.MinHeight(kTextHeight);
            segment.Padding(ThicknessHelper::FromLengths(12, 0, 12, 0));
            TextBlock text;
            text.Text(labels_.GetAt(i));
            text.FontSize(12);
            text.HorizontalAlignment(HorizontalAlignment::Center);
            text.VerticalAlignment(VerticalAlignment::Center);
            text.TextWrapping(TextWrapping::NoWrap);
            segment.Child(text);
        }
        Grid::SetColumn(segment, static_cast<int32_t>(i));
        segments_panel_.Children().Append(segment);
        segments_.push_back(segment);
        segment_peers_.Append(make<GlassSegmentAutomationPeer>(segment, static_cast<MusicUnlock::GlassSwitch>(*this),
                                                              static_cast<int32_t>(i)));
    }
    syncing_ = false;
    Restyle();
    UpdateLayout();
    MoveLens(false);
    Automation::AutomationProperties::SetName(*this, SpokenName());
}

hstring GlassSwitch::SegmentName(int32_t index) const {
    return index >= 0 && index < static_cast<int32_t>(labels_.Size()) ? labels_.GetAt(index) : hstring{};
}

Windows::Foundation::Collections::IVector<Automation::Peers::AutomationPeer> GlassSwitch::SegmentPeers() {
    if (!segment_peers_) segment_peers_ = single_threaded_vector<Automation::Peers::AutomationPeer>();
    return segment_peers_;
}

hstring GlassSwitch::SpokenName() const {
    hstring name = title_;
    const auto segment = SegmentName(selected_);
    if (!segment.empty()) {
        if (!name.empty()) name = name + L"，";
        name = name + segment;
    }
    return name;
}

void GlassSwitch::Restyle(bool animate_colors) {
    std::vector<Windows::UI::Color> current_colors;
    for (auto const& segment : segments_) {
        const auto child = segment.as<Controls::Border>().Child();
        Brush brush{nullptr};
        if (auto text = child.try_as<TextBlock>()) brush = text.Foreground();
        else if (auto glyph = child.try_as<FontIcon>()) brush = glyph.Foreground();
        auto solid = brush.try_as<SolidColorBrush>();
        current_colors.push_back(solid ? solid.Color() : Windows::UI::Color{});
    }
    for (auto const& animation : color_animations_) animation.Stop();
    color_animations_.clear();
    const hstring theme = high_contrast() ? L"HighContrast" : ActualTheme() == ElementTheme::Dark ? L"Dark" : L"Light";
    const auto theme_brush = [&](wchar_t const* key) {
        for (DependencyObject node = *this; node; node = VisualTreeHelper::GetParent(node)) {
            if (auto element = node.try_as<FrameworkElement>()) {
                if (auto brush = find_brush(element.Resources(), theme, key)) return brush;
            }
        }
        return find_brush(Application::Current().Resources(), theme, key);
    };
    for (int32_t i = 0; i < static_cast<int32_t>(segments_.size()); ++i) {
        auto segment = segments_[i].as<Controls::Border>();
        const bool active = i == selected_ || i == hovered_;
        auto brush = theme_brush(active ? L"MuTextBrush" : L"MuSecondaryBrush");
        if (auto target = brush.try_as<SolidColorBrush>(); animate_colors && !ReducedMotion() && target &&
            current_colors[i].A && current_colors[i] != target.Color()) {
            // 每段拥有自己的画刷，禁止动画修改共享主题画刷。
            SolidColorBrush local(target.Color());
            using namespace Media::Animation;
            ColorAnimation animation;
            animation.From(current_colors[i]);
            animation.To(target.Color());
            animation.Duration(DurationHelper::FromTimeSpan(100ms));
            CubicEase ease;
            ease.EasingMode(EasingMode::EaseOut);
            animation.EasingFunction(ease);
            Storyboard storyboard;
            Storyboard::SetTarget(animation, local);
            Storyboard::SetTargetProperty(animation, L"Color");
            storyboard.Children().Append(animation);
            storyboard.Begin();
            color_animations_.push_back(storyboard);
            brush = local;
        }
        const auto child = segment.Child();
        if (auto text = child.try_as<TextBlock>()) {
            text.Foreground(brush);
            text.FontWeight(i == selected_ ? Windows::UI::Text::FontWeights::SemiBold() : Windows::UI::Text::FontWeights::Normal());
        } else if (auto glyph = child.try_as<FontIcon>()) {
            glyph.Foreground(brush);
        }
    }
    if (auto glass = GetTemplateChild(L"TrackGlass").try_as<MusicUnlock::GlassSurface>()) {
        glass.Visibility(on_glass_ || plain_lens() ? Visibility::Collapsed : Visibility::Visible);
        // 只使用 #59 / #60 的公开接口，不替换材质实现。
        glass.Background(theme_brush(L"MuSurfaceBrush"));
    }
    if (auto plate = GetTemplateChild(L"TrackFill").try_as<Shapes::Rectangle>()) {
        plate.Visibility(on_glass_ || plain_lens() ? Visibility::Visible : Visibility::Collapsed);
        plate.Fill(theme_brush(on_glass_ ? L"MuTextBrush" : L"MuSurfaceBrush"));
        plate.Opacity(on_glass_ ? 0.06 : 1.0);
        plate.Stroke(on_glass_ ? nullptr : theme_brush(L"MuSeparatorBrush"));
        plate.StrokeThickness(on_glass_ ? 0 : 1);
    }
    if (auto glass = GetTemplateChild(L"LensGlass").try_as<MusicUnlock::GlassSurface>()) {
        glass.Visibility(on_glass_ || plain_lens() ? Visibility::Collapsed : Visibility::Visible);
    }
    if (auto lens = GetTemplateChild(L"LensFill").try_as<Shapes::Rectangle>()) {
        lens.Visibility(on_glass_ || plain_lens() ? Visibility::Visible : Visibility::Collapsed);
        // 文字始终是更高一层；实心透镜无需通过降低透明度避让文字。
        if (on_glass_ && !plain_lens()) {
            const bool dark = ActualTheme() == ElementTheme::Dark;
            Media::SolidColorBrush fill;
            fill.Color(dark ? Windows::UI::Color{255, 0x48, 0x48, 0x48} : Windows::UI::Color{255, 0xFF, 0xFF, 0xFF});
            lens.Fill(fill);
            lens.Opacity(1.0);
        } else {
            lens.Fill(theme_brush(L"MuTextBrush"));
            lens.Opacity(0.10);
        }
    }
    UpdateShadow();
}

Windows::Foundation::Rect GlassSwitch::SegmentBounds(int32_t index) const {
    const auto& segment = segments_[index];
    const auto parent = lens_ ? lens_.Parent().as<UIElement>() : UIElement{nullptr};
    const auto origin = segment.TransformToVisual(parent ? parent : UIElement{*this}).TransformPoint({0, 0});
    return {origin.X, origin.Y, static_cast<float>(segment.ActualWidth()), static_cast<float>(segment.ActualHeight())};
}

int32_t GlassSwitch::SegmentAt(double x) const {
    double best = std::numeric_limits<double>::infinity();
    int32_t index = std::max(selected_, 0);
    for (int32_t i = 0; i < static_cast<int32_t>(segments_.size()); ++i) {
        const auto& segment = segments_[i];
        const auto origin = segment.TransformToVisual(*this).TransformPoint({0, 0});
        const double left = origin.X;
        const double right = left + segment.ActualWidth();
        const double distance = x < left ? left - x : std::max(0.0, x - right);
        if (distance < best) {
            best = distance;
            index = i;
        }
    }
    return index;
}

bool GlassSwitch::ReducedMotion() const { return !motion::enabled(); }

void GlassSwitch::MoveLens(bool animate) {
    if (!lens_) return;
    const bool valid = selected_ >= 0 && selected_ < static_cast<int32_t>(segments_.size());
    lens_.Visibility(valid ? Visibility::Visible : Visibility::Collapsed);
    if (!valid) return;
    const auto bounds = SegmentBounds(selected_);
    if (bounds.Width < 1) return;
    lens_bounds_ = bounds;
    // XAML Transform 既供屏幕合成，也供 RenderTargetBitmap；从有效值续接中断动画。
    const double current_x = lens_position_.X();
    const double current_width = lens_.Width();
    if (lens_animation_) lens_animation_.Stop();
    lens_.Width(bounds.Width);
    lens_.Height(bounds.Height);
    lens_position_.X(bounds.X);
    if (!animate || ReducedMotion()) return;
    using namespace Media::Animation;
    lens_animation_ = Storyboard();
    const auto append = [&](DependencyObject const& target, wchar_t const* property, double from, double to) {
        if (!std::isfinite(from) || std::abs(from - to) < 0.1) return;
        DoubleAnimationUsingKeyFrames animation;
        animation.EnableDependentAnimation(true);
        const double duration = spring::settle_time(glass_motion::lens);
        for (auto const& [progress, sample] : spring::keyframes(glass_motion::lens)) {
            LinearDoubleKeyFrame frame;
            frame.KeyTime(KeyTimeHelper::FromTimeSpan(std::chrono::duration_cast<Windows::Foundation::TimeSpan>(
                std::chrono::duration<double>(progress * duration))));
            frame.Value(from + (to - from) * sample);
            animation.KeyFrames().Append(frame);
        }
        Storyboard::SetTarget(animation, target);
        Storyboard::SetTargetProperty(animation, property);
        lens_animation_.Children().Append(animation);
    };
    append(lens_position_, L"X", current_x, bounds.X);
    append(lens_, L"Width", current_width, bounds.Width);
    lens_animation_.Begin();
}

// 透镜内部的填充形状；按压和挤压各用独立缩放层。
FrameworkElement GlassSwitch::LensFill() const { return GetTemplateChild(L"LensFill").try_as<FrameworkElement>(); }

void GlassSwitch::ScaleLens(wchar_t const* layer, Microsoft::UI::Composition::CompositionAnimation const& animation) {
    auto fill = GetTemplateChild(layer).try_as<FrameworkElement>();
    if (!fill) return;
    fill.CenterPoint({static_cast<float>(lens_.ActualWidth() / 2), static_cast<float>(lens_.ActualHeight() / 2), 0});
    StartScale(fill, animation);
}

void GlassSwitch::StartScale(FrameworkElement const& element, Microsoft::UI::Composition::CompositionAnimation const& animation) {
    for (auto it = scale_animations_.begin(); it != scale_animations_.end();) {
        if (it->first == element) {
            element.StopAnimation(it->second);
            it = scale_animations_.erase(it);
        } else ++it;
    }
    element.StartAnimation(animation);
    scale_animations_.emplace_back(element, animation);
}

void GlassSwitch::StopMotion() {
    if (lens_shadow_) {
        lens_shadow_.StopAnimation(L"Opacity");
        lens_shadow_.StopAnimation(L"BlurRadius");
        shadow_opacity_ = shadow_blur_ = -1.f;
    }
    if (lens_animation_) lens_animation_.Stop();
    lens_animation_ = nullptr;
    for (auto const& animation : color_animations_) animation.Stop();
    color_animations_.clear();
    for (auto const& [element, animation] : scale_animations_) {
        element.StopAnimation(animation);
        element.Scale({1, 1, 1});
    }
    scale_animations_.clear();
    for (auto const* name : {L"LensPress", L"LensSquash"})
        if (auto element = GetTemplateChild(name).try_as<FrameworkElement>()) element.Scale({1, 1, 1});
    for (auto const& segment : segments_) segment.Scale({1, 1, 1});
}

void GlassSwitch::RefreshMotion() {
    const bool reduced = ReducedMotion();
    if (reduced == motion_reduced_) return;
    motion_reduced_ = reduced;
    if (!reduced) return;
    StopMotion();
    Restyle();
    MoveLens(false);
}

void GlassSwitch::Squash() {
    if (!lens_ || ReducedMotion()) return;
    // 位移弹簧负责移动，此层只负责沿途挤压、拉伸和回弹。
    auto compositor = ElementCompositionPreview::GetElementVisual(*this).Compositor();
    auto scale = compositor.CreateVector3KeyFrameAnimation();
    scale.Target(L"Scale");
    scale.InsertKeyFrame(0.f, {1.f, 1.f, 1.f});
    scale.InsertKeyFrame(0.09f / 0.35f, {1.3f, 0.82f, 1.f});
    scale.InsertKeyFrame(0.19f / 0.35f, {0.95f, 1.05f, 1.f});
    scale.InsertKeyFrame(1.f, {1.f, 1.f, 1.f});
    scale.Duration(350ms);
    motion::describe(L"GlassSwitch.squash");
    ScaleLens(L"LensSquash", scale);
}

void GlassSwitch::Press(bool pressed) {
    RefreshMotion();
    if (!lens_ || pressed_ == pressed) return;
    pressed_ = pressed;
    UpdateShadow(true);
    if (ReducedMotion()) return;
    auto compositor = ElementCompositionPreview::GetElementVisual(*this).Compositor();
    motion::describe(L"GlassSwitch.press");
    const float3 target = pressed ? float3{1.1f, 1.1f, 1.f} : float3{1.f, 1.f, 1.f};
    if (auto animation = retarget(compositor, glass_motion::press, L"Scale", target)) ScaleLens(L"LensPress", animation);
}

void GlassSwitch::UpdateShadow(bool animate) {
    if (!lens_) return;
    if (!shadow_visual_) {
        const auto compositor = ElementCompositionPreview::GetElementVisual(lens_).Compositor();
        shadow_visual_ = compositor.CreateSpriteVisual();
        shadow_visual_.RelativeSizeAdjustment({1, 1});
        lens_shadow_ = compositor.CreateDropShadow();
        lens_shadow_.Color(Windows::UI::Color{255, 0, 0, 0});
        lens_shadow_.Offset({0, 1, 0});
        shadow_visual_.Shadow(lens_shadow_);
        if (auto shape = LensFill().try_as<Shapes::Rectangle>()) lens_shadow_.Mask(shape.GetAlphaMask());
        ElementCompositionPreview::SetElementChildVisual(GetTemplateChild(L"LensShadow").as<UIElement>(), shadow_visual_);
    }
    const float opacity = on_glass_ && !high_contrast() ? (pressed_ ? 0.16f : 0.10f) : 0.f;
    const float blur = pressed_ ? 6.f : 2.f;
    if (opacity == shadow_opacity_ && blur == shadow_blur_ && !ReducedMotion()) return;
    shadow_opacity_ = opacity;
    shadow_blur_ = blur;
    if (animate && !ReducedMotion()) {
        const auto compositor = lens_shadow_.Compositor();
        for (auto const& [property, target] : {std::pair{L"Opacity", opacity}, std::pair{L"BlurRadius", blur}}) {
            auto animation = glass_motion::scalar(compositor, glass_motion::press, property, target)
                                 .as<Microsoft::UI::Composition::ScalarKeyFrameAnimation>();
            animation.SetScalarParameter(L"destination", target);
            for (auto const& [progress, sample] : spring::keyframes(glass_motion::press))
                animation.InsertExpressionKeyFrame(progress,
                    L"this.StartingValue + (destination - this.StartingValue) * " + std::to_wstring(sample));
            lens_shadow_.StartAnimation(property, animation);
        }
    } else {
        lens_shadow_.StopAnimation(L"Opacity");
        lens_shadow_.StopAnimation(L"BlurRadius");
        lens_shadow_.Opacity(opacity);
        lens_shadow_.BlurRadius(blur);
    }
}

void GlassSwitch::Bounce(int32_t index) {
    if (ReducedMotion() || index < 0 || index >= static_cast<int32_t>(segments_.size())) return;
    auto segment = segments_[index].as<Controls::Border>();
    if (!segment.Child().try_as<FontIcon>()) return;
    auto compositor = ElementCompositionPreview::GetElementVisual(*this).Compositor();
    auto scale = compositor.CreateVector3KeyFrameAnimation();
    scale.Target(L"Scale");
    scale.InsertKeyFrame(0.f, {1.f, 1.f, 1.f});
    const double duration = 0.08 + spring::settle_time(glass_motion::pressable);
    scale.InsertKeyFrame(static_cast<float>(0.08 / duration), {0.85f, 0.85f, 1.f},
                         compositor.CreateCubicBezierEasingFunction({0, 0}, {0.58f, 1}));
    for (auto const& [progress, sample] : spring::keyframes(glass_motion::pressable)) {
        const float value = 0.85f + 0.15f * sample;
        scale.InsertKeyFrame(static_cast<float>((0.08 + progress * (duration - 0.08)) / duration), {value, value, 1});
    }
    scale.Duration(std::chrono::duration_cast<Windows::Foundation::TimeSpan>(std::chrono::duration<double>(duration)));
    segment.CenterPoint({static_cast<float>(segment.ActualWidth() / 2), static_cast<float>(segment.ActualHeight() / 2), 0});
    StartScale(segment, scale);
}

void GlassSwitch::Select(int32_t index) {
    if (index < 0 || index >= static_cast<int32_t>(labels_.Size()) || index == selected_) return;
    SelectedIndex(index);
}

void GlassSwitch::OnPointerPressed(Input::PointerRoutedEventArgs const& args) {
    const auto point = args.GetCurrentPoint(*this);
    if (!point.Properties().IsLeftButtonPressed()) return;
    Focus(FocusState::Pointer);
    pointer_ = point.PointerId();
    CapturePointer(args.Pointer());
    Press(true);
    Select(SegmentAt(point.Position().X));
    args.Handled(true);
}

void GlassSwitch::OnPointerMoved(Input::PointerRoutedEventArgs const& args) {
    const auto point = args.GetCurrentPoint(*this);
    const auto hovered = SegmentAt(point.Position().X);
    if (hovered_ != hovered) {
        hovered_ = hovered;
        Restyle(true);
    }
    if (point.PointerId() != pointer_ || !point.Properties().IsLeftButtonPressed()) return;
    Select(SegmentAt(point.Position().X));
    args.Handled(true);
}

void GlassSwitch::OnPointerExited(Input::PointerRoutedEventArgs const&) {
    hovered_ = -1;
    Restyle(true);
}

void GlassSwitch::OnPointerReleased(Input::PointerRoutedEventArgs const& args) {
    if (args.GetCurrentPoint(*this).PointerId() != pointer_) return;
    pointer_ = 0;
    ReleasePointerCapture(args.Pointer());
    Press(false);
    args.Handled(true);
}

void GlassSwitch::OnPointerCaptureLost(Input::PointerRoutedEventArgs const&) {
    pointer_ = 0;
    Press(false);
}

void GlassSwitch::OnKeyDown(Input::KeyRoutedEventArgs const& args) {
    const auto key = args.Key();
    const int32_t last = static_cast<int32_t>(labels_.Size()) - 1;
    if (last < 0) return;
    int32_t next = selected_;
    if (key == Windows::System::VirtualKey::Left) next = std::max(0, selected_ - 1);
    else if (key == Windows::System::VirtualKey::Right) next = std::min(last, selected_ + 1);
    else if (key == Windows::System::VirtualKey::Home) next = 0;
    else if (key == Windows::System::VirtualKey::End) next = last;
    else return;
    Select(next);
    args.Handled(true);
}

void GlassSwitch::OnSized(IInspectable const&, SizeChangedEventArgs const&) {
    MoveLens(false);
}

void GlassSwitch::OnSurfaceSized(IInspectable const& sender, SizeChangedEventArgs const&) {
    const auto element = sender.as<FrameworkElement>();
    const double radius = element.ActualHeight() / 2;
    if (radius <= 0) return;
    // Rectangle 的两个半径分别受宽高约束，写死大值会成为椭圆，而不是胶囊。
    if (auto shape = element.try_as<Shapes::Rectangle>()) {
        shape.RadiusX(radius);
        shape.RadiusY(radius);
    } else if (auto surface = element.try_as<MusicUnlock::GlassSurface>()) {
        surface.CornerRadius(CornerRadiusHelper::FromUniformRadius(radius));
    }
    // 连续圆角裁剪自己跟随 SizeChanged 更新尺寸，这里只更新半径。
    corners::clip(element, static_cast<float>(radius));
}

void GlassSwitch::OnLayoutUpdated(IInspectable const&, IInspectable const&) {
    RefreshMotion();
    if (!lens_ || selected_ < 0 || selected_ >= static_cast<int32_t>(segments_.size())) return;
    const auto bounds = SegmentBounds(selected_);
    // 外层尺寸不变时，字体放大、段宽变化仍可能改变透镜。只在几何变化时更新，避免打断选择动画。
    if (bounds.X != lens_bounds_.X || bounds.Y != lens_bounds_.Y ||
        bounds.Width != lens_bounds_.Width || bounds.Height != lens_bounds_.Height)
        MoveLens(false);
}

void GlassSwitch::OnThemeChanged(FrameworkElement const&, IInspectable const&) { Restyle(); }

}  // namespace winrt::MusicUnlock::implementation
