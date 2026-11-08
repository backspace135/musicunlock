#pragma once

#include "GlassSurface.g.h"

#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <memory>

namespace glass { class Backdrop; }

namespace winrt::MusicUnlock::implementation {

struct GlassSurface : GlassSurfaceT<GlassSurface> {
    GlassSurface();

    MusicUnlock::GlassKind Kind() const { return kind_; }
    void Kind(MusicUnlock::GlassKind value);
    double Radius() const { return radius_; }
    void Radius(double value);
    double TintOpacity() const { return tint_; }
    void TintOpacity(double value);
    bool OnGlass() const { return on_glass_; }
    void OnGlass(bool value);

    // XAML 属性赋值通过依赖属性，而不是上面的 setter。
    static Microsoft::UI::Xaml::DependencyProperty KindProperty() { return kind_property_; }
    static Microsoft::UI::Xaml::DependencyProperty RadiusProperty() { return radius_property_; }
    static Microsoft::UI::Xaml::DependencyProperty TintOpacityProperty() { return tint_property_; }
    static Microsoft::UI::Xaml::DependencyProperty OnGlassProperty() { return on_glass_property_; }

private:
    static Microsoft::UI::Xaml::DependencyProperty kind_property_;
    static Microsoft::UI::Xaml::DependencyProperty radius_property_;
    static Microsoft::UI::Xaml::DependencyProperty tint_property_;
    static Microsoft::UI::Xaml::DependencyProperty on_glass_property_;

    static void OnKindChanged(Microsoft::UI::Xaml::DependencyObject const& sender,
                              Microsoft::UI::Xaml::DependencyPropertyChangedEventArgs const& args);
    static void OnRadiusChanged(Microsoft::UI::Xaml::DependencyObject const& sender,
                                Microsoft::UI::Xaml::DependencyPropertyChangedEventArgs const& args);
    static void OnTintChanged(Microsoft::UI::Xaml::DependencyObject const& sender,
                              Microsoft::UI::Xaml::DependencyPropertyChangedEventArgs const& args);
    static void OnGlassChanged(Microsoft::UI::Xaml::DependencyObject const& sender,
                               Microsoft::UI::Xaml::DependencyPropertyChangedEventArgs const& args);

    void Apply();
    void ApplyTint();
    void OnLoaded(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnUnloaded(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    MusicUnlock::GlassKind kind_ = MusicUnlock::GlassKind::Panel;
    double radius_ = 20.0;
    double tint_ = 0.0;
    bool on_glass_ = false;
    Microsoft::UI::Xaml::Media::ThemeShadow menu_shadow_{nullptr};
    Microsoft::UI::Xaml::Media::AcrylicBrush acrylic_{nullptr};
    Windows::UI::ViewManagement::UISettings settings_;
    Windows::UI::ViewManagement::AccessibilitySettings accessibility_;
    Windows::UI::ViewManagement::UISettings::AdvancedEffectsEnabledChanged_revoker effects_changed_;
    Windows::UI::ViewManagement::UISettings::ColorValuesChanged_revoker colors_changed_;
    std::shared_ptr<::glass::Backdrop> backdrop_;
};

}  // namespace winrt::MusicUnlock::implementation

namespace winrt::MusicUnlock::factory_implementation {

struct GlassSurface : GlassSurfaceT<GlassSurface, implementation::GlassSurface> {};

}  // namespace winrt::MusicUnlock::factory_implementation
