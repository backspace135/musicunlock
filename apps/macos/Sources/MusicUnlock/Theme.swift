import AppKit
import SwiftUI

enum Theme {
    static let background = adaptive(0xf4f6fa, 0x1b2029)
    static let surface = adaptive(0xffffff, 0x252c38)
    static let text = adaptive(0x1f2b3d, 0xf0f3f8)
    static let secondary = adaptive(0x526176, 0xadb8c8)
    static let accent = adaptive(0x315bc7, 0x8caeff)
    static let success = adaptive(0x24735c, 0x83cfad)
    static let error = adaptive(0xb13a3a, 0xffa0a0)
    static let separator = adaptive(0xe0e5ee, 0x394251)

    static func adaptive(_ light: UInt32, _ dark: UInt32) -> Color {
        Color(nsColor: NSColor(name: nil) { appearance in
            let hex = appearance.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua ? dark : light
            return NSColor(srgbRed: Double((hex >> 16) & 255) / 255,
                           green: Double((hex >> 8) & 255) / 255,
                           blue: Double(hex & 255) / 255, alpha: 1)
        })
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

/// Uses the OS material rather than a blur approximation. Accessibility can make it opaque.
struct LiquidSurface: ViewModifier {
    @Environment(\.accessibilityReduceTransparency) private var reduceTransparency
    @Environment(\.staticPreview) private var staticPreview
    var radius: CGFloat = 22

    @ViewBuilder
    func body(content: Content) -> some View {
        if reduceTransparency || staticPreview {
            content.background(Theme.surface, in: RoundedRectangle(cornerRadius: radius))
                .overlay(RoundedRectangle(cornerRadius: radius).strokeBorder(Theme.separator))
        } else {
            content.glassEffect(.regular, in: .rect(cornerRadius: radius))
        }
    }
}

extension View {
    func liquidSurface(radius: CGFloat = 22) -> some View { modifier(LiquidSurface(radius: radius)) }
    func liquidButton(prominent: Bool = false) -> some View { modifier(LiquidButton(prominent: prominent)) }
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
