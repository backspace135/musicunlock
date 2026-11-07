import AppKit
import MusicUnlockCore
import SwiftUI
import UniformTypeIdentifiers

struct ContentView: View {
    @ObservedObject var model: AppModel
    var findLyrics: ((AudioJob) -> Void)? = nil
    @State private var dropTargeted = false
    @Namespace private var glass

    var body: some View {
        VStack(spacing: 0) {
            HStack(alignment: .top, spacing: 24) {
                queue.frame(maxWidth: .infinity, maxHeight: .infinity)
                ScrollView {
                    inspector.padding(20)
                }
                .scrollIndicators(.never)
                .scrollEdgeEffectStyle(.soft, for: .bottom)
                .frame(width: 268)
                .liquidSurface(radius: 20)
            }
            .padding(.horizontal, 24).padding(.top, 20)
            footer
        }
        .foregroundStyle(Theme.text)
        .tint(Theme.accent)
        .frame(minWidth: 880, minHeight: 600)
        .toolbar {
            ToolbarItemGroup(placement: .primaryAction) {
                Button { model.chooseFiles(folder: true) } label: { Label("添加文件夹", systemImage: "folder.badge.plus") }
                    .disabled(model.importing).help("添加文件夹（⇧⌘O）")
                Button { model.chooseFiles() } label: { Label("添加文件", systemImage: "plus") }
                    .disabled(model.importing).help("添加文件（⌘O）")
            }
        }
        .onDrop(of: [UTType.fileURL], isTargeted: $dropTargeted, perform: receiveDrop)
        .overlay {
            if dropTargeted { dropOverlay.transition(.opacity.combined(with: .scale(scale: 1.03))) }
        }
        .motion(.spring(duration: 0.35, bounce: 0.25), value: dropTargeted)
    }

