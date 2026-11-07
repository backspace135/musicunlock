#pragma once
// Motion capture for checking the curves (issue #66).
//
// screenshot.py's private desktop cannot produce pixels: Windows.Graphics.Capture refuses a window that is
// not on the input desktop (CreateForWindow returns E_INVALIDARG), and RenderTargetBitmap's RenderAsync
// never completes there. Switching to the input desktop, or capturing the screen, is not allowed. So a
// capture run records every animation instead — its target, from and to values, curve and duration — into
// <output>\motion\<scenario>-<theme>.log, and the scenario says what it did and when.
//
// Scenarios register themselves; C–F add their own beside the three this file's caller registers.

#include <functional>
#include <string>

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.System.h>

namespace motion_capture {

struct Driver {
    /// One click: press, then release, through the control's Invoke automation (its click handler).
    void click(winrt::Microsoft::UI::Xaml::UIElement const& element);
    void press(winrt::Microsoft::UI::Xaml::UIElement const& element);
    void release(winrt::Microsoft::UI::Xaml::UIElement const& element);
    /// Reports a drag between two points of the element. A render has no pointer, so this records the gesture
    /// rather than synthesising one.
    void drag(winrt::Microsoft::UI::Xaml::UIElement const& element, winrt::Windows::Foundation::Numerics::float2 from,
              winrt::Windows::Foundation::Numerics::float2 to, int ms);
    /// The window's keyboard accelerator handler, which is what the real Ctrl+key reaches.
    void key(winrt::Windows::System::VirtualKey key,
             winrt::Windows::System::VirtualKeyModifiers modifiers = winrt::Windows::System::VirtualKeyModifiers::None);
    /// Waits `ms`, so the log shows the gap between two actions.
    void wait(int ms);
    /// Marks the following `ms` as the part of the scenario being examined.
    void capture(int ms);
    /// Names the element the next animation belongs to (motion::describe).
    void name(std::wstring element);
    /// A line in the scenario log, timestamped from the start of the scenario.
    void note(const std::wstring& text) const;

    struct State;
    explicit Driver(std::shared_ptr<State> state) : state_(std::move(state)) {}

private:
    std::shared_ptr<State> state_;
};

void add(std::wstring name, std::function<void(Driver&)> scenario);

/// Where key() delivers a key: the same handler a real accelerator reaches. Set before run().
void set_key_handler(std::function<void(winrt::Windows::System::VirtualKey, winrt::Windows::System::VirtualKeyModifiers)> handler);

/// Runs every name in the comma separated list (or all of them), light then dark, and exits the app.
void run(winrt::Microsoft::UI::Xaml::FrameworkElement const& root, std::wstring scenarios, std::wstring directory);

}  // namespace motion_capture
