#pragma once
// The one parameter table for the glass controls, matching GlassMotion in
// apps/macos/Sources/MusicUnlock/Theme.swift. These are community estimates of iOS 26, not measurements;
// change them in both places together. Springs are SwiftUI response / dampingFraction (see SpringCurve.h).

#include "SpringCurve.h"

#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Windows.Foundation.Numerics.h>

namespace glass_motion {

namespace composition = winrt::Microsoft::UI::Composition;
using composition::CompositionAnimation;
using composition::Compositor;
using winrt::Windows::Foundation::Numerics::float2;
using winrt::Windows::Foundation::Numerics::float3;

inline constexpr spring::Spring open{0.32, 0.78};        // menu growing out of its button, ~2 % overshoot
inline constexpr spring::Spring close{0.26, 1.0};        // menu shrinking back, no overshoot
inline constexpr double handoff_duration = 0.08;        // easeInOut, seconds
inline constexpr double handoff_delay = 0.12;           // after close starts
inline constexpr spring::Spring lens{0.3, 0.72};         // switch lens travelling between segments
inline constexpr spring::Spring press{0.22, 0.6};        // lens lifting under the pointer
inline constexpr spring::Spring highlight{0.2, 0.85};    // highlight sliding between menu rows
inline constexpr spring::Spring workspace{0.5, 0.85};    // .spring(duration: 0.5, bounce: 0.15)
inline constexpr spring::Spring pressable{0.3, 0.5};     // .spring(duration: 0.3, bounce: 0.5)
inline constexpr spring::Spring footer{0.45, 0.8};       // .spring(duration: 0.45, bounce: 0.2)
inline constexpr spring::Spring selection{0.35, 0.8};    // .spring(duration: 0.35, bounce: 0.2)

// Keyframes sampled from SpringCurve, interpolated linearly, lasting settle_time. The first keyframe reads
// the property's current value (this.StartingValue), so an animation interrupted halfway does not jump.
// Returns nullptr when motion::enabled() is false; the caller then assigns the final value directly.
// Not a SpringNaturalMotionAnimation: its Period has no documented mapping to SwiftUI's response.
CompositionAnimation scalar(Compositor const& compositor, spring::Spring spring, wchar_t const* target, float to);
CompositionAnimation vector2(Compositor const& compositor, spring::Spring spring, wchar_t const* target, float2 to);
CompositionAnimation vector3(Compositor const& compositor, spring::Spring spring, wchar_t const* target, float3 to);

}  // namespace glass_motion