    /// A tinted glass sheet over the window while files hover, so the target is the whole app.
    private var dropOverlay: some View {
        VStack(spacing: 12) {
            Image(systemName: "square.and.arrow.down.on.square")
                .font(.system(size: 38, weight: .light)).foregroundStyle(Theme.accent)
                .symbolEffect(.bounce.up, options: .repeat(.continuous))
            Text("松开即可加入转换队列").font(.system(size: 17, weight: .semibold))
            Text("支持文件与整个文件夹").font(.system(size: 12)).foregroundStyle(Theme.secondary)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .liquidSurface(radius: 26, tint: Theme.accent.opacity(0.12))
        .overlay(RoundedRectangle(cornerRadius: 26).strokeBorder(Theme.accent.opacity(0.5), style: StrokeStyle(lineWidth: 2, dash: [8, 6])))
        .padding(12).allowsHitTesting(false)
    }

    private var queue: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack(alignment: .center, spacing: 12) {
                Text("转换队列").font(.system(size: 20, weight: .semibold))
                if !model.jobs.isEmpty {
                    Text("\(model.jobs.count) 个文件").font(.system(size: 12)).foregroundStyle(Theme.secondary)
                        .contentTransition(.numericText(value: Double(model.jobs.count)))
                }
                Spacer()
                if !model.jobs.isEmpty { queueFilters.transition(.opacity.combined(with: .scale(scale: 0.95))) }
                Menu {
                    Button("清除已完成", action: model.clearCompleted).disabled(model.doneCount == 0)
                    Button("清空队列", action: model.clearQueue).disabled(model.running || model.jobs.isEmpty)
                } label: {
                    Image(systemName: "ellipsis.circle").font(.system(size: 15))
                }
                .menuStyle(.borderlessButton).menuIndicator(.hidden).fixedSize()
                .foregroundStyle(Theme.secondary)
                .help("整理队列").accessibilityLabel("整理队列")
            }
            .frame(minHeight: 28)

            if let notice = model.notice {
                HStack(alignment: .top, spacing: 8) {
                    Image(systemName: "info.circle")
                    Text(notice).font(.system(size: 12)).fixedSize(horizontal: false, vertical: true)
                    Spacer(minLength: 4)
                    Button { model.notice = nil } label: { Image(systemName: "xmark") }
                        .buttonStyle(.plain).help("关闭提示").accessibilityLabel("关闭提示")
                }
                .foregroundStyle(Theme.secondary).padding(12)
                .liquidSurface(radius: 12, tint: Theme.accent.opacity(0.08))
                .transition(.settle)
            }
            if model.importing {
                HStack(spacing: 8) { ProgressView().controlSize(.small); Text("正在扫描文件…").font(.system(size: 12)) }
                    .foregroundStyle(Theme.secondary).transition(.settle)
            }
            Group {
                if model.jobs.isEmpty { emptyQueue.transition(.blurReplace) }
                else if model.visibleJobs.isEmpty { emptyFilter.transition(.blurReplace) }
                else { fileList.transition(.blurReplace) }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .motion(.smooth(duration: 0.35), value: model.notice)
        .motion(.smooth(duration: 0.3), value: model.importing)
        .motion(.smooth(duration: 0.4), value: model.jobs.isEmpty)
        .motion(.smooth(duration: 0.3), value: model.filter)
        .motion(.snappy, value: model.jobs.count)
    }

    private var emptyQueue: some View {
        VStack(spacing: 0) {
            Spacer(minLength: 20)
            FloatingDisc(lifted: dropTargeted).padding(.bottom, 12)
            Text("把音乐放进来").font(.system(size: 22, weight: .medium)).padding(.bottom, 10)
            Text("拖入文件或整个文件夹\n恢复为普通播放器也能打开的音频")
                .font(.system(size: 13)).foregroundStyle(Theme.secondary)
                .multilineTextAlignment(.center).lineSpacing(5)
                .fixedSize(horizontal: false, vertical: true).padding(.bottom, 24)
            Button { model.chooseFiles() } label: { Label("选择音乐文件", systemImage: "plus") }
                .controlSize(.large).liquidButton(prominent: true).disabled(model.importing)
            HStack(spacing: 8) {
                ForEach(["NCM", "QMC", "MFLAC", "MGG"], id: \.self) { format in
                    Text(format).font(.system(size: 10, weight: .medium))
                        .padding(.horizontal, 8).padding(.vertical, 4)
                        .background(Theme.text.opacity(0.055), in: RoundedRectangle(cornerRadius: 5))
                }
            }
            .foregroundStyle(Theme.secondary).padding(.top, 24)
            Spacer(minLength: 20)
            Label("仅在你的 Mac 上处理，不上传文件", systemImage: "internaldrive")
                .font(.system(size: 11)).foregroundStyle(Theme.secondary).padding(.bottom, 22)
        }
        .frame(maxWidth: .infinity)
        .overlay(RoundedRectangle(cornerRadius: 14).strokeBorder(Theme.secondary.opacity(0.28), style: StrokeStyle(lineWidth: 1, dash: [5, 5])).padding(8))
        .liquidSurface(radius: 22)
    }

    private var emptyFilter: some View {
        VStack(spacing: 12) {
            Image(systemName: model.filter == .done ? "checkmark.circle" : "tray")
                .font(.system(size: 32, weight: .light)).foregroundStyle(Theme.secondary)
                .symbolEffect(.bounce, value: model.filter)
            Text("暂无\(model.filter.rawValue)的文件").font(.system(size: 14))
            Button("查看全部", action: { model.filter = .all }).buttonStyle(.link)
        }
    }

    private var fileList: some View {
        VStack(spacing: 0) {
            HStack {
                Text("文件").frame(maxWidth: .infinity, alignment: .leading)
                Text("格式").frame(width: 76, alignment: .leading)
                Text("状态").frame(width: 100, alignment: .leading)
                Color.clear.frame(width: 26, height: 1)
            }
            .font(.system(size: 11, weight: .medium)).foregroundStyle(Theme.secondary)
            .padding(.horizontal, 16).padding(.vertical, 12)
            Divider()
            ScrollView {
                let visible = model.visibleJobs
                LazyVStack(spacing: 0) {
                    ForEach(visible) { job in
                        VStack(spacing: 0) {
                            JobRow(job: job, remove: { model.remove(job.id) }, findLyrics: findLyrics.map { action in { action(job) } })
                            if job.id != visible.last?.id { Divider().padding(.leading, 68) }
                        }
                        .transition(.settle)
                    }
                }
                .padding(.vertical, 4)
                .motion(.snappy(duration: 0.3), value: visible.map(\.id))
            }
            .scrollEdgeEffectStyle(.soft, for: .top)
        }
        .clipShape(RoundedRectangle(cornerRadius: 20))
        .liquidSurface(radius: 20)
    }

    private var queueFilters: some View {
        GlassSwitch(title: "筛选队列", selection: $model.filter, options: QueueFilter.allCases, label: \.rawValue)
    }

    private var inspector: some View {
        VStack(alignment: .leading, spacing: 20) {
            Text("导出设置").font(.system(size: 15, weight: .semibold))
            VStack(alignment: .leading, spacing: 8) {
                Text("输出文件夹").font(.system(size: 12, weight: .medium))
                Button(action: model.chooseOutput) {
                    HStack(spacing: 10) {
                        Image(systemName: "folder.fill").foregroundStyle(Theme.accent).font(.system(size: 20))
                        VStack(alignment: .leading, spacing: 3) {
                            Text(model.outputDirectory.lastPathComponent).font(.system(size: 12, weight: .medium)).lineLimit(1)
                            Text(model.outputDirectory.deletingLastPathComponent().path.replacingOccurrences(of: NSHomeDirectory(), with: "~"))
                                .font(.system(size: 11)).foregroundStyle(Theme.secondary).lineLimit(1).truncationMode(.middle)
                        }
                        Spacer(minLength: 0)
                        Image(systemName: "chevron.up.chevron.down").font(.system(size: 9)).foregroundStyle(Theme.secondary)
                    }
                    .padding(12).frame(maxWidth: .infinity, alignment: .leading)
                    .background(Theme.surface, in: RoundedRectangle(cornerRadius: 8))
                    .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(Theme.separator))
                    .contentShape(Rectangle())
                }
                .buttonStyle(.plain).disabled(model.running)
                .help(model.outputDirectory.path).accessibilityLabel("更改输出文件夹：\(model.outputDirectory.path)")
            }
            VStack(alignment: .leading, spacing: 8) {
                Text("文件命名").font(.system(size: 12, weight: .medium))
                Picker("文件命名", selection: $model.naming) {
                    ForEach(NamingPolicy.allCases) { policy in Text(policy.rawValue).tag(policy) }
                }
                .pickerStyle(.menu).labelsHidden().fixedSize()
                .frame(maxWidth: .infinity, alignment: .leading).disabled(model.running)
                Text("同名文件自动编号，源文件保持不变。")
                    .font(.system(size: 11)).foregroundStyle(Theme.secondary).fixedSize(horizontal: false, vertical: true)
            }
            Divider()
            VStack(alignment: .leading, spacing: 10) {
                Label("保留原始音质", systemImage: "waveform")
                    .font(.system(size: 13, weight: .medium)).foregroundStyle(Theme.accent)
                Text("解除文件封装，保留原有编码。输出可能是 FLAC、MP3、OGG 或 M4A，不进行有损转码。")
                    .font(.system(size: 12)).foregroundStyle(Theme.secondary).lineSpacing(4)
                    .fixedSize(horizontal: false, vertical: true)
            }
            VStack(alignment: .leading, spacing: 10) {
                Text("支持的来源").font(.system(size: 12, weight: .medium))
                supportLine("网易云", "NCM / UC")
                supportLine("QQ 音乐", "QMC / MFLAC / MGG")
                supportLine("更多", "KWM / KGM v3 / XM")
                Text("新版外置密钥格式可能无法转换，结果会显示在对应文件下方。")
                    .font(.system(size: 11)).foregroundStyle(Theme.secondary).lineSpacing(3)
                    .fixedSize(horizontal: false, vertical: true)
            }
            Button("关于音匣与开源许可", action: showAbout).buttonStyle(.link).font(.system(size: 11))
                .foregroundStyle(Theme.accent)
        }
        .frame(maxHeight: .infinity, alignment: .topLeading)
    }

