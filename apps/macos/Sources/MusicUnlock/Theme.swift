import AppKit
import SwiftUI

enum Theme {
    // Black, white and grey only. Emphasis comes from weight and contrast; state from the symbol, not hue.
    // Pure greys (R = G = B), matched to the macOS window greys, so dark mode does not drift blue.
    static let background = adaptive(0xf2f2f2, 0x1e1e1e)
    static let surface = adaptive(0xfafafa, 0x282828)
    static let text = adaptive(0x1c1c1c, 0xf2f2f2)
    static let secondary = adaptive(0x636363, 0xa0a0a0)
    static let accent = adaptive(0x1c1c1c, 0xf2f2f2)
    static let success = adaptive(0x1c1c1c, 0xf2f2f2)
    static let error = adaptive(0x1c1c1c, 0xf2f2f2)
    static let separator = adaptive(0xe0e0e0, 0x3a3a3a)

    static func adaptive(_ light: UInt32, _ dark: UInt32) -> Color {
        Color(nsColor: NSColor(name: nil) { appearance in
            let hex = appearance.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua ? dark : light
            return NSColor(srgbRed: Double((hex >> 16) & 255) / 255,
                           green: Double((hex >> 8) & 255) / 255,
                           blue: Double(hex & 255) / 255, alpha: 1)
        })
    }

    static func hex(_ value: UInt32) -> Color {
        Color(.sRGB, red: Double((value >> 16) & 255) / 255, green: Double((value >> 8) & 255) / 255,
              blue: Double(value & 255) / 255)
    }
}

/// The app icon (assets/AppIcon.svg), so the window and Dock share one mark.
struct DiscMark: View {
    var size: CGFloat = 64

    var body: some View {
        Image(nsImage: NSApp.applicationIconImage)
            .resizable().interpolation(.high)
            .frame(width: size, height: size)
            .accessibilityHidden(true)
    }
}

/// Slow colour field behind the window. Glass only reads as glass when there is something to refract.
/// Pauses while the window is inactive; static under Reduce Motion, opaque under Reduce Transparency.
struct AmbientBackground: View {
    @Environment(\.colorScheme) private var colorScheme
    @Environment(\.controlActiveState) private var activeState
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @Environment(\.accessibilityReduceTransparency) private var reduceTransparency
    @Environment(\.staticPreview) private var staticPreview

    var body: some View {
        if reduceTransparency || staticPreview {
            Theme.background
        } else {
            TimelineView(.animation(minimumInterval: 1 / 30, paused: reduceMotion || activeState == .inactive)) { context in
                mesh(at: reduceMotion ? 0 : context.date.timeIntervalSinceReferenceDate)
            }
        }
    }

    /// Greyscale light and shade only. Enough contrast for the glass to refract, no colour cast.
    private var colors: [Color] {
        let light: [UInt32] = [0xffffff, 0xe3e3e3, 0xf6f6f6,
                               0xdadada, 0xfdfdfd, 0xdfdfdf,
                               0xf0f0f0, 0xd6d6d6, 0xfafafa]
        let dark: [UInt32] = [0x2e2e2e, 0x161616, 0x242424,
                              0x353535, 0x121212, 0x2b2b2b,
                              0x1b1b1b, 0x323232, 0x181818]
        return (colorScheme == .dark ? dark : light).map(Theme.hex)
    }

    private func mesh(at t: TimeInterval) -> some View {
        func wave(_ speed: Double, _ phase: Double, _ amplitude: Float) -> Float {
            Float(sin(t * speed + phase)) * amplitude
        }
        return MeshGradient(width: 3, height: 3, points: [
            [0, 0], [0.5 + wave(0.11, 0, 0.18), 0], [1, 0],
            [0, 0.5 + wave(0.09, 1.3, 0.16)], [0.5 + wave(0.07, 2.1, 0.2), 0.5 + wave(0.1, 0.4, 0.18)], [1, 0.5 + wave(0.08, 3.2, 0.16)],
            [0, 1], [0.5 + wave(0.1, 4.4, 0.18), 1], [1, 1],
        ], colors: colors, smoothsColors: true)
    }
}

