#pragma once

#include "Corners.g.h"

namespace winrt::MusicUnlock::implementation {

struct Corners {
    static Microsoft::UI::Xaml::DependencyProperty RadiusProperty() { return radius_property_; }
    static double GetRadius(Microsoft::UI::Xaml::UIElement const& element);
    static void SetRadius(Microsoft::UI::Xaml::UIElement const& element, double radius);

private:
    static Microsoft::UI::Xaml::DependencyProperty radius_property_;
    static void OnRadiusChanged(Microsoft::UI::Xaml::DependencyObject const& sender,
                                Microsoft::UI::Xaml::DependencyPropertyChangedEventArgs const& args);
};

}  // namespace winrt::MusicUnlock::implementation

namespace winrt::MusicUnlock::factory_implementation {

struct Corners : CornersT<Corners, implementation::Corners> {};

}  // namespace winrt::MusicUnlock::factory_implementation
