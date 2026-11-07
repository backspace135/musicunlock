#pragma once
// Motion from apps/macos/DESIGN.md ("动效") on composition animations. Nothing moves when Windows
// "Animation effects" is off (UISettings.AnimationsEnabled), when MUSICUNLOCK_REDUCE_MOTION is set, or
// while previews render; reduced motion means no movement at all rather than shorter movement.
//
// Implicit show / hide animations use an element's handoff visual, the one-shot and looping animations
// use its UIElement properties (Translation, Scale, Rotation). An element may only use one of the two,
// so each helper below says which it takes.

#include "SpringCurve.h"

#include <initializer_list>
#include <vector>

#include <winrt/Microsoft.UI.Xaml.h>

namespace motion {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = winrt::Microsoft::UI::Xaml::Controls;

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

/// Workspaces, empty states and lists: fade, growing from 98 %, on .smooth(duration: 0.35).
void crossfade(xaml::UIElement const& element);
/// The same crossfade on a chosen spring. The title-bar action groups use the workspace spring.
void crossfade(xaml::UIElement const& element, spring::Spring spring);
/// The two workspaces trading places. The one leaving slides 40 px toward its own side and fades out, the one
/// arriving comes in from 40 px on its own side, and both scale between 98 % and 100 %. `toward_lyrics` is the
/// direction of the switch, so the lyrics workspace owns the right side. Plays on the elements' handoff visuals,
/// so call it when the visibility is about to change, and call it again after the change so the arrival runs.
void workspace(xaml::UIElement const& outgoing, xaml::UIElement const& incoming, bool toward_lyrics);

/// Rows shrink and fade as they scroll past the list's edges, the way iOS lists do: fully inside a row is
/// unchanged, and one clipped by the top or bottom edge eases to 40 % opacity and 96 % scale, pivoted on the
/// edge it is leaving. The scroll manipulation property set never updates under WinUI 3, so this follows the
/// ScrollViewer's ViewChanged instead of an expression. Call it once; it keeps up with virtualization itself.
void scroll_edge(controls::ListView const& list);
/// Banners: drop in from 8 px above and fade in; shrink in place and fade out.
void settle(xaml::UIElement const& element);
/// The drop overlay: fade in while shrinking from 103 %, the reverse when it leaves.
void overlay(xaml::UIElement const& element);

// One-shot animations (UIElement properties).

/// A queue row that just appeared: from 8 px above, faded in.
void enter(xaml::UIElement const& element);
/// A state glyph that changed: grows in from 60 %; `bounce` overshoots once (finished jobs).
void pop(xaml::UIElement const& element, bool bounce);
/// An icon acknowledging a change: dips to 82 % and springs back to full size, around its own centre.
void bounce(xaml::UIElement const& element);
/// An icon replaced by another: the old one shrinks to 60 % and fades out, the new one grows in from 60 %.
void replace(xaml::UIElement const& outgoing, xaml::UIElement const& incoming);
/// An export starting or finishing: the icon drops 3 px and springs back up.
void dip(xaml::UIElement const& element);
/// Fades and scales between rest and `from_scale`. `appear` settles at full size and full opacity; otherwise it
/// settles at `from_scale` and 35 percent opacity, which is how an expired QR code recedes.
void fade_scale(xaml::UIElement const& element, bool appear, spring::Spring spring, float from_scale = 1.f);

/// Slides one highlight onto the selected row and keeps it there while the list scrolls. The highlight is a
/// sibling of the list, so its offset is the row's position in that shared parent. Pass a null row to hide it.
void move_highlight(xaml::UIElement const& highlight, xaml::UIElement const& row);

/// The "keep original quality" mark while a conversion runs: its bars light up one after another, and go back to
/// rest together when it stops. Call it with the same bars each time; starting again does nothing.
class Wave {
public:
    Wave() = default;
    Wave(std::initializer_list<xaml::UIElement> bars);

    void start();
    void stop();

private:
    void light(int index);

    std::vector<winrt::weak_ref<xaml::UIElement>> bars_;
    winrt::Microsoft::UI::Xaml::DispatcherTimer timer_{nullptr};
    int index_ = 0;
    bool running_ = false;
};
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
