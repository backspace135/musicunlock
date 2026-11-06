import SwiftUI

enum AppWorkspace: String, CaseIterable, Identifiable {
    case audio = "音乐解锁", lyrics = "歌词工具"
    var id: String { rawValue }
    var symbol: String { self == .audio ? "lock.open" : "text.alignleft" }
}

struct WorkspaceView: View {
    @ObservedObject var model: AppModel
    @ObservedObject var lyrics: LyricsModel

    var body: some View {
        Group {
            if model.workspace == .audio {
                ContentView(model: model, findLyrics: { job in
                    model.workspace = .lyrics
                    lyrics.searchForAudio(job)
                })
            } else {
                LyricsView(model: lyrics, accounts: lyrics.accounts)
            }
        }
        .toolbar {
            ToolbarItem(placement: .principal) {
                Picker("工作区", selection: $model.workspace) {
                    ForEach(AppWorkspace.allCases) { workspace in
                        Label(workspace.rawValue, systemImage: workspace.symbol).tag(workspace)
                    }
                }.pickerStyle(.segmented).labelsHidden().frame(width: 224)
            }
        }
    }
}
