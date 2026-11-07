#pragma once
// Motion from apps/macos/DESIGN.md ("动效") on composition animations. Nothing moves when Windows
// "Animation effects" is off (UISettings.AnimationsEnabled), when MUSICUNLOCK_REDUCE_MOTION is set, or
// while previews render; reduced motion means no movement at all rather than shorter movement.
//
// Implicit show / hide animations use an element's handoff visual, the one-shot and looping animations
// use its UIElement properties (Translation, Scale, Rotation). An element may only use one of the two,
// so each helper below says which it takes.

namespace motion {

namespace xaml = winrt::Microsoft::UI::Xaml;

bool enabled();
/// Static renders, like the macOS PreviewRenderer.
void suspend(bool suspended);
/// Motion capture (MUSICUNLOCK_RENDER_MOTION). On screenshot.py's private desktop neither
/// Windows.Graphics.Capture nor RenderTargetBitmap can produce a frame, so capture records every animation
/// instead: its target, from and to values, curve and duration. `describe` names the element.
void capture(bool capturing);
/// Set before the call that starts an animation, so the record says which element moved.
void describe(std::wstring element);
/// Notes that an element's implicit show or hide animation is running, from its current Visibility.
/// Call it after the change: Visibility changes raise no property-changed callback.
void changed(xaml::UIElement const& element);

// Implicit animations on Visibility changes (handoff visual). They are set, or cleared when motion is
// off, at the time of the call, so callers apply them again when the system setting may have changed.

/// Workspaces, empty states and lists: fade, growing from 98 %.
void crossfade(xaml::UIElement const& element);
/// Banners: drop in from 8 px above and fade in; shrink in place and fade out.
void settle(xaml::UIElement const& element);
/// The drop overlay: fade in while shrinking from 103 %, the reverse when it leaves.
void overlay(xaml::UIElement const& element);

// One-shot animations (UIElement properties).

/// A queue row that just appeared: from 8 px above, faded in.
void enter(xaml::UIElement const& element);
/// A state glyph that changed: grows in from 60 %; `bounce` overshoots once (finished jobs).
void pop(xaml::UIElement const& element, bool bounce);
/// The empty-state disc while files hover: lifted to 110 % and tilted, or back.
void lift(xaml::UIElement const& element, bool lifted);

/// A repeating animation (UIElement properties) that can be stopped again.
class Loop {
public:
    /// Drop overlay icon: hops up and back down.
    static Loop hop(xaml::UIElement const& element);
    /// Empty-state disc: drifts slowly up and down.
    static Loop drift(xaml::UIElement const& element);

    Loop() = default;
    /// Starts when motion is enabled; otherwise (or when already running) does nothing.
    void start();
    /// Stops and returns the element to rest.
    void stop();
    bool running() const { return running_; }

private:
    Loop(xaml::UIElement const& element, winrt::Microsoft::UI::Composition::CompositionAnimation animation)
        : element_(element), animation_(std::move(animation)) {}

    winrt::weak_ref<xaml::UIElement> element_;
    winrt::Microsoft::UI::Composition::CompositionAnimation animation_{nullptr};
    bool running_ = false;
};

}  // namespace motion