/// Uses the OS material rather than a blur approximation. Accessibility can make it opaque.
struct LiquidSurface: ViewModifier {
    @Environment(\.accessibilityReduceTransparency) private var reduceTransparency
    @Environment(\.staticPreview) private var staticPreview
    var radius: CGFloat = 22
    var tint: Color? = nil
    var interactive = false

    @ViewBuilder
    func body(content: Content) -> some View {
        if reduceTransparency || staticPreview {
            content.background(Theme.surface, in: RoundedRectangle(cornerRadius: radius, style: .continuous))
                .overlay(RoundedRectangle(cornerRadius: radius, style: .continuous).strokeBorder(Theme.separator))
        } else {
            content.glassEffect(Glass.regular.tint(tint).interactive(interactive), in: .rect(cornerRadius: radius, style: .continuous))
        }
    }
}

/// Implicit animation that respects Reduce Motion and the static renderer.
struct MotionAnimation<Value: Equatable>: ViewModifier {
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @Environment(\.staticPreview) private var staticPreview
    let animation: Animation
    let value: Value

    func body(content: Content) -> some View {
        content.animation(reduceMotion || staticPreview ? nil : animation, value: value)
    }
}

/// Slight lift under the pointer for clickable rows and cards.
struct HoverHighlight: ViewModifier {
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @State private var hovering = false
    var radius: CGFloat = 8

    func body(content: Content) -> some View {
        content
            .background(Theme.text.opacity(hovering ? 0.045 : 0), in: RoundedRectangle(cornerRadius: radius, style: .continuous))
            .onHover { inside in
                withAnimation(reduceMotion ? nil : .easeOut(duration: 0.18)) { hovering = inside }
            }
    }
}

extension View {
    func liquidSurface(radius: CGFloat = 22, tint: Color? = nil, interactive: Bool = false) -> some View {
        modifier(LiquidSurface(radius: radius, tint: tint, interactive: interactive))
    }
    func liquidButton(prominent: Bool = false) -> some View { modifier(LiquidButton(prominent: prominent)) }
    func motion<Value: Equatable>(_ animation: Animation = .smooth(duration: 0.35), value: Value) -> some View {
        modifier(MotionAnimation(animation: animation, value: value))
    }
    func hoverHighlight(radius: CGFloat = 8) -> some View { modifier(HoverHighlight(radius: radius)) }
}

extension AnyTransition {
    /// Lists and banners: slide in a little from above, fade out in place.
    static var settle: AnyTransition {
        .asymmetric(insertion: .opacity.combined(with: .offset(y: -8)).combined(with: .scale(scale: 0.98, anchor: .top)),
                    removal: .opacity.combined(with: .scale(scale: 0.98)))
    }
}

private struct StaticPreviewKey: EnvironmentKey {
    static let defaultValue = false
}

extension EnvironmentValues {
    var staticPreview: Bool {
        get { self[StaticPreviewKey.self] }
        set { self[StaticPreviewKey.self] = newValue }
    }
}

struct LiquidButton: ViewModifier {
    @Environment(\.staticPreview) private var staticPreview
    var prominent = false

    @ViewBuilder
    func body(content: Content) -> some View {
        if staticPreview {
            if prominent { content.buttonStyle(.borderedProminent) }
            else { content.buttonStyle(.bordered) }
        } else if prominent { content.buttonStyle(.glassProminent) }
        // Secondary glass stays neutral; inheriting the accent tint made every button look primary.
        else { content.buttonStyle(.glass).tint(nil) }
    }
}

/// Groups glass buttons so they merge and split as they appear, instead of popping in.
struct GlassCluster<Content: View>: View {
    @Environment(\.staticPreview) private var staticPreview
    var spacing: CGFloat = 12
    @ViewBuilder var content: Content

    var body: some View {
        if staticPreview {
            HStack(spacing: spacing) { content }
        } else {
            GlassEffectContainer(spacing: spacing) {
                HStack(spacing: spacing) { content }
            }
        }
    }
}