    private func supportLine(_ title: String, _ formats: String) -> some View {
        HStack {
            Text(title)
            Spacer()
            Text(formats).font(.system(size: 10, weight: .medium))
        }
        .font(.system(size: 11)).foregroundStyle(Theme.secondary)
    }

    private var footer: some View {
        HStack(spacing: 16) {
            VStack(alignment: .leading, spacing: 7) {
                if model.running {
                    Text(model.stopRequested ? "将在当前文件完成后停止" : "正在转换 \(model.batchFinished + 1) / \(model.batchCount)")
                        .font(.system(size: 12, weight: .medium)).monospacedDigit()
                        .contentTransition(.numericText(value: Double(model.batchFinished)))
                    ProgressView(value: Double(model.batchFinished), total: Double(max(1, model.batchCount)))
                        .frame(width: 240).accessibilityLabel("转换进度")
                } else {
                    Text(model.jobs.isEmpty ? "准备好，随时开始" : "\(model.waitingCount) 个待转换，\(model.doneCount) 个已完成\(model.failedCount > 0 ? "，\(model.failedCount) 个未完成" : "")")
                        .font(.system(size: 12, weight: .medium))
                        .contentTransition(.numericText())
                    Text(model.jobs.isEmpty ? "支持文件夹批量导入" : ByteCountFormatter.string(fromByteCount: model.totalSize, countStyle: .file))
                        .font(.system(size: 11)).foregroundStyle(Theme.secondary)
                        .contentTransition(.numericText())
                }
            }
            .transition(.blurReplace)
            Spacer()
            GlassCluster {
                if model.failedCount > 0, !model.running {
                    Button("重试未完成", action: model.retryFailed).liquidButton()
                        .glassEffectID("retry", in: glass)
                }
                if model.doneCount > 0 {
                    Button { model.revealOutput() } label: { Label("打开输出目录", systemImage: "folder") }
                        .liquidButton().glassEffectID("reveal", in: glass)
                }
                // Start and stop share an identity, so the glass morphs between them in place.
                if model.running {
                    Button(model.stopRequested ? "正在停止…" : "停止转换") { model.stopRequested = true }
                        .liquidButton().disabled(model.stopRequested).keyboardShortcut(".", modifiers: .command)
                        .glassEffectID("primary", in: glass)
                } else {
                    Button(action: model.start) { Label("开始转换", systemImage: "play.fill").frame(minWidth: 96) }
                        .liquidButton(prominent: true).disabled(model.waitingCount == 0 || model.importing)
                        .keyboardShortcut(.return, modifiers: .command)
                        .glassEffectID("primary", in: glass)
                }
            }
        }
        .controlSize(.large)
        .padding(.horizontal, 20).padding(.vertical, 12)
        .frame(minHeight: 64)
        .liquidSurface(radius: 22)
        .padding(.horizontal, 24).padding(.vertical, 16)
        .motion(.spring(duration: 0.45, bounce: 0.2), value: model.running)
        .motion(.spring(duration: 0.45, bounce: 0.2), value: model.doneCount > 0)
        .motion(.spring(duration: 0.45, bounce: 0.2), value: model.failedCount > 0)
        .motion(.smooth(duration: 0.3), value: model.batchFinished)
        .motion(.smooth(duration: 0.3), value: model.waitingCount)
    }

