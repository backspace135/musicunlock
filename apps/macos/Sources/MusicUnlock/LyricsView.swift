import AppKit
import MusicUnlockCore
import SwiftUI

struct LyricsView: View {
    @ObservedObject var model: LyricsModel
    @ObservedObject var accounts: MusicAccounts
    @State private var editor = LyricEditorTab.original
    @FocusState private var searchFocused: Bool
    @Namespace private var glass
    @Namespace private var selection

    var body: some View {
        VStack(spacing: 16) {
            searchBar
            if let message = model.notice ?? accounts.message {
                HStack(alignment: .top, spacing: 8) {
                    Image(systemName: "info.circle")
                    Text(message).font(.system(size: 12)).textSelection(.enabled).fixedSize(horizontal: false, vertical: true)
                    Spacer(minLength: 4)
                    Button {
                        model.notice = nil; accounts.message = nil
                    } label: { Image(systemName: "xmark") }
                    .buttonStyle(.plain).accessibilityLabel("关闭提示")
                }.foregroundStyle(Theme.secondary).padding(12)
                    .liquidSurface(radius: 12, tint: Theme.accent.opacity(0.08))
                    .transition(.settle)
            }
            HStack(alignment: .top, spacing: 18) {
                results.frame(width: 300)
                lyricEditor.frame(maxWidth: .infinity, maxHeight: .infinity)
            }.frame(maxHeight: .infinity)
            footer
        }
        .padding(24).padding(.top, -4)
        .foregroundStyle(Theme.text).tint(Theme.accent)
        .motion(.smooth(duration: 0.35), value: model.notice ?? accounts.message)
        .frame(minWidth: 880, minHeight: 600)
        .toolbar {
            ToolbarItemGroup(placement: .primaryAction) {
                Button { model.importLyrics() } label: { Label("打开歌词", systemImage: "doc.badge.plus") }
                    .disabled(model.exporting).help("打开本地 LRC 或 TXT")
                Menu {
                    ForEach(MusicProvider.allCases) { provider in
                        if accounts.signedIn.contains(provider) {
                            Text("\(provider.title)：会话已保存")
                            Button("重新登录\(provider.title)") { accounts.loginProvider = provider }
                            Button("退出\(provider.title)") { accounts.logout(provider) }
                        } else {
                            Button("登录\(provider.title)") { accounts.loginProvider = provider }
                        }
                        if provider != MusicProvider.allCases.last { Divider() }
                    }
                } label: { Label("账号登录", systemImage: accounts.signedIn.isEmpty ? "person.crop.circle" : "person.crop.circle.badge.checkmark") }
                    .help("官方登录后自动保存会话，无需手动获取 Cookie")
            }
        }
        .sheet(item: $accounts.loginProvider) { provider in
            MusicLoginSheet(accounts: accounts, provider: provider, api: model.api)
        }
        .sheet(isPresented: $model.showSettings) { LyricSettingsSheet(model: model) }
        .onChange(of: model.selected?.id) { editor = .original }
        .onReceive(NotificationCenter.default.publisher(for: .focusLyricSearch)) { _ in searchFocused = true }
    }

