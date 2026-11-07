#include "pch.h"

#include "Motion.h"

#include <map>

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
bool capturing = false;
std::wstring described;

Compositor compositor() { return winrt::Microsoft::UI::Xaml::Media::CompositionTarget::GetCompositorForCurrentThread(); }

/// One line per animation while capture is on: target, what changed, the curve and how long it takes.
void record(const wchar_t* target, const wchar_t* change, const wchar_t* curve, int milliseconds) {
    if (!capturing) return;
    wchar_t path[MAX_PATH];
    if (!GetEnvironmentVariableW(L"MUSICUNLOCK_MOTION_LOG", path, MAX_PATH) || !path[0]) return;
    FILE* file = nullptr;
    if (_wfopen_s(&file, path, L"a, ccs=UTF-8") != 0 || !file) return;
    fwprintf(file, L"%s | %s | %s | %s | %d ms\n", described.empty() ? L"(unnamed)" : described.c_str(), target, change,
             curve && *curve ? curve : L"-", milliseconds);
    fclose(file);
}

const wchar_t* curve_name(CompositionEasingFunction const& easing) {
    const auto cubic = easing.try_as<CubicBezierEasingFunction>();
    if (!cubic) return L"linear";
    const auto control = cubic.ControlPoint2();
    if (control.x < 0.1f) return L"smooth";
    if (control.y > 1.1f || cubic.ControlPoint1().y > 1.1f) return L"springy";
    return L"ease-in-out";
}

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
    if (capturing) animation.Comment(described.c_str());
    record(L"Opacity",
           (std::to_wstring(static_cast<int>(from)) + L" -> " + std::to_wstring(static_cast<int>(to))).c_str(), L"smooth",
           static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count()));
    return animation;
}

Vector3KeyFrameAnimation vector(Compositor const& c, const wchar_t* target, float3 from, float3 to,
                                winrt::Windows::Foundation::TimeSpan duration, CompositionEasingFunction const& easing) {
    auto animation = c.CreateVector3KeyFrameAnimation();
    animation.Target(target);
    animation.InsertKeyFrame(0.f, from);
    animation.InsertKeyFrame(1.f, to, easing);
    animation.Duration(duration);
    auto component = [](float3 value) {
        wchar_t text[64];
        swprintf_s(text, L"%.2f,%.2f", value.x, value.y);
        return std::wstring(text);
    };
    if (capturing) animation.Comment(described.c_str());
    record(target, (component(from) + L" -> " + component(to)).c_str(), curve_name(easing),
           static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count()));
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

/// Implicit animations are recorded when they are built, which is when they are attached — before anything is
/// shown. Playing one names it again, so the log shows the moment it actually ran.
void replay(CompositionAnimation const& animation) {
    const std::wstring saved = described;
    describe(animation.Comment().c_str());
    const auto duration = [](CompositionAnimation const& animation) {
        if (auto scalar = animation.try_as<ScalarKeyFrameAnimation>()) return scalar.Duration();
        if (auto vector = animation.try_as<Vector3KeyFrameAnimation>()) return vector.Duration();
        return winrt::Windows::Foundation::TimeSpan{};
    };
    record(animation.Target().c_str(), L"started", L"implicit",
           static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(duration(animation)).count()));
    describe(saved);
}

void note_group(CompositionAnimationGroup const& group) {
    if (!capturing || !group) return;
    for (auto const& animation : group) replay(animation);
}

struct ImplicitGroups {
    CompositionAnimationGroup show{nullptr};
    CompositionAnimationGroup hide{nullptr};
};

std::map<UIElement, ImplicitGroups> implicit;

void set_implicit(UIElement const& element, CompositionAnimationGroup const& show, CompositionAnimationGroup const& hide) {
    if (capturing) {
        implicit[element] = {show, hide};
        for (const auto& group : {show, hide}) {
            if (!group) continue;
            for (auto const& animation : group) animation.Comment(described.c_str());
        }
    }
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

void capture(bool value) { capturing = value; }

void changed(UIElement const& element) {
    const auto found = implicit.find(element);
    if (found == implicit.end()) return;
    const bool shown = element.Visibility() == winrt::Microsoft::UI::Xaml::Visibility::Visible;
    note_group(shown ? found->second.show : found->second.hide);
}

void describe(std::wstring element) { described = std::move(element); }

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
    record(L"Scale", bounce ? L"0.6 -> 1.2 -> 1" : L"0.6 -> 1", bounce ? L"smooth, ease-in-out" : L"smooth", bounce ? 400 : 250);
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
    record(L"Translation", L"0,0 -> 0,-7 -> 0,0", L"smooth, ease-in-out", 900);
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
    record(L"Translation", L"0,3 -> 0,-5 -> 0,3", L"ease-in-out", 5600);
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