    private func receiveDrop(_ providers: [NSItemProvider]) -> Bool {
        for provider in providers {
            _ = provider.loadObject(ofClass: URL.self) { url, _ in
                if let url { Task { @MainActor in model.importURLs([url]) } }
            }
        }
        return !providers.isEmpty
    }
}

struct JobRow: View {
    let job: AudioJob
    let remove: () -> Void
    var findLyrics: (() -> Void)? = nil

    private var statusColor: Color {
        switch job.state {
        case .done: return Theme.success
        case .failed: return Theme.error
        case .running: return Theme.accent
        case .waiting: return Theme.secondary
        }
    }

    var body: some View {
        HStack(alignment: .top, spacing: 12) {
            Group {
                if let cover = job.cover {
                    Image(nsImage: cover).resizable().scaledToFill()
                } else {
                    ZStack {
                        RoundedRectangle(cornerRadius: 7).fill(Theme.accent.opacity(0.08))
                        Image(systemName: "music.note").font(.system(size: 17, weight: .medium)).foregroundStyle(Theme.accent)
                    }
                }
            }
            .frame(width: 36, height: 40).clipShape(RoundedRectangle(cornerRadius: 7)).accessibilityHidden(true)
            VStack(alignment: .leading, spacing: 5) {
                Text(job.title.isEmpty ? job.source.deletingPathExtension().lastPathComponent : job.title)
                    .font(.system(size: 13, weight: .medium)).lineLimit(1).help(job.source.lastPathComponent)
                Text("\(AudioDecoder.platform(for: job.source.pathExtension)) · \(ByteCountFormatter.string(fromByteCount: job.size, countStyle: .file))")
                    .font(.system(size: 11)).foregroundStyle(Theme.secondary)
                if job.state == .failed {
                    Text(job.detail).font(.system(size: 11)).foregroundStyle(Theme.error).lineSpacing(3)
                        .fixedSize(horizontal: false, vertical: true).textSelection(.enabled)
                        .transition(.opacity.combined(with: .offset(y: -4)))
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            VStack(alignment: .leading, spacing: 5) {
                Text(job.source.pathExtension.uppercased()).font(.system(size: 10, weight: .semibold))
                if !job.outputExtension.isEmpty {
                    Label(job.outputExtension.uppercased(), systemImage: "arrow.turn.down.right")
                        .font(.system(size: 10)).foregroundStyle(Theme.secondary)
                        .transition(.opacity.combined(with: .move(edge: .leading)))
                }
            }
            .frame(width: 64, alignment: .leading).padding(.top, 3)
            HStack(spacing: 5) {
                if job.state == .running { ProgressView().controlSize(.mini).frame(width: 12).transition(.scale.combined(with: .opacity)) }
                else {
                    Image(systemName: job.state.symbol).font(.system(size: 12))
                        .contentTransition(.symbolEffect(.replace.downUp))
                        .symbolEffect(.bounce, value: job.state == .done)
                        .transition(.scale.combined(with: .opacity))
                }
                Text(job.state.label).font(.system(size: 11)).contentTransition(.interpolate)
            }
            .foregroundStyle(statusColor).frame(width: 88, alignment: .leading).padding(.top, 3)
            Group {
                if let output = job.output {
                    Button { NSWorkspace.shared.activateFileViewerSelecting([output]) } label: { Image(systemName: "magnifyingglass") }
                        .help("在 Finder 中显示").accessibilityLabel("在 Finder 中显示 \(output.lastPathComponent)")
                } else {
                    Button(action: remove) { Image(systemName: "xmark") }
                        .disabled(job.state == .running).help("移出队列").accessibilityLabel("移出队列：\(job.source.lastPathComponent)")
                }
            }
            .buttonStyle(.borderless).foregroundStyle(Theme.secondary).frame(width: 24, height: 24)
        }
        .padding(.horizontal, 16).padding(.vertical, 15)
        .background(statusColor.opacity(job.state == .running ? 0.06 : 0))
        .hoverHighlight(radius: 0)
        .motion(.spring(duration: 0.4, bounce: 0.25), value: job.state)
        .contextMenu {
            Button("在 Finder 中显示源文件") { NSWorkspace.shared.activateFileViewerSelecting([job.source]) }
            if let output = job.output {
                Button("在 Finder 中显示输出文件") { NSWorkspace.shared.activateFileViewerSelecting([output]) }
            }
            if let findLyrics { Button("查找这首歌的歌词", action: findLyrics) }
            Divider()
            Button("移出队列", action: remove).disabled(job.state == .running)
        }
    }
}

/// The empty-state icon drifts gently and lifts when files hover over the window.
private struct FloatingDisc: View {
    var lifted = false
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @Environment(\.staticPreview) private var staticPreview

    var body: some View {
        let disc = DiscMark(size: 112)
            .scaleEffect(lifted ? 1.1 : 1)
            .rotationEffect(.degrees(lifted ? -6 : 0))
            .shadow(color: .black.opacity(lifted ? 0.22 : 0.1), radius: lifted ? 22 : 10, y: lifted ? 12 : 5)
            .animation(.spring(duration: 0.4, bounce: 0.35), value: lifted)
        if reduceMotion || staticPreview {
            disc
        } else {
            disc.phaseAnimator([false, true]) { view, up in
                view.offset(y: up ? -5 : 3)
            } animation: { _ in .easeInOut(duration: 2.8) }
        }
    }
}

@MainActor
func showAbout() {
    let alert = NSAlert()
    alert.messageText = "音匣 MusicUnlock"
    alert.informativeText = "原生 macOS 音频解封装与歌词工具\nSwiftUI · Swift\n音频本地处理，歌词直连音乐平台\n\n音匣采用 MIT 许可证\n© 2026 backspace135 and MusicUnlock contributors\n\n解密算法移植自 Unlock Music\n© 2019–2021 MengYX（MIT）\n\n歌词功能参照 MusicLyricApp 的工作流独立实现。\nTEA 算法包含 Go Authors 的 BSD-3-Clause 实现衍生代码。完整许可随 App 分发。"
    alert.addButton(withTitle: "好")
    alert.addButton(withTitle: "查看开源许可")
    if alert.runModal() == .alertSecondButtonReturn,
       let url = Bundle.main.url(forResource: "THIRD_PARTY_NOTICES", withExtension: "txt") {
        NSWorkspace.shared.open(url)
    }
}