    private var searchBar: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Text("歌词工作台").font(.system(size: 20, weight: .semibold))
                Spacer()
                Label(accounts.signedIn.contains(model.provider) ? "登录会话已保存" : "公开内容可免登录读取", systemImage: accounts.signedIn.contains(model.provider) ? "checkmark.shield" : "network")
                    .font(.system(size: 11)).foregroundStyle(Theme.secondary)
                    .contentTransition(.symbolEffect(.replace))
            }
            HStack(spacing: 10) {
                Picker("音乐来源", selection: $model.provider) {
                    ForEach(MusicProvider.allCases) { source in Text(source.title).tag(source) }
                }.labelsHidden().frame(width: 136)
                Picker("搜索类型", selection: $model.kind) {
                    ForEach(MusicSearchKind.allCases) { kind in Text(kind.rawValue).tag(kind) }
                }.labelsHidden().frame(width: 82)
                TextField("歌曲、歌手，或粘贴歌曲 / 歌单 / 专辑链接", text: $model.query)
                    .textFieldStyle(.roundedBorder).focused($searchFocused)
                    .onSubmit { model.search() }.disabled(model.exporting)
                    .accessibilityLabel("歌词搜索关键词或音乐链接")
                GlassCluster {
                    if model.searching {
                        Button(action: model.cancelSearch) { Label("取消", systemImage: "xmark") }
                            .liquidButton().glassEffectID("search", in: glass)
                    } else {
                        Button(action: { model.search() }) { Label("搜索", systemImage: "magnifyingglass") }
                            .liquidButton(prominent: true).disabled(model.exporting || model.query.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
                            .glassEffectID("search", in: glass)
                    }
                }
            }.controlSize(.large)
            .motion(.spring(duration: 0.4, bounce: 0.2), value: model.searching)
        }
    }

    private var results: some View {
        VStack(spacing: 0) {
            HStack {
                VStack(alignment: .leading, spacing: 4) {
                    Text(model.collectionTitle ?? "搜索结果").font(.system(size: 13, weight: .semibold)).lineLimit(1)
                    Text(model.songs.isEmpty ? "支持网易云音乐与 QQ 音乐" : "\(model.songs.count) 首歌曲，已选 \(model.checked.count) 首")
                        .font(.system(size: 11)).foregroundStyle(Theme.secondary)
                        .contentTransition(.numericText())
                }
                Spacer(minLength: 4)
                if !model.songs.isEmpty {
                    Button(model.songs.allSatisfy { model.checked.contains($0.id) } ? "取消全选" : "全选", action: model.selectAll)
                        .buttonStyle(.link).disabled(model.exporting)
                }
            }.padding(14)
            Divider()
            if model.searching && model.songs.isEmpty && model.collections.isEmpty {
                VStack(spacing: 12) { ProgressView(); Text("正在读取\(model.provider.title)…").font(.system(size: 12)) }
                    .frame(maxWidth: .infinity, maxHeight: .infinity).foregroundStyle(Theme.secondary)
                    .transition(.blurReplace)
            } else if model.songs.isEmpty && model.collections.isEmpty {
                VStack(spacing: 14) {
                    Image(systemName: "text.magnifyingglass").font(.system(size: 34, weight: .light))
                        .symbolEffect(.wiggle, value: searchFocused)
                    Text("先找到你想要的歌").font(.system(size: 14, weight: .medium)).foregroundStyle(Theme.text)
                    Text("输入名称搜索，或粘贴完整链接。\n歌单与专辑可以批量提取歌词。")
                        .font(.system(size: 12)).multilineTextAlignment(.center).lineSpacing(4)
                    Button("打开本地歌词", action: model.importLyrics).buttonStyle(.link)
                }.foregroundStyle(Theme.secondary).padding(20).frame(maxWidth: .infinity, maxHeight: .infinity)
                    .transition(.blurReplace)
            } else {
                ScrollView {
                    LazyVStack(spacing: 0) {
                        ForEach(model.collections) { collection in
                            Button { model.open(collection) } label: {
                                HStack(spacing: 10) {
                                    Image(systemName: collection.kind == .album ? "opticaldisc" : "music.note.list")
                                        .font(.system(size: 21)).foregroundStyle(Theme.accent).frame(width: 30)
                                    VStack(alignment: .leading, spacing: 5) {
                                        Text(collection.title).font(.system(size: 13, weight: .medium)).lineLimit(2)
                                        Text(collection.subtitle).font(.system(size: 11)).foregroundStyle(Theme.secondary).lineLimit(1)
                                    }.frame(maxWidth: .infinity, alignment: .leading)
                                    Image(systemName: "chevron.right").font(.system(size: 10)).foregroundStyle(Theme.secondary)
                                }.padding(14).contentShape(Rectangle())
                            }.buttonStyle(.plain).disabled(model.exporting)
                            .hoverHighlight(radius: 10).padding(.horizontal, 6)
                            .transition(.settle)
                            Divider().padding(.leading, 54)
                        }
                        ForEach(model.songs) { song in songRow(song).transition(.settle) }
                        if model.hasMore {
                            Button("加载更多", action: { model.search(more: true) })
                                .padding(16).disabled(model.searching || model.exporting)
                        }
                    }
                    .padding(.vertical, 4)
                    .motion(.spring(duration: 0.35, bounce: 0.2), value: model.selected?.id)
                    .motion(.smooth(duration: 0.35), value: model.songs.map(\.id))
                }
                .scrollEdgeEffectStyle(.soft, for: .top)
                .transition(.blurReplace)
            }
        }
        .clipShape(RoundedRectangle(cornerRadius: 20))
        .liquidSurface(radius: 20)
        .motion(.smooth(duration: 0.35), value: model.songs.isEmpty && model.collections.isEmpty)
        .motion(.smooth(duration: 0.3), value: model.searching)
    }

    private func songRow(_ song: LyricSong) -> some View {
        HStack(alignment: .center, spacing: 10) {
            Toggle("选择 \(song.title)", isOn: Binding(get: { model.checked.contains(song.id) }, set: { _ in model.toggle(song) }))
                .toggleStyle(.checkbox).labelsHidden().disabled(model.exporting)
            Button { model.select(song) } label: {
                VStack(alignment: .leading, spacing: 5) {
                    Text(song.title).font(.system(size: 13, weight: .medium)).lineLimit(2)
                    Text(song.artist.isEmpty ? "未知歌手" : song.artist).font(.system(size: 11)).foregroundStyle(Theme.secondary).lineLimit(1)
                    HStack {
                        Text(song.album).lineLimit(1)
                        Spacer(minLength: 4)
                        if song.duration > 0 { Text(String(format: "%d:%02d", song.duration / 60_000, song.duration / 1000 % 60)).monospacedDigit() }
                    }.font(.system(size: 10)).foregroundStyle(Theme.secondary)
                }.frame(maxWidth: .infinity, alignment: .leading).contentShape(Rectangle())
            }.buttonStyle(.plain).disabled(model.exporting)
        }
        .padding(.horizontal, 10).padding(.vertical, 13)
        .hoverHighlight(radius: 10)
        // A single highlight slides between rows rather than blinking on and off.
        .background {
            if model.selected?.id == song.id {
                ZStack(alignment: .leading) {
                    RoundedRectangle(cornerRadius: 10).fill(Theme.accent.opacity(0.11))
                    Capsule().fill(Theme.accent).frame(width: 3).padding(.vertical, 12).padding(.leading, 3)
                }
                .matchedGeometryEffect(id: "selection", in: selection)
            }
        }
        .padding(.horizontal, 6)
        .contextMenu {
            Button("查看歌词") { model.select(song) }.disabled(model.exporting)
            Button(model.checked.contains(song.id) ? "取消选择" : "加入批量导出") { model.toggle(song) }.disabled(model.exporting)
        }
    }

    private var lyricEditor: some View {
        VStack(alignment: .leading, spacing: 0) {
            if let song = model.selected {
                HStack(alignment: .top, spacing: 12) {
                    VStack(alignment: .leading, spacing: 6) {
                        Text(song.title).font(.system(size: 19, weight: .semibold)).lineLimit(2).textSelection(.enabled)
                        Text([song.artist, song.album].filter { !$0.isEmpty }.joined(separator: " / "))
                            .font(.system(size: 12)).foregroundStyle(Theme.secondary).lineLimit(2).textSelection(.enabled)
                    }
                    .id(song.id).transition(.blurReplace)
                    Spacer()
                    Button { model.retryLyrics() } label: {
                        Image(systemName: "arrow.clockwise").symbolEffect(.rotate, isActive: model.loadingLyrics)
                    }
                        .buttonStyle(.borderless).help("重新获取歌词，会替换当前编辑")
                        .accessibilityLabel("重新获取歌词").disabled(model.loadingLyrics || model.exporting || song.songID.hasPrefix("local:"))
                }.padding(18)
                HStack {
                    Picker("歌词编辑区域", selection: $editor) {
                        ForEach(LyricEditorTab.allCases) { tab in Text(tab.rawValue).tag(tab) }
                    }.pickerStyle(.segmented).labelsHidden()
                }.padding(.horizontal, 18).padding(.bottom, 14)
                Divider()
                if model.loadingLyrics {
                    VStack(spacing: 12) { ProgressView(); Text("正在获取歌词…").font(.system(size: 12)) }
                        .frame(maxWidth: .infinity, maxHeight: .infinity).foregroundStyle(Theme.secondary)
                        .transition(.blurReplace)
                } else if editor == .preview {
                    if let error = model.previewError {
                        VStack(alignment: .leading, spacing: 12) {
                            Label("检查导出设置", systemImage: "slider.horizontal.3").font(.headline)
                            Text(error).font(.system(size: 13)).foregroundStyle(Theme.secondary)
                            Button("调整设置") { model.showSettings = true }
                        }.padding(24).frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
                            .transition(.opacity)
                    } else {
                        ScrollView([.vertical, .horizontal]) {
                            Text(model.preview).font(.system(size: 12, design: .monospaced)).lineSpacing(5).textSelection(.enabled)
                                .frame(maxWidth: .infinity, alignment: .topLeading).padding(18)
                        }.frame(maxWidth: .infinity, maxHeight: .infinity)
                        .transition(.opacity)
                    }
                } else {
                    TextEditor(text: editorBinding)
                        .font(.system(size: 13, design: .monospaced)).lineSpacing(5)
                        .scrollContentBackground(.hidden).padding(12)
                        .disabled(model.exporting).accessibilityLabel("编辑\(editor.rawValue)歌词，支持 LRC 时间戳")
                        .id("\(song.id)-\(editor.rawValue)")
                        .transition(.opacity.combined(with: .offset(y: 6)))
                }
                Divider()
                HStack(spacing: 10) {
                    Text(editor == .preview ? "预览与实际导出内容一致" : "可直接编辑，修改会用于本次导出")
                        .font(.system(size: 11)).foregroundStyle(Theme.secondary)
                    Spacer()
                    Text("\(LyricParser.parse(model.document.original).count) 行")
                        .font(.system(size: 11)).foregroundStyle(Theme.secondary).monospacedDigit()
                        .contentTransition(.numericText())
                }.padding(14)
            } else {
                VStack(spacing: 16) {
                    Image(systemName: "text.alignleft").font(.system(size: 42, weight: .ultraLight)).foregroundStyle(Theme.accent)
                        .symbolEffect(.breathe.pulse, options: .repeat(.continuous))
                    Text("让歌词跟上音乐").font(.system(size: 22, weight: .medium))
                    Text("选一首歌，查看原文、译文与罗马音。\n也能把歌词变成视频用的字幕。")
                        .font(.system(size: 13)).foregroundStyle(Theme.secondary).lineSpacing(5).multilineTextAlignment(.center)
                    HStack(spacing: 18) {
                        Label("LRC 歌词", systemImage: "music.note")
                        Label("SRT / VTT 字幕", systemImage: "captions.bubble")
                    }.font(.system(size: 11)).foregroundStyle(Theme.secondary).padding(.top, 8)
                }.frame(maxWidth: .infinity, maxHeight: .infinity)
                .transition(.blurReplace)
            }
        }
        .clipShape(RoundedRectangle(cornerRadius: 20))
        .liquidSurface(radius: 20)
        .motion(.smooth(duration: 0.35), value: model.selected?.id)
        .motion(.smooth(duration: 0.25), value: editor)
        .motion(.smooth(duration: 0.3), value: model.loadingLyrics)
    }

    private var editorBinding: Binding<String> {
        Binding(get: {
            switch editor {
            case .original: return model.document.original
            case .translation: return model.document.translation
            case .romanization: return model.document.romanization
            case .preview: return model.preview
            }
        }, set: { text in
            switch editor {
            case .original: model.document.original = text; model.document.wordTimed = ""
            case .translation: model.document.translation = text
            case .romanization: model.document.romanization = text
            case .preview: break
            }
        })
    }

    private var footer: some View {
        VStack(alignment: .leading, spacing: 12) {
            if !model.failures.isEmpty {
                DisclosureGroup("\(model.failures.count) 首导出失败，展开查看") {
                    ScrollView { Text(model.failures.joined(separator: "\n")).font(.system(size: 11)).textSelection(.enabled).frame(maxWidth: .infinity, alignment: .leading) }
                        .frame(maxHeight: 80).padding(.top, 6)
                }.font(.system(size: 12)).foregroundStyle(Theme.error)
                .transition(.settle)
            }
            HStack(spacing: 12) {
                if model.exporting {
                    VStack(alignment: .leading, spacing: 6) {
                        Text("正在导出 \(model.progress) / \(model.exportTotal)").font(.system(size: 12)).monospacedDigit()
                            .contentTransition(.numericText(value: Double(model.progress)))
                        ProgressView(value: Double(model.progress), total: Double(max(1, model.exportTotal)))
                    }.frame(width: 190).transition(.blurReplace)
                } else {
                    Picker("导出格式", selection: $model.options.format) {
                        ForEach(LyricFormat.allCases) { format in Text(format.rawValue).tag(format) }
                    }.labelsHidden().frame(width: 78)
                    Button { model.showSettings = true } label: { Label("导出设置", systemImage: "slider.horizontal.3") }.liquidButton()
                        .transition(.blurReplace)
                }
                Spacer(minLength: 0)
                if !model.exporting {
                    Menu {
                        Button("复制歌词", action: model.copyPreview).disabled(model.preview.isEmpty)
                        Button("打开输出文件夹", action: model.revealOutput)
                        Button("更改输出文件夹", action: model.chooseOutput)
                    } label: { Image(systemName: "ellipsis.circle") }
                        .menuStyle(.borderlessButton).menuIndicator(.hidden).fixedSize().help("更多歌词操作").accessibilityLabel("更多歌词操作")
                        .transition(.opacity)
                }
                GlassCluster {
                    if model.exporting {
                        Button("停止导出", action: model.stopExport).liquidButton()
                            .glassEffectID("primary", in: glass)
                    } else {
                        if !model.checked.isEmpty {
                            Button("导出所选 \(model.checked.count) 首", action: model.exportChecked).liquidButton()
                                .contentTransition(.numericText(value: Double(model.checked.count)))
                                .glassEffectID("batch", in: glass)
                        }
                        Button(action: model.exportCurrent) { Label("导出当前歌词", systemImage: "square.and.arrow.down") }
                            .liquidButton(prominent: true).disabled(model.selected == nil || model.loadingLyrics || model.previewError != nil)
                            .glassEffectID("primary", in: glass)
                    }
                }
            }.controlSize(.large)
        }
        .padding(16).liquidSurface(radius: 22)
        .motion(.spring(duration: 0.45, bounce: 0.2), value: model.exporting)
        .motion(.spring(duration: 0.45, bounce: 0.2), value: model.checked.isEmpty)
        .motion(.smooth(duration: 0.3), value: model.checked.count)
        .motion(.smooth(duration: 0.3), value: model.progress)
        .motion(.smooth(duration: 0.35), value: model.failures.count)
    }
}

