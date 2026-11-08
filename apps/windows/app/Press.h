#pragma once

#include "Press.g.h"

namespace winrt::MusicUnlock::implementation {

struct Press : PressT<Press> {
    Press() = default;

    static Microsoft::UI::Xaml::DependencyProperty ScaleProperty() { return scale_property_; }
    static Microsoft::UI::Xaml::DependencyProperty GlassProperty() { return glass_property_; }

    static double GetScale(Microsoft::UI::Xaml::DependencyObject const& element);
    static void SetScale(Microsoft::UI::Xaml::DependencyObject const& element, double value);
    static double GetGlass(Microsoft::UI::Xaml::DependencyObject const& element);
    static void SetGlass(Microsoft::UI::Xaml::DependencyObject const& element, double value);

private:
    static Microsoft::UI::Xaml::DependencyProperty scale_property_;
    static Microsoft::UI::Xaml::DependencyProperty glass_property_;

    static void OnScaleChanged(Microsoft::UI::Xaml::DependencyObject const& sender,
                               Microsoft::UI::Xaml::DependencyPropertyChangedEventArgs const& args);
};

}  // namespace winrt::MusicUnlock::implementation

namespace winrt::MusicUnlock::factory_implementation {

struct Press : PressT<Press, implementation::Press> {};

}  // namespace winrt::MusicUnlock::factory_implementation
