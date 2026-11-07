#include "pch.h"

#include "GlassMotion.h"

#include "Motion.h"

#include <chrono>

namespace glass_motion {
namespace {

template <typename Animation, typename Value>
Animation animate(Compositor const&, spring::Spring spring, wchar_t const* target, Value to, Animation const& animation) {
    animation.Target(target);
    // StartingValue keeps the current value when a running animation is replaced.
    animation.InsertExpressionKeyFrame(0.f, L"this.StartingValue");
    const double duration = spring::settle_time(spring);
    for (const auto& [progress, sample] : spring::keyframes(spring)) {
        if (progress <= 0.f) continue;
        animation.InsertKeyFrame(progress, to * sample);
    }
    animation.Duration(std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(
        std::chrono::duration<double>(duration)));
    return animation;
}

}  // namespace

CompositionAnimation scalar(Compositor const& compositor, spring::Spring spring, wchar_t const* target, float to) {
    if (!motion::enabled()) return nullptr;
    return animate(compositor, spring, target, to, compositor.CreateScalarKeyFrameAnimation());
}

CompositionAnimation vector2(Compositor const& compositor, spring::Spring spring, wchar_t const* target, float2 to) {
    if (!motion::enabled()) return nullptr;
    return animate(compositor, spring, target, to, compositor.CreateVector2KeyFrameAnimation());
}

CompositionAnimation vector3(Compositor const& compositor, spring::Spring spring, wchar_t const* target, float3 to) {
    if (!motion::enabled()) return nullptr;
    return animate(compositor, spring, target, to, compositor.CreateVector3KeyFrameAnimation());
}

}  // namespace glass_motion
