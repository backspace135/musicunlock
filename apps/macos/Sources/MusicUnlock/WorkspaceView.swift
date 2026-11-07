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
                .transition(.blurReplace)
            } else {
                LyricsView(model: lyrics, accounts: lyrics.accounts)
                    .transition(.blurReplace)
            }
        }
        // One shared backdrop, so switching workspaces does not flash the colour field.
        .background { AmbientBackground().ignoresSafeArea() }
        // Let the colour field run under the titlebar so the toolbar glass has something to refract.
        .toolbarBackgroundVisibility(.hidden, for: .windowToolbar)
        .motion(.smooth(duration: 0.4), value: model.workspace)
        .toolbar {
            ToolbarItem(placement: .principal) {
                GlassSwitch(title: "工作区", selection: $model.workspace, options: AppWorkspace.allCases,
                            label: \.rawValue, symbol: \.symbol)
            }
            // The switch draws its own glass track; the shared toolbar capsule would put glass on glass.
            .sharedBackgroundVisibility(.hidden)
        }
    }
}
