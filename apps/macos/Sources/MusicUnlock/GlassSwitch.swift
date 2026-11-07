import SwiftUI

/// Liquid Glass switch. A glass lens slides behind the symbols, so it never covers them, and
/// stretches along its path like a drop. Press to lift the lens; drag across the track to slide it.
/// VoiceOver and the off-screen renderer see a native segmented picker.
struct GlassSwitch<Value: Hashable>: View {
    let title: String
    @Binding var selection: Value
    let options: [Value]
    let label: (Value) -> String
    let symbol: (Value) -> String

    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @Environment(\.accessibilityReduceTransparency) private var reduceTransparency
    @Environment(\.staticPreview) private var staticPreview
    @Namespace private var segments
    @State private var pressed = false
    @State private var hovered: Value?
    @State private var bounces: [Value: Int] = [:]

    private let segment = CGSize(width: 46, height: 28)
    private let inset: CGFloat = 3
    private var animated: Bool { !reduceMotion && !staticPreview }

    var body: some View {
        HStack(spacing: 0) {
            ForEach(options, id: \.self) { option in
                Image(systemName: symbol(option))
                    .font(.system(size: 14, weight: .medium))
                    .foregroundStyle(option == selection || option == hovered ? Theme.text : Theme.secondary)
                    .symbolEffect(.bounce.down, value: bounces[option, default: 0])
                    .frame(width: segment.width, height: segment.height)
                    .matchedGeometryEffect(id: option, in: segments, isSource: true)
                    .onHover { inside in
                        if inside { hovered = option } else if hovered == option { hovered = nil }
                    }
                    .help(label(option))
            }
        }
        .background { lens.matchedGeometryEffect(id: selection, in: segments, isSource: false) }
        .padding(inset)
        .background { track }
        .contentShape(Capsule())
        // One gesture for both: a press selects at once, a drag keeps following the pointer.
        .gesture(DragGesture(minimumDistance: 0)
            .onChanged { drag in pressed = true; select(at: drag.location.x) }
            .onEnded { drag in pressed = false; select(at: drag.location.x) })
        .animation(animated ? .spring(duration: 0.42, bounce: 0.3) : nil, value: selection)
        .animation(animated ? .spring(duration: 0.3, bounce: 0.45) : nil, value: pressed)
        .animation(animated ? .easeOut(duration: 0.15) : nil, value: hovered)
        .sensoryFeedback(.levelChange, trigger: selection)
        .accessibilityRepresentation {
            Picker(title, selection: $selection) {
                ForEach(options, id: \.self) { option in Text(label(option)).tag(option) }
            }
            .pickerStyle(.segmented)
        }
    }

    private func select(at x: CGFloat) {
        let index = min(max(Int((x - inset) / segment.width), 0), options.count - 1)
        let option = options[index]
        guard option != selection else { return }
        selection = option
        bounces[option, default: 0] += 1
    }

    @ViewBuilder
    private var track: some View {
        if reduceTransparency || staticPreview {
            Capsule().fill(Theme.surface).overlay(Capsule().strokeBorder(Theme.separator))
        } else {
            Color.clear.glassEffect(.regular, in: .capsule)
        }
    }

    @ViewBuilder
    private var lens: some View {
        let body = Group {
            if reduceTransparency || staticPreview {
                Capsule().fill(Theme.text.opacity(0.1))
            } else {
                Color.clear.glassEffect(.regular.tint(Theme.text.opacity(0.08)).interactive(), in: .capsule)
            }
        }
        .scaleEffect(pressed ? 1.12 : 1)
        if animated {
            // Squash and stretch on each move; the spring above carries the lens across.
            body.keyframeAnimator(initialValue: CGSize(width: 1, height: 1), trigger: selection) { content, scale in
                content.scaleEffect(x: scale.width, y: scale.height)
            } keyframes: { _ in
                KeyframeTrack(\.width) {
                    SpringKeyframe(1.32, duration: 0.14)
                    SpringKeyframe(0.94, duration: 0.16)
                    SpringKeyframe(1, duration: 0.24)
                }
                KeyframeTrack(\.height) {
                    SpringKeyframe(0.8, duration: 0.14)
                    SpringKeyframe(1.06, duration: 0.16)
                    SpringKeyframe(1, duration: 0.24)
                }
            }
        } else {
            body
        }
    }
}
