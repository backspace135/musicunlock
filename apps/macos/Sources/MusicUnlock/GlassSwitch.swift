import SwiftUI

/// Liquid Glass switch in the manner of the iOS segmented control. A lens slides behind the labels,
/// so it never covers them, and stretches along its path like a drop. Press to lift the lens; drag
/// across the track to slide it. Segments show a symbol, or the label as text when there is none.
/// On a glass card (`onGlass`) the track and lens are plain surfaces, so glass never sits on glass.
/// VoiceOver and the off-screen renderer see a native segmented picker.
struct GlassSwitch<Value: Hashable>: View {
    let title: String
    @Binding var selection: Value
    let options: [Value]
    let label: (Value) -> String
    var symbol: ((Value) -> String)? = nil
    var onGlass = false
    /// Stretch segments to fill the offered width.
    var fill = false

    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @Environment(\.accessibilityReduceTransparency) private var reduceTransparency
    @Environment(\.staticPreview) private var staticPreview
    @Namespace private var segments
    @State private var frames: [Value: CGRect] = [:]
    @State private var pressed = false
    @State private var hovered: Value?
    @State private var bounces: [Value: Int] = [:]

    private let inset: CGFloat = 3
    private let space = "GlassSwitch"
    private var animated: Bool { !reduceMotion && !staticPreview }
    private var plain: Bool { onGlass || reduceTransparency || staticPreview }

    var body: some View {
        HStack(spacing: 0) {
            ForEach(options, id: \.self) { option in
                segment(option)
                    .frame(maxWidth: fill ? .infinity : nil)
                    .contentShape(Capsule())
                    .matchedGeometryEffect(id: option, in: segments, isSource: true)
                    .onGeometryChange(for: CGRect.self) { $0.frame(in: .named(space)) } action: { frames[option] = $0 }
                    .onHover { inside in
                        if inside { hovered = option } else if hovered == option { hovered = nil }
                    }
                    .help(symbol == nil ? "" : label(option))
            }
        }
        .background { lens.matchedGeometryEffect(id: selection, in: segments, isSource: false) }
        .padding(inset)
        .background { track }
        .contentShape(Capsule())
        .coordinateSpace(.named(space))
        // One gesture for both: a press selects at once, a drag keeps following the pointer.
        .gesture(DragGesture(minimumDistance: 0, coordinateSpace: .named(space))
            .onChanged { drag in pressed = true; select(at: drag.location.x) }
            .onEnded { drag in pressed = false; select(at: drag.location.x) })
        .animation(animated ? GlassMotion.lens : nil, value: selection)
        .animation(animated ? GlassMotion.press : nil, value: pressed)
        .animation(animated ? .easeOut(duration: 0.1) : nil, value: hovered)
        .sensoryFeedback(.levelChange, trigger: selection)
        .accessibilityRepresentation {
            Picker(title, selection: $selection) {
                ForEach(options, id: \.self) { option in Text(label(option)).tag(option) }
            }
            .pickerStyle(.segmented)
        }
    }

    @ViewBuilder
    private func segment(_ option: Value) -> some View {
        let style = option == selection || option == hovered ? Theme.text : Theme.secondary
        if let symbol {
            Image(systemName: symbol(option))
                .font(.system(size: 14, weight: .medium)).foregroundStyle(style)
                .symbolEffect(.bounce.down, value: bounces[option, default: 0])
                .frame(width: 46, height: 28)
        } else {
            Text(label(option))
                .font(.system(size: 12, weight: option == selection ? .semibold : .regular)).foregroundStyle(style)
                .lineLimit(1).fixedSize()
                .padding(.horizontal, 12).frame(height: 24)
        }
    }

    private func select(at x: CGFloat) {
        // The segment under the pointer, or the nearest one once the drag leaves the track.
        let distance = { (option: Value) -> CGFloat in
            guard let frame = frames[option] else { return .infinity }
            return x < frame.minX ? frame.minX - x : max(0, x - frame.maxX)
        }
        guard let option = options.min(by: { distance($0) < distance($1) }), option != selection else { return }
        selection = option
        bounces[option, default: 0] += 1
    }

    @ViewBuilder
    private var track: some View {
        if onGlass {
            Capsule().fill(Theme.text.opacity(0.06))
        } else if plain {
            Capsule().fill(Theme.surface).overlay(Capsule().strokeBorder(Theme.separator))
        } else {
            Color.clear.glassEffect(.regular, in: .capsule)
        }
    }

    @ViewBuilder
    private var lens: some View {
        let body = Group {
            if onGlass {
                Capsule().fill(Theme.adaptive(0xffffff, 0x484848)).shadow(color: .black.opacity(pressed ? 0.16 : 0.1), radius: pressed ? 6 : 2, y: 1)
            } else if plain {
                Capsule().fill(Theme.text.opacity(0.1))
            } else {
                Color.clear.glassEffect(.regular.tint(Theme.text.opacity(0.08)).interactive(), in: .capsule)
            }
        }
        .scaleEffect(pressed ? 1.1 : 1)
        if animated {
            // Squash and stretch on each move; the spring above carries the lens across.
            body.keyframeAnimator(initialValue: CGSize(width: 1, height: 1), trigger: selection) { content, scale in
                content.scaleEffect(x: scale.width, y: scale.height)
            } keyframes: { _ in
                KeyframeTrack(\.width) {
                    SpringKeyframe(1.3, duration: 0.09)
                    SpringKeyframe(0.95, duration: 0.1)
                    SpringKeyframe(1, duration: 0.16)
                }
                KeyframeTrack(\.height) {
                    SpringKeyframe(0.82, duration: 0.09)
                    SpringKeyframe(1.05, duration: 0.1)
                    SpringKeyframe(1, duration: 0.16)
                }
            }
        } else {
            body
        }
    }
}
