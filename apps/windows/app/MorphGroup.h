#pragma once

#include "MorphGroup.g.h"

#include <map>

namespace winrt::MusicUnlock::implementation {

struct MorphGroup : MorphGroupT<MorphGroup> {
    MorphGroup();

    Microsoft::UI::Xaml::UIElement Shared() const { return shared_; }
    void Shared(Microsoft::UI::Xaml::UIElement const& value);

    static Microsoft::UI::Xaml::DependencyProperty SharedProperty() { return shared_property_; }

    /// Lays the row out and runs the merge, split and shared-slot transitions. Call it after changing a child's
    /// Visibility, which raises no callback of its own.
    void Update();

private:
    static Microsoft::UI::Xaml::DependencyProperty shared_property_;
    static void OnSharedChanged(Microsoft::UI::Xaml::DependencyObject const& sender,
                                Microsoft::UI::Xaml::DependencyPropertyChangedEventArgs const& args);

    Microsoft::UI::Xaml::UIElement shared_{nullptr};
    bool arranged_ = false;
    std::map<Microsoft::UI::Xaml::UIElement, bool> seen_;
    double slot_width_ = 0;
};

}  // namespace winrt::MusicUnlock::implementation

namespace winrt::MusicUnlock::factory_implementation {

struct MorphGroup : MorphGroupT<MorphGroup, implementation::MorphGroup> {};

}  // namespace winrt::MusicUnlock::factory_implementation