private enum LyricEditorTab: String, CaseIterable, Identifiable {
    case original = "原文", translation = "译文", romanization = "罗马音", preview = "导出预览"
    var id: String { rawValue }
}

private struct LyricSettingsSheet: View {
    @ObservedObject var model: LyricsModel
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                Text("歌词导出设置").font(.title3.weight(.semibold))
                Spacer()
                Button("完成") { dismiss() }.keyboardShortcut(.defaultAction)
            }.padding(20)
            Divider()
            Form {
                Section("文件") {
                    Picker("格式", selection: $model.options.format) {
                        ForEach(LyricFormat.allCases) { format in Text(format.rawValue).tag(format) }
                    }
                    Picker("编码", selection: $model.options.encoding) {
                        ForEach(LyricEncoding.allCases) { value in Text(value.rawValue).tag(value) }
                    }
                    TextField("文件名", text: $model.options.filename)
                    Text("支持 ${name}、${singer}、${album}、${id}、${index}。同名文件自动编号。")
                        .font(.system(size: 11)).foregroundStyle(Theme.secondary)
                    LabeledContent("输出文件夹") {
                        Button(model.outputDirectory.lastPathComponent, action: model.chooseOutput).help(model.outputDirectory.path)
                    }
                }
                Section("歌词内容") {
                    Toggle("原文", isOn: $model.options.includeOriginal)
                    Toggle("平台译文", isOn: $model.options.includeTranslation)
                    Toggle("平台罗马音", isOn: $model.options.includeRomanization)
                    Toggle("中文拼音（Apple 系统转换）", isOn: $model.options.includePinyin)
                    Text("译文和罗马音取自音乐平台；平台未提供时可在编辑区补充。拼音为系统自动转换，多音字可能需手动校正。")
                        .font(.system(size: 11)).foregroundStyle(Theme.secondary)
                }
                Section("排版与时间") {
                    Picker("排版", selection: $model.options.layout) {
                        ForEach(LyricLayout.allCases) { value in Text(value.rawValue).tag(value) }
                    }
                    TextField("合并分隔符", text: $model.options.separator).disabled(model.options.layout != .merge)
                    Picker("LRC 毫秒精度", selection: $model.options.precision) {
                        Text("1 位").tag(1); Text("2 位").tag(2); Text("3 位").tag(3)
                    }
                    TextField("时间偏移（毫秒）", value: $model.options.offset, format: .number.grouping(.never))
                    Text("正数延后，负数提前；字幕会自动合并同一时间的多语言歌词。")
                        .font(.system(size: 11)).foregroundStyle(Theme.secondary)
                    Toggle("忽略空行", isOn: $model.options.ignoreEmpty)
                    Toggle("跳过无歌词歌曲", isOn: $model.options.skipInstrumental)
                    Toggle("逐字 LRC（仅原文，需平台提供）", isOn: $model.options.wordTiming)
                }
            }.formStyle(.grouped)
        }.frame(width: 540, height: 720).foregroundStyle(Theme.text).tint(Theme.accent)
    }
}

extension Notification.Name {
    static let focusLyricSearch = Notification.Name("MusicUnlock.focusLyricSearch")
}
