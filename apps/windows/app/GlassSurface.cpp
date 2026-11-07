#include "pch.h"

#include "GlassSurface.h"
#if __has_include("GlassSurface.g.cpp")
#include "GlassSurface.g.cpp"
#endif

#include <algorithm>

#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

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
    // The default ContentControl template does not paint Background or BorderBrush. ThemeResource (not a
    // brush resolved in code) so the surface follows the element's requested theme, including previews.
    // Plate paints the surface. Tint sits over the fill and under the content, collapsed until a tint is set.
    const hstring markup =
        L"<ControlTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' TargetType='ContentControl'>"
        L"<Border x:Name='Plate' xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml'"
        L" Background='{ThemeResource MuPanelBrush}' BorderBrush='{ThemeResource MuSeparatorBrush}'"
        L" BorderThickness='{TemplateBinding BorderThickness}' CornerRadius='{TemplateBinding CornerRadius}'"
        L" Padding='{TemplateBinding Padding}'><Grid><Rectangle x:Name='Tint' Visibility='Collapsed'"
        L" Fill='{ThemeResource MuTextBrush}' RadiusX='20' RadiusY='20' />"
        L"<ContentPresenter /></Grid></Border></ControlTemplate>";
    Template(Markup::XamlReader::Load(markup).as<Controls::ControlTemplate>());
    BorderThickness(ThicknessHelper::FromUniformLength(1));
    // A panel is not a control of its own; only its content belongs in the tab order.
    IsTabStop(false);
    Loaded({this, &GlassSurface::OnLoaded});
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
    self->Apply();
}
void GlassSurface::OnGlassChanged(DependencyObject const& sender, DependencyPropertyChangedEventArgs const& args) {
    auto self = sender.as<GlassSurface>();
    self->on_glass_ = unbox_value<bool>(args.NewValue());
    self->Apply();
}

void GlassSurface::Apply() {
    if (!IsLoaded()) return;
    // Placeholder paints, matching MuPanelStyle until issue #60 supplies the material. Panel keeps the
    // translucent brush from the template; controls, menus and anything already on glass are opaque.
    if (auto plate = GetTemplateChild(L"Plate").try_as<Controls::Border>()) {
        if (kind_ != MusicUnlock::GlassKind::Panel || on_glass_) {
            plate.Background(Application::Current().Resources().TryLookup(box_value(L"MuSurfaceBrush")).try_as<Media::Brush>());
        }
    }

    // A negative radius is a capsule: half the laid-out height, the panel radius until the first layout.
    const double height = ActualHeight();
    const float radius = radius_ < 0 && height > 0 ? static_cast<float>(height / 2.0) : static_cast<float>(std::max(radius_, 0.0));
    CornerRadius(CornerRadiusHelper::FromRadii(radius, radius, radius, radius));

    if (kind_ == MusicUnlock::GlassKind::Menu && !on_glass_) {
        if (!menu_shadow_) menu_shadow_ = Media::ThemeShadow();
        Shadow(menu_shadow_);
        Microsoft::UI::Xaml::Hosting::ElementCompositionPreview::SetIsTranslationEnabled(*this, true);
        Translation({0, 0, 32});
    } else {
        Shadow(nullptr);
        Translation({0, 0, 0});
    }

    // The tint keeps the template's theme brush and only changes how much of it shows.
    if (auto tint = GetTemplateChild(L"Tint").try_as<Shapes::Rectangle>()) {
        tint.Opacity(std::max(tint_, 0.0));
        tint.RadiusX(radius);
        tint.RadiusY(radius);
        tint.Visibility(tint_ > 0 ? Visibility::Visible : Visibility::Collapsed);
    }
}

void GlassSurface::OnLoaded(IInspectable const&, RoutedEventArgs const&) { Apply(); }

}  // namespace winrt::MusicUnlock::implementation
