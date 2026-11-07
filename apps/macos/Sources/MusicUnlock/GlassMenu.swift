import SwiftUI

/// One row of a glass menu.
enum GlassMenuEntry {
    case item(String, checked: Bool = false, disabled: Bool = false, action: () -> Void)
    case note(String)
    case divider
}

/// The open menu, shared by the window so a panel can float above every card instead of being clipped.
@MainActor @Observable
final class GlassMenuCenter {
    struct Menu {
        let id: UUID
        let title: String
        let anchor: CGRect
        let entries: [GlassMenuEntry]
        var hidesSource: Bool
    }

    private(set) var menu: Menu?
    /// False while the glass is still the shape of its button.
    private(set) var expanded = false
    /// The panel and its button swap only where they coincide, so the two never show at once.
    private(set) var panelVisible = false
    private var sourceHidden = false
    /// Set until the panel has been measured and starts to grow.
    private var opening = false
    var animated = true

    func isShowing(_ id: UUID) -> Bool { menu?.id == id && menu?.hidesSource == true && sourceHidden }

    func toggle(_ menu: Menu) {
        if self.menu?.id == menu.id, expanded { close(); return }
        // No animation: the glass appears exactly over the button it replaces.
        var instant = Transaction(); instant.disablesAnimations = true
        withTransaction(instant) {
            expanded = false; panelVisible = true; sourceHidden = true; opening = true
            self.menu = menu
        }
    }

    func expand() {
        guard menu != nil, opening else { return }
        opening = false
        withAnimation(animated ? GlassMotion.open : nil) { expanded = true }
    }

    func close() {
        guard let id = menu?.id, panelVisible else { return }
        opening = false
        guard animated else { expanded = false; panelVisible = false; sourceHidden = false; menu = nil; return }
        // Shrink without overshoot, then cross-fade with the button in place.
        withAnimation(GlassMotion.close) { expanded = false }
        withAnimation(GlassMotion.handoff) {
            panelVisible = false; sourceHidden = false
        } completion: { [weak self] in
            if let self, self.menu?.id == id, !self.panelVisible { self.menu = nil }
        }
    }

    func choose(_ action: () -> Void) {
        close()
        action()
    }
}

/// Hosts the window's glass menus. Apply once at the root of the window content.
struct GlassMenuHost: ViewModifier {
    @State private var center = GlassMenuCenter()
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @Environment(\.controlActiveState) private var activeState

    func body(content: Content) -> some View {
        content
            .environment(center)
            .overlay { GlassMenuLayer(center: center).ignoresSafeArea() }
            .onAppear { center.animated = !reduceMotion }
            .onChange(of: reduceMotion) { center.animated = !reduceMotion }
            .onChange(of: activeState) { if activeState == .inactive { center.close() } }
    }
}

extension View {
    func glassMenuHost() -> some View { modifier(GlassMenuHost()) }
}

/// A button that opens a glass menu. The glass grows out of the button's own shape, as on iOS,
/// and shrinks back into it on close.
struct GlassMenuButton<Label: View>: View {
    let title: String
    var hidesWhileOpen = true
    let entries: () -> [GlassMenuEntry]
    @ViewBuilder var label: Label

    @Environment(GlassMenuCenter.self) private var center
    @State private var id = UUID()
    @State private var frame = CGRect.zero

    var body: some View {
        Button {
            center.toggle(.init(id: id, title: title, anchor: frame, entries: entries(), hidesSource: hidesWhileOpen))
        } label: { label }
            .onGeometryChange(for: CGRect.self) { $0.frame(in: .global) } action: { frame = $0 }
            .opacity(center.isShowing(id) ? 0 : 1)
            .accessibilityLabel(title)
    }
}

