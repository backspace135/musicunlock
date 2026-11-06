#include "pch.h"

#include "Motion.h"

#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.UI.ViewManagement.h>

namespace motion {

namespace {

using namespace winrt::Microsoft::UI::Composition;
using winrt::Microsoft::UI::Xaml::UIElement;
using winrt::Microsoft::UI::Xaml::Hosting::ElementCompositionPreview;
using winrt::Windows::Foundation::Numerics::float3;
using namespace std::chrono_literals;

bool suspended = false;

Compositor compositor() { return winrt::Microsoft::UI::Xaml::Media::CompositionTarget::GetCompositorForCurrentThread(); }

/// macOS .smooth: quick start, long gentle stop, no overshoot.
CompositionEasingFunction smooth(Compositor const& c) { return c.CreateCubicBezierEasingFunction({0.2f, 0.f}, {0.f, 1.f}); }
/// macOS .spring with a little bounce: overshoots slightly before settling.
CompositionEasingFunction springy(Compositor const& c) {
    return c.CreateCubicBezierEasingFunction({0.34f, 1.4f}, {0.64f, 1.f});
}
CompositionEasingFunction ease_in_out(Compositor const& c) {
    return c.CreateCubicBezierEasingFunction({0.45f, 0.f}, {0.55f, 1.f});
}

ScalarKeyFrameAnimation fade(Compositor const& c, float from, float to, winrt::Windows::Foundation::TimeSpan duration) {
    auto animation = c.CreateScalarKeyFrameAnimation();
    animation.Target(L"Opacity");
    animation.InsertKeyFrame(0.f, from);
    animation.InsertKeyFrame(1.f, to, smooth(c));
    animation.Duration(duration);
    return animation;
}

Vector3KeyFrameAnimation vector(Compositor const& c, const wchar_t* target, float3 from, float3 to,
                                winrt::Windows::Foundation::TimeSpan duration, CompositionEasingFunction const& easing) {
    auto animation = c.CreateVector3KeyFrameAnimation();
    animation.Target(target);
    animation.InsertKeyFrame(0.f, from);
    animation.InsertKeyFrame(1.f, to, easing);
    animation.Duration(duration);
    return animation;
}

float3 uniform(float scale) { return {scale, scale, 1.f}; }

/// Scale and rotation pivot on the handoff visual, following its size (top edge for banners).
void pivot(UIElement const& element, bool top) {
    auto visual = ElementCompositionPreview::GetElementVisual(element);
    auto center = visual.Compositor().CreateExpressionAnimation(
        top ? L"Vector3(this.Target.Size.X / 2, 0, 0)" : L"Vector3(this.Target.Size.X / 2, this.Target.Size.Y / 2, 0)");
    visual.StartAnimation(L"CenterPoint", center);
}

/// The same for UIElement properties; laid out first when the element has only just become visible.
void pivot_properties(UIElement const& element) {
    auto size = element.ActualSize();
    if (size.x == 0 || size.y == 0) {
        element.UpdateLayout();
        size = element.ActualSize();
    }
    element.CenterPoint({size.x / 2, size.y / 2, 0.f});
}

void set_implicit(UIElement const& element, CompositionAnimationGroup const& show, CompositionAnimationGroup const& hide) {
    ElementCompositionPreview::SetImplicitShowAnimation(element, show);
    ElementCompositionPreview::SetImplicitHideAnimation(element, hide);
}

CompositionAnimationGroup group(Compositor const& c, std::initializer_list<CompositionAnimation> animations) {
    auto result = c.CreateAnimationGroup();
    for (const auto& animation : animations) result.Add(animation);
    return result;
}

}  // namespace

bool enabled() {
    if (suspended || GetEnvironmentVariableW(L"MUSICUNLOCK_REDUCE_MOTION", nullptr, 0) > 0) return false;
    return winrt::Windows::UI::ViewManagement::UISettings().AnimationsEnabled();
}

void suspend(bool value) { suspended = value; }

void crossfade(UIElement const& element) {
    if (!enabled()) return set_implicit(element, nullptr, nullptr);
    auto c = compositor();
    pivot(element, false);
    set_implicit(element,
                 group(c, {fade(c, 0.f, 1.f, 350ms), vector(c, L"Scale", uniform(0.98f), uniform(1.f), 350ms, smooth(c))}),
                 group(c, {fade(c, 1.f, 0.f, 200ms), vector(c, L"Scale", uniform(1.f), uniform(0.98f), 200ms, smooth(c))}));
}

void settle(UIElement const& element) {
    if (!enabled()) return set_implicit(element, nullptr, nullptr);
    auto c = compositor();
    ElementCompositionPreview::SetIsTranslationEnabled(element, true);
    pivot(element, true);
    set_implicit(element,
                 group(c, {fade(c, 0.f, 1.f, 300ms), vector(c, L"Translation", {0, -8, 0}, {0, 0, 0}, 300ms, smooth(c)),
                           vector(c, L"Scale", uniform(0.98f), uniform(1.f), 300ms, smooth(c))}),
                 group(c, {fade(c, 1.f, 0.f, 200ms), vector(c, L"Scale", uniform(1.f), uniform(0.98f), 200ms, smooth(c))}));
}

void overlay(UIElement const& element) {
    if (!enabled()) return set_implicit(element, nullptr, nullptr);
    auto c = compositor();
    pivot(element, false);
    set_implicit(element,
                 group(c, {fade(c, 0.f, 1.f, 250ms), vector(c, L"Scale", uniform(1.03f), uniform(1.f), 350ms, springy(c))}),
                 group(c, {fade(c, 1.f, 0.f, 200ms), vector(c, L"Scale", uniform(1.f), uniform(1.03f), 200ms, smooth(c))}));
}

void enter(UIElement const& element) {
    if (!enabled()) return;
    auto c = compositor();
    element.StartAnimation(fade(c, 0.f, 1.f, 300ms));
    element.StartAnimation(vector(c, L"Translation", {0, -8, 0}, {0, 0, 0}, 300ms, smooth(c)));
}

void pop(UIElement const& element, bool bounce) {
    if (!enabled()) return;
    auto c = compositor();
    pivot_properties(element);
    auto scale = c.CreateVector3KeyFrameAnimation();
    scale.Target(L"Scale");
    scale.InsertKeyFrame(0.f, uniform(0.6f));
    if (bounce) {
        scale.InsertKeyFrame(0.55f, uniform(1.2f), smooth(c));
        scale.InsertKeyFrame(1.f, uniform(1.f), ease_in_out(c));
    } else {
        scale.InsertKeyFrame(1.f, uniform(1.f), smooth(c));
    }
    scale.Duration(bounce ? 400ms : 250ms);
    element.StartAnimation(scale);
    element.StartAnimation(fade(c, 0.f, 1.f, 150ms));
}

void lift(UIElement const& element, bool lifted) {
    const float3 scale = uniform(lifted ? 1.1f : 1.f);
    const float rotation = lifted ? -6.f : 0.f;
    pivot_properties(element);
    if (!enabled()) {
        element.Scale(scale);
        element.Rotation(rotation);
        return;
    }
    auto c = compositor();
    element.StartAnimation(vector(c, L"Scale", element.Scale(), scale, 400ms, springy(c)));
    auto rotate = c.CreateScalarKeyFrameAnimation();
    rotate.Target(L"Rotation");
    rotate.InsertKeyFrame(0.f, element.Rotation());
    rotate.InsertKeyFrame(1.f, rotation, springy(c));
    rotate.Duration(400ms);
    element.StartAnimation(rotate);
    // The animated values stay after the animations end; keep the properties in step.
    element.Scale(scale);
    element.Rotation(rotation);
}

Loop Loop::hop(UIElement const& element) {
    auto c = compositor();
    auto animation = c.CreateVector3KeyFrameAnimation();
    animation.Target(L"Translation");
    animation.InsertKeyFrame(0.f, {0, 0, 0});
    animation.InsertKeyFrame(0.3f, {0, -7, 0}, smooth(c));
    animation.InsertKeyFrame(0.6f, {0, 0, 0}, ease_in_out(c));
    animation.InsertKeyFrame(1.f, {0, 0, 0});
    animation.Duration(900ms);
    animation.IterationBehavior(AnimationIterationBehavior::Forever);
    return Loop(element, animation);
}

Loop Loop::drift(UIElement const& element) {
    auto c = compositor();
    auto animation = c.CreateVector3KeyFrameAnimation();
    animation.Target(L"Translation");
    animation.InsertKeyFrame(0.f, {0, 3, 0});
    animation.InsertKeyFrame(0.5f, {0, -5, 0}, ease_in_out(c));
    animation.InsertKeyFrame(1.f, {0, 3, 0}, ease_in_out(c));
    animation.Duration(5600ms);  // 2.8 s each way, as on macOS
    animation.IterationBehavior(AnimationIterationBehavior::Forever);
    return Loop(element, animation);
}

void Loop::start() {
    if (running_ || !animation_ || !enabled()) return;
    if (auto element = element_.get()) {
        element.StartAnimation(animation_);
        running_ = true;
    }
}

void Loop::stop() {
    if (!running_) return;
    running_ = false;
    if (auto element = element_.get()) {
        element.StopAnimation(animation_);
        element.Translation({0, 0, 0});
    }
}

}  // namespace motion
