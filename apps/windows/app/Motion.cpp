#include "pch.h"

#include "Motion.h"

#include "GlassMotion.h"

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

/// SwiftUI springs the existing motion maps onto. glass_motion's table covers the shared controls; these are the
/// ones ContentView.swift and LyricsView.swift pass to .motion() directly.
constexpr spring::Spring smooth_35{0.35, 1};     // .smooth(duration: 0.35): banners, status text
constexpr spring::Spring snappy{0.5, 0.85};      // .snappy: rows appearing
constexpr spring::Spring spring_40{0.4, 0.75};   // .spring(duration: 0.4, bounce: 0.25): a state glyph
constexpr spring::Spring spring_35{0.35, 0.75};  // .spring(duration: 0.35, bounce: 0.25): the drop overlay

std::wstring spring_label(spring::Spring value) {
    wchar_t text[64];
    swprintf_s(text, L"spring %.2f/%.2f", value.response, value.damping);
    return text;
}

/// Samples `spring` from `from` to `to`. The first keyframe is the value at the call, so replacing a running
/// animation continues from wherever it was. glass_motion's helpers only cover the case of starting at 0.
template <typename Animation, typename Value>
void sample(Animation const& animation, spring::Spring spring, Value from, Value to) {
    const auto frames = spring::keyframes(spring);
    animation.InsertKeyFrame(0.f, from);
    for (const auto& [progress, position] : frames) {
        if (progress <= 0.f) continue;
        animation.InsertKeyFrame(progress, from + (to - from) * static_cast<float>(position));
    }
    animation.Duration(std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(
        std::chrono::duration<double>(spring::settle_time(spring))));
}

int milliseconds(winrt::Windows::Foundation::TimeSpan duration) {
    return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
}

std::wstring components(float3 value) {
    wchar_t text[64];
    swprintf_s(text, L"%.2f,%.2f", value.x, value.y);
    return text;
}

ScalarKeyFrameAnimation fade(Compositor const& c, float from, float to, spring::Spring spring) {
    auto animation = c.CreateScalarKeyFrameAnimation();
    animation.Target(L"Opacity");
    sample(animation, spring, from, to);
    if (capturing) animation.Comment(described.c_str());
    record(L"Opacity", (std::to_wstring(static_cast<int>(from)) + L" -> " + std::to_wstring(static_cast<int>(to))).c_str(),
           spring_label(spring).c_str(), milliseconds(animation.Duration()));
    return animation;
}