/// A menu picker drawn as a glass capsule with the current value and a chevron.
/// `onGlass` uses a plain surface so it does not put glass on a glass card.
struct GlassMenuPicker<Value: Hashable>: View {
    let title: String
    @Binding var selection: Value
    let options: [Value]
    let label: (Value) -> String
    /// Keeps neighbouring pickers steady as the value changes length.
    var minWidth: CGFloat? = nil
    var onGlass = false

    var body: some View {
        let button = GlassMenuButton(title: "\(title)：\(label(selection))", entries: {
            options.map { option in .item(label(option), checked: option == selection) { selection = option } }
        }) {
            let content = HStack(spacing: 6) {
                Text(label(selection)).lineLimit(1)
                Spacer(minLength: 0)
                Image(systemName: "chevron.up.chevron.down").font(.system(size: 9, weight: .semibold))
                    .foregroundStyle(Theme.secondary)
            }
            .frame(minWidth: minWidth, alignment: .leading)
            .fixedSize()
            if onGlass {
                // The whole capsule is the hit target, not only the glyphs.
                content.font(.system(size: 12, weight: .medium))
                    .padding(.horizontal, 12).padding(.vertical, 6)
                    .background(Theme.surface, in: Capsule())
                    .overlay(Capsule().strokeBorder(Theme.separator))
                    .contentShape(Capsule())
            } else {
                content
            }
        }
        if onGlass { button.buttonStyle(.pressable) } else { button.liquidButton() }
    }
}

private struct GlassMenuLayer: View {
    let center: GlassMenuCenter
    @Environment(\.accessibilityReduceTransparency) private var reduceTransparency
    @Namespace private var highlight
    @State private var size = CGSize.zero
    @State private var hovered: Int?
    @FocusState private var focused: Bool

    private let radius: CGFloat = 18

    var body: some View {
        GeometryReader { proxy in
            if let menu = center.menu {
                let bounds = proxy.frame(in: .global)
                let anchor = menu.anchor.offsetBy(dx: -bounds.minX, dy: -bounds.minY)
                let target = placement(for: anchor, in: proxy.size)
                let rect = center.expanded ? target : anchor
                let shape = RoundedRectangle(cornerRadius: center.expanded ? radius : min(anchor.height / 2, radius), style: .continuous)
                ZStack(alignment: .topLeading) {
                    // Clicking anywhere else closes the menu, as a system menu would.
                    Color.clear.contentShape(Rectangle()).onTapGesture { center.close() }
                    surface(shape)
                        .frame(width: rect.width, height: rect.height)
                        .offset(x: rect.minX, y: rect.minY)
                        .opacity(center.panelVisible ? 1 : 0)
                    rows(menu, minWidth: anchor.width)
                        .onGeometryChange(for: CGSize.self) { $0.size } action: { measured in
                            size = measured
                            // Grow on the very next run loop turn, once the glass has drawn at the button.
                            DispatchQueue.main.async { center.expand() }
                        }
                        .opacity(center.expanded ? 1 : 0)
                        .blur(radius: center.expanded ? 0 : 6)
                        .scaleEffect(center.expanded ? 1 : 0.9, anchor: target.minY >= anchor.maxY ? .top : .bottom)
                        // Rows only show inside the growing glass. Masked before the offset, in the rows' own space.
                        .mask(alignment: .topLeading) {
                            shape.frame(width: rect.width, height: rect.height)
                                .offset(x: rect.minX - target.minX, y: rect.minY - target.minY)
                        }
                        .offset(x: target.minX, y: target.minY)
                }
                // Once closing starts, clicks go straight through to the window again.
                .allowsHitTesting(center.expanded)
                .task(id: menu.id) {
                    hovered = nil
                    focused = true
                    // Same size as the last menu: no geometry change fires, so grow from here.
                    center.expand()
                }
            }
        }
    }

