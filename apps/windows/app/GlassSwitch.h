#pragma once

#include "GlassSwitch.g.h"

#include <vector>
#include <winrt/Windows.UI.ViewManagement.h>

namespace glass_switch_tests { struct Access; }

namespace winrt::MusicUnlock::implementation {

struct GlassSwitch : GlassSwitchT<GlassSwitch> {
    GlassSwitch();

    Windows::Foundation::Collections::IVector<hstring> Labels() const { return labels_; }
    Windows::Foundation::Collections::IVector<hstring> Glyphs() const { return glyphs_; }

    int32_t SelectedIndex() const { return selected_; }
    void SelectedIndex(int32_t value);
    bool OnGlass() const { return on_glass_; }
    void OnGlass(bool value);
    bool Fill() const { return fill_; }
    void Fill(bool value);
    hstring Title() const { return title_; }
    void Title(hstring const& value);

    static Microsoft::UI::Xaml::DependencyProperty SelectedIndexProperty() { return selected_property_; }
    static Microsoft::UI::Xaml::DependencyProperty OnGlassProperty() { return on_glass_property_; }
    static Microsoft::UI::Xaml::DependencyProperty FillProperty() { return fill_property_; }
    static Microsoft::UI::Xaml::DependencyProperty TitleProperty() { return title_property_; }

    winrt::event_token SelectionChanged(
        Windows::Foundation::TypedEventHandler<MusicUnlock::GlassSwitch, IInspectable> const& handler);
    void SelectionChanged(winrt::event_token const& token) noexcept;

    /// 返回指针所在段；拖出轨道后返回最近段，对应 macOS select(at:)。
    int32_t SegmentAt(double x) const;
    int32_t SegmentCount() const { return static_cast<int32_t>(labels_.Size()); }
    hstring SegmentName(int32_t index) const;
    /// 由讲述人选择一段。
    void SelectFromPeer(int32_t index) { Select(index); }
    Windows::Foundation::Collections::IVector<Microsoft::UI::Xaml::Automation::Peers::AutomationPeer> SegmentPeers();

    void OnApplyTemplate();
    Microsoft::UI::Xaml::Automation::Peers::AutomationPeer OnCreateAutomationPeer();
    void OnPointerPressed(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
    void OnPointerMoved(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
    void OnPointerReleased(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
    void OnPointerCaptureLost(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
    void OnPointerExited(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
    void OnKeyDown(Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);

private:
    friend struct ::glass_switch_tests::Access;
    static Microsoft::UI::Xaml::DependencyProperty selected_property_;
    static Microsoft::UI::Xaml::DependencyProperty on_glass_property_;
    static Microsoft::UI::Xaml::DependencyProperty fill_property_;
    static Microsoft::UI::Xaml::DependencyProperty title_property_;

    static void OnSelectedChanged(Microsoft::UI::Xaml::DependencyObject const& sender,
                                  Microsoft::UI::Xaml::DependencyPropertyChangedEventArgs const& args);
    static void OnGlassChanged(Microsoft::UI::Xaml::DependencyObject const& sender,
                               Microsoft::UI::Xaml::DependencyPropertyChangedEventArgs const& args);
    static void OnFillChanged(Microsoft::UI::Xaml::DependencyObject const& sender,
                              Microsoft::UI::Xaml::DependencyPropertyChangedEventArgs const& args);
    static void OnTitleChanged(Microsoft::UI::Xaml::DependencyObject const& sender,
                               Microsoft::UI::Xaml::DependencyPropertyChangedEventArgs const& args);

    void OnLabelsChanged(Windows::Foundation::Collections::IObservableVector<hstring> const&,
                         Windows::Foundation::Collections::IVectorChangedEventArgs const&);
    void OnSized(IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&);
    void OnThemeChanged(Microsoft::UI::Xaml::FrameworkElement const&, IInspectable const&);
    void OnLayoutUpdated(IInspectable const&, IInspectable const&);
    void OnSurfaceSized(IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&);

    void Rebuild();
    void Restyle(bool animate_colors = false);
    void MoveLens(bool animate);
    Microsoft::UI::Xaml::FrameworkElement LensFill() const;
    void ScaleLens(wchar_t const* layer, Microsoft::UI::Composition::CompositionAnimation const& animation);
    void StartScale(Microsoft::UI::Xaml::FrameworkElement const& element,
                    Microsoft::UI::Composition::CompositionAnimation const& animation);
    void StopMotion();
    void RefreshMotion();
    void Squash();
    void Press(bool pressed);
    void Bounce(int32_t index);
    void UpdateShadow(bool animate = false);
    bool ReducedMotion() const;
    void Select(int32_t index);
    hstring SpokenName() const;
    Windows::Foundation::Rect SegmentBounds(int32_t index) const;

    Windows::Foundation::Collections::IObservableVector<hstring> labels_;
    Windows::Foundation::Collections::IObservableVector<hstring> glyphs_;
    int32_t selected_ = -1;
    bool on_glass_ = false;
    bool fill_ = false;
    hstring title_;
    bool pressed_ = false;
    bool syncing_ = false;
    uint32_t pointer_ = 0;
    int32_t hovered_ = -1;
    Windows::Foundation::Rect lens_bounds_{};

    Microsoft::UI::Xaml::Controls::Grid segments_panel_{nullptr};
    Microsoft::UI::Xaml::Controls::Grid lens_{nullptr};
    std::vector<Microsoft::UI::Xaml::FrameworkElement> segments_;
    Windows::Foundation::Collections::IVector<Microsoft::UI::Xaml::Automation::Peers::AutomationPeer> segment_peers_;
    Microsoft::UI::Composition::DropShadow lens_shadow_{nullptr};
    Microsoft::UI::Composition::SpriteVisual shadow_visual_{nullptr};
    float shadow_opacity_ = -1.f;
    float shadow_blur_ = -1.f;
    Microsoft::UI::Xaml::Media::TranslateTransform lens_position_{nullptr};
    Microsoft::UI::Xaml::Media::Animation::Storyboard lens_animation_{nullptr};
    std::vector<Microsoft::UI::Xaml::Media::Animation::Storyboard> color_animations_;
    std::vector<std::pair<Microsoft::UI::Xaml::FrameworkElement, Microsoft::UI::Composition::CompositionAnimation>> scale_animations_;
    Windows::UI::ViewManagement::UISettings ui_settings_;
    Windows::UI::ViewManagement::UISettings::AnimationsEnabledChanged_revoker motion_event_;
    bool motion_reduced_ = false;
    std::vector<Microsoft::UI::Xaml::FrameworkElement::SizeChanged_revoker> surface_events_;
    winrt::event<Windows::Foundation::TypedEventHandler<MusicUnlock::GlassSwitch, IInspectable>> selection_changed_;
};

}  // namespace winrt::MusicUnlock::implementation

namespace winrt::MusicUnlock::factory_implementation {

struct GlassSwitch : GlassSwitchT<GlassSwitch, implementation::GlassSwitch> {};

}  // namespace winrt::MusicUnlock::factory_implementation