Vector3KeyFrameAnimation vector(Compositor const& c, const wchar_t* target, float3 from, float3 to, spring::Spring spring) {
    auto animation = c.CreateVector3KeyFrameAnimation();
    animation.Target(target);
    sample(animation, spring, from, to);
    if (capturing) animation.Comment(described.c_str());
    record(target, (components(from) + L" -> " + components(to)).c_str(), spring_label(spring).c_str(),
           milliseconds(animation.Duration()));
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

void crossfade(UIElement const& element) { crossfade(element, smooth_35); }

void crossfade(UIElement const& element, spring::Spring spring) {
    if (!enabled()) return set_implicit(element, nullptr, nullptr);
    auto c = compositor();
    pivot(element, false);
    // Workspaces, empty states and lists crossfade. macOS uses .smooth(duration: 0.35) for these; the title-bar
    // action groups pass the workspace spring so they travel with the switch.
    set_implicit(element, group(c, {fade(c, 0.f, 1.f, spring), vector(c, L"Scale", uniform(0.98f), uniform(1.f), spring)}),
                 group(c, {fade(c, 1.f, 0.f, spring), vector(c, L"Scale", uniform(1.f), uniform(0.98f), spring)}));
}

void workspace(UIElement const& outgoing, UIElement const& incoming, bool toward_lyrics) {
    // Music unlock owns the left side and lyrics the right, matching the switch. A workspace leaves toward its own
    // side and arrives from it. No blur: WinUI 3 has no Gaussian blur effect for a live XAML subtree.
    auto place = [](UIElement const& element, float from_x, float to_x, float from_scale, float to_scale, float from_opacity,
                    float to_opacity) {
        auto visual = ElementCompositionPreview::GetElementVisual(element);
        auto c = visual.Compositor();
        auto centre = c.CreateExpressionAnimation(L"Vector3(this.Target.Size.X / 2, this.Target.Size.Y / 2, 0)");
        visual.StartAnimation(L"CenterPoint", centre);
        if (!enabled()) {
            visual.Offset({to_x, 0, 0});
            visual.Scale(uniform(to_scale));
            visual.Opacity(to_opacity);
            return;
        }
        auto offset = c.CreateVector3KeyFrameAnimation();
        offset.Target(L"Offset");
        sample(offset, glass_motion::workspace, float3{from_x, 0, 0}, float3{to_x, 0, 0});
        auto scale = c.CreateVector3KeyFrameAnimation();
        scale.Target(L"Scale");
        sample(scale, glass_motion::workspace, uniform(from_scale), uniform(to_scale));
        auto opacity = c.CreateScalarKeyFrameAnimation();
        opacity.Target(L"Opacity");
        sample(opacity, glass_motion::workspace, from_opacity, to_opacity);
        record(L"Offset", (std::to_wstring(static_cast<int>(from_x)) + L" -> " + std::to_wstring(static_cast<int>(to_x))).c_str(),
               spring_label(glass_motion::workspace).c_str(), milliseconds(offset.Duration()));
        visual.StartAnimation(L"Offset", offset);
        visual.StartAnimation(L"Scale", scale);
        visual.StartAnimation(L"Opacity", opacity);
    };
    const float leaving = toward_lyrics ? -40.f : 40.f;
    const float arriving = toward_lyrics ? 40.f : -40.f;
    place(outgoing, 0.f, leaving, 1.f, 0.98f, 1.f, 0.f);
    place(incoming, arriving, 0.f, 0.98f, 1.f, 0.f, 1.f);
}

void settle(UIElement const& element) {
    if (!enabled()) return set_implicit(element, nullptr, nullptr);
    auto c = compositor();
    ElementCompositionPreview::SetIsTranslationEnabled(element, true);
    pivot(element, true);
    // Banners settle. macOS .smooth(duration: 0.35): in from 8 px above, out by shrinking in place.
    set_implicit(element,
                 group(c, {fade(c, 0.f, 1.f, smooth_35), vector(c, L"Translation", {0, -8, 0}, {0, 0, 0}, smooth_35),
                           vector(c, L"Scale", uniform(0.98f), uniform(1.f), smooth_35)}),
                 group(c, {fade(c, 1.f, 0.f, smooth_35), vector(c, L"Scale", uniform(1.f), uniform(0.98f), smooth_35)}));
}

void overlay(UIElement const& element) {
    if (!enabled()) return set_implicit(element, nullptr, nullptr);
    auto c = compositor();
    pivot(element, false);
    // The drop overlay. macOS .spring(duration: 0.35, bounce: 0.25): in from 103 %, back out the same way.
    set_implicit(element,
                 group(c, {fade(c, 0.f, 1.f, spring_35), vector(c, L"Scale", uniform(1.03f), uniform(1.f), spring_35)}),
                 group(c, {fade(c, 1.f, 0.f, spring_35), vector(c, L"Scale", uniform(1.f), uniform(1.03f), spring_35)}));
}

void enter(UIElement const& element) {
    if (!enabled()) return;
    auto c = compositor();
    // A queue row that just appeared. macOS .snappy, which is response 0.5, damping 0.85.
    element.StartAnimation(fade(c, 0.f, 1.f, snappy));
    element.StartAnimation(vector(c, L"Translation", {0, -8, 0}, {0, 0, 0}, snappy));
}

void pop(UIElement const& element, bool bounce) {
    if (!enabled()) return;
    auto c = compositor();
    pivot_properties(element);
    // A state glyph. macOS .spring(duration: 0.4, bounce: 0.25). A finished job passes through 1.08 on the way, which
    // is the extra bounce; the spring then settles it back to 1.
    auto scale = c.CreateVector3KeyFrameAnimation();
    scale.Target(L"Scale");
    if (bounce) {
        const auto frames = spring::keyframes(spring_40);
        const auto halfway = frames[frames.size() / 2].first;
        scale.InsertKeyFrame(0.f, uniform(0.6f));
        scale.InsertKeyFrame(halfway, uniform(1.08f));
        for (const auto& [progress, position] : frames) {
            if (progress <= halfway) continue;
            const float rest = static_cast<float>((position - frames[frames.size() / 2].second) /
                                                   (1.0 - frames[frames.size() / 2].second));
            scale.InsertKeyFrame(progress, uniform(1.08f) + (uniform(1.f) - uniform(1.08f)) * rest);
        }
        scale.Duration(std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(
            std::chrono::duration<double>(spring::settle_time(spring_40))));
    } else {
        sample(scale, spring_40, uniform(0.6f), uniform(1.f));
    }
    record(L"Scale", bounce ? L"0.6 -> 1.08 -> 1" : L"0.6 -> 1", spring_label(spring_40).c_str(),
           milliseconds(scale.Duration()));
    element.StartAnimation(scale);
    element.StartAnimation(fade(c, 0.f, 1.f, spring_40));
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
    // The empty-state disc lifting as files hover. macOS .spring(duration: 0.35, bounce: 0.25).
    element.StartAnimation(vector(c, L"Scale", element.Scale(), scale, spring_35));
    auto rotate = c.CreateScalarKeyFrameAnimation();
    rotate.Target(L"Rotation");
    sample(rotate, spring_35, element.Rotation(), rotation);
    record(L"Rotation", lifted ? L"0 -> -6" : L"-6 -> 0", spring_label(spring_35).c_str(), milliseconds(rotate.Duration()));
    element.StartAnimation(rotate);
    // The animated values stay after the animations end; keep the properties in step.
    element.Scale(scale);
    element.Rotation(rotation);
}

Loop Loop::hop(UIElement const& element) {
    auto c = compositor();
    auto animation = c.CreateVector3KeyFrameAnimation();
    animation.Target(L"Translation");
    // A repeating hop has no single spring; the rise uses .smooth and the fall eases back.
    animation.InsertKeyFrame(0.f, {0, 0, 0});
    animation.InsertKeyFrame(0.3f, {0, -7, 0}, c.CreateCubicBezierEasingFunction({0.2f, 0.f}, {0.f, 1.f}));
    animation.InsertKeyFrame(0.6f, {0, 0, 0}, c.CreateCubicBezierEasingFunction({0.45f, 0.f}, {0.55f, 1.f}));
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
    auto drift_ease = c.CreateCubicBezierEasingFunction({0.45f, 0.f}, {0.55f, 1.f});
    animation.InsertKeyFrame(0.5f, {0, -5, 0}, drift_ease);
    animation.InsertKeyFrame(1.f, {0, 3, 0}, drift_ease);
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