    @ViewBuilder
    private func surface(_ shape: RoundedRectangle) -> some View {
        if reduceTransparency {
            shape.fill(Theme.surface).overlay(shape.strokeBorder(Theme.separator))
                .shadow(color: .black.opacity(0.15), radius: 16, y: 6)
        } else {
            Color.clear.glassEffect(.regular, in: shape)
                .shadow(color: .black.opacity(center.expanded ? 0.12 : 0), radius: 18, y: 8)
        }
    }

    private func rows(_ menu: GlassMenuCenter.Menu, minWidth: CGFloat) -> some View {
        let checks = menu.entries.contains { if case .item(_, true, _, _) = $0 { true } else { false } }
        return VStack(alignment: .leading, spacing: 0) {
            ForEach(Array(menu.entries.enumerated()), id: \.offset) { index, entry in
                switch entry {
                case .divider:
                    Divider().padding(.horizontal, 10).padding(.vertical, 4)
                case .note(let text):
                    Text(text).font(.system(size: 11)).foregroundStyle(Theme.secondary)
                        .padding(.horizontal, 12).padding(.vertical, 5)
                case .item(let title, let checked, let disabled, let action):
                    Button { center.choose(action) } label: {
                        HStack(spacing: 8) {
                            if checks {
                                Image(systemName: "checkmark").font(.system(size: 11, weight: .semibold))
                                    .opacity(checked ? 1 : 0).frame(width: 12)
                            }
                            Text(title).font(.system(size: 13)).lineLimit(1)
                            Spacer(minLength: 0)
                        }
                        .padding(.horizontal, 12).frame(height: 30)
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain).disabled(disabled)
                    .foregroundStyle(disabled ? Theme.secondary.opacity(0.6) : Theme.text)
                    // One highlight slides between rows under the pointer or the arrow keys.
                    .background {
                        if hovered == index {
                            RoundedRectangle(cornerRadius: 12, style: .continuous).fill(Theme.text.opacity(0.08))
                                .matchedGeometryEffect(id: "highlight", in: highlight)
                        }
                    }
                    .onHover { inside in
                        if inside, !disabled { hovered = index } else if hovered == index { hovered = nil }
                    }
                    .accessibilityAddTraits(checked ? .isSelected : [])
                }
            }
        }
        .padding(6)
        .frame(minWidth: max(minWidth, 150), alignment: .leading)
        .fixedSize()
        .animation(center.animated ? GlassMotion.highlight : nil, value: hovered)
        .focusable().focused($focused).focusEffectDisabled()
        .onKeyPress(.downArrow) { move(1, in: menu); return .handled }
        .onKeyPress(.upArrow) { move(-1, in: menu); return .handled }
        .onKeyPress(.return) {
            if let hovered, case .item(_, _, false, let action) = menu.entries[hovered] { center.choose(action) }
            return .handled
        }
        .onKeyPress(.escape) { center.close(); return .handled }
        .accessibilityElement(children: .contain)
        .accessibilityLabel(menu.title)
    }

    private func move(_ step: Int, in menu: GlassMenuCenter.Menu) {
        let enabled = menu.entries.indices.filter { if case .item(_, _, false, _) = menu.entries[$0] { true } else { false } }
        guard !enabled.isEmpty else { return }
        guard let hovered, let position = enabled.firstIndex(of: hovered) else {
            self.hovered = step > 0 ? enabled.first : enabled.last; return
        }
        self.hovered = enabled[(position + step + enabled.count) % enabled.count]
    }

    /// Below the button when there is room, otherwise above; kept inside the window.
    private func placement(for anchor: CGRect, in bounds: CGSize) -> CGRect {
        let margin: CGFloat = 8, gap: CGFloat = 6
        var x = anchor.minX
        if x + size.width > bounds.width - margin { x = anchor.maxX - size.width }
        x = min(max(x, margin), bounds.width - margin - size.width)
        var y = anchor.maxY + gap
        if y + size.height > bounds.height - margin { y = anchor.minY - gap - size.height }
        return CGRect(x: x, y: max(y, margin), width: size.width, height: size.height)
    }
}
