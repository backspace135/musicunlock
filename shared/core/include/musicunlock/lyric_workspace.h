#pragma once
// The lyrics workspace without UI: search results, the checked set, edited documents, the export
// batch and the user-facing wording. Mirrors LyricsModel.swift on macOS. Not thread-safe; owned by
// the UI thread, which runs the returned requests in the background and reports the results back.
// Results of a request that has since been replaced or cancelled are ignored.

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "musicunlock/lyrics.h"
#include "musicunlock/music_api.h"

namespace mu {

enum class LyricTab { original, translation, romanization, preview };
inline constexpr LyricTab all_lyric_tabs[] = {LyricTab::original, LyricTab::translation, LyricTab::romanization,
                                              LyricTab::preview};
std::string_view lyric_tab_label(LyricTab tab);  // 原文 / 译文 / 罗马音 / 导出预览

/// "3:36"; empty for unknown durations.
std::string format_duration(std::int64_t milliseconds);

/// A search or link resolution to run in the background.
struct SearchRequest {
    std::uint64_t serial = 0;
    std::string text;
    MusicProvider provider = MusicProvider::netease;
    SearchKind kind = SearchKind::song;
    int page = 1;
    bool more = false;
    std::optional<MusicLink> link;  // resolve this instead of searching
    std::string collection_title;   // shown above the songs of a resolved album or playlist
};

struct SearchResult {
    std::vector<LyricSong> songs;
    std::vector<MusicCollection> collections;
    bool has_more = false;
};

/// Runs on a background thread. Throws LyricError or Cancelled.
SearchResult run_search(const MusicApi& api, const SearchRequest& request, const std::string& cookie,
                        const CancelToken& cancel = {});

/// Lyrics of one song to fetch in the background.
struct LyricsRequest {
    std::uint64_t serial = 0;
    LyricSong song;
};

struct ExportJob {
    LyricSong song;
    int index = 1;                         // ${index} in the file name template
    std::optional<LyricDocument> document;  // edited or already fetched; fetched otherwise
};

/// Settings and songs frozen when the batch starts, so later edits do not change it.
struct ExportBatch {
    std::vector<ExportJob> jobs;
    LyricOptions options;
    std::filesystem::path directory;
};

struct ExportOutcome {
    LyricDocument document;
    std::optional<std::filesystem::path> file;  // nullopt when skipped as having no lyrics
};

/// Fetches (unless the job has a document), renders and saves one song without overwriting.
/// Runs on a background thread. Throws LyricError or Cancelled.
ExportOutcome export_song(const MusicApi& api, const ExportJob& job, const LyricOptions& options,
                          const std::filesystem::path& directory, const std::string& cookie,
                          const CancelToken& cancel = {});

/// Reads and decodes a local LRC / TXT (see decode_lyric_file). Throws LyricError.
std::string read_lyric_file(const std::filesystem::path& path);

class LyricWorkspace {
public:
    // Search bar
    MusicProvider provider() const { return provider_; }
    SearchKind kind() const { return kind_; }
    const std::string& query() const { return query_; }
    void set_provider(MusicProvider provider) { provider_ = provider; }
    void set_kind(SearchKind kind) { kind_ = kind; }
    void set_query(std::string query) { query_ = std::move(query); }

    /// A new search (or the next page with `more`). Without a usable query it sets the notice instead.
    std::optional<SearchRequest> search(bool more = false);
    /// Lists the songs of an album or playlist.
    std::optional<SearchRequest> open(const MusicCollection& collection);
    /// "查找这首歌的歌词" from the audio queue: searches songs by the track's title.
    std::optional<SearchRequest> search_for_audio(std::string title);
    /// Applies the results; returns the lyrics to fetch for the first song of a new search.
    std::optional<LyricsRequest> search_finished(const SearchRequest& request, SearchResult result);
    void search_failed(const SearchRequest& request, std::string message);
    void cancel_search();
    bool searching() const { return searching_; }
    bool has_more() const { return has_more_; }

    const std::vector<LyricSong>& songs() const { return songs_; }
    const std::vector<MusicCollection>& collections() const { return collections_; }
    const LyricSong* selected() const { return selected_ ? &*selected_ : nullptr; }
    bool is_selected(const std::string& id) const { return selected_ && selected_->id() == id; }

    /// Previews a song (separate from checking it). Returns the lyrics to fetch when not cached.
    std::optional<LyricsRequest> select(const std::string& song_id, bool reload = false);
    /// Fetches the selected song's lyrics again, replacing the edit.
    std::optional<LyricsRequest> retry_lyrics();
    bool can_retry() const;
    void lyrics_loaded(const LyricsRequest& request, LyricDocument document);
    void lyrics_failed(const LyricsRequest& request, std::string message);
    bool loading_lyrics() const { return loading_lyrics_; }

    // Batch selection
    void toggle(const std::string& song_id);
    /// Checks every listed song, or unchecks them all when they already are.
    void select_all();
    bool all_checked() const;
    bool is_checked(const std::string& song_id) const { return checked_.contains(song_id); }
    std::size_t checked_count() const { return checked_.size(); }

    /// Shows a local file's text as a new song at the top of the list.
    bool import_local(std::string title, std::string text);

    // Editor
    const LyricDocument& document() const { return document_; }
    /// The editable text of a tab; the export preview for LyricTab::preview.
    std::string text(LyricTab tab) const;
    /// Editing the original drops the platform's word timing, which no longer matches.
    void edit(LyricTab tab, std::string text);
    std::size_t line_count() const;

    // Export
    const LyricOptions& options() const { return options_; }
    void set_options(LyricOptions options) { options_ = std::move(options); }
    const std::filesystem::path& output_directory() const { return output_directory_; }
    void set_output_directory(std::filesystem::path directory) { output_directory_ = std::move(directory); }
    /// Rendered with the current options; empty when nothing is selected or the options are invalid.
    std::string preview() const;
    /// Why the current options cannot be exported, e.g. "请至少选择一种歌词内容。".
    std::optional<std::string> preview_error() const;
    bool can_export_current() const;
    /// The preview text for the clipboard (sets the "已复制" notice), or empty.
    std::string copy_preview();

    /// Creates the output folder and starts a serial batch; nullopt (maybe with a notice) when it cannot.
    std::optional<ExportBatch> export_current();
    std::optional<ExportBatch> export_checked();
    void export_succeeded(const ExportJob& job, const ExportOutcome& outcome);
    void export_failed(const ExportJob& job, std::string message);
    void request_stop() { stop_requested_ = exporting_; }
    bool stop_requested() const { return stop_requested_; }
    /// Ends the batch and sets the summary notice.
    void end_export();
    bool exporting() const { return exporting_; }
    std::size_t progress() const { return progress_; }
    std::size_t export_total() const { return export_total_; }
    const std::vector<std::string>& failures() const { return failures_; }
    const std::vector<std::filesystem::path>& exported() const { return exported_; }

    const std::string& notice() const { return notice_; }
    /// Errors from outside the model, such as an unreadable local file.
    void show_notice(std::string text) { notice_ = std::move(text); }
    void dismiss_notice() { notice_.clear(); }

    // Wording
    std::string results_title() const;     // album / playlist name, or 搜索结果
    std::string results_subtitle() const;  // 2 首歌曲，已选 1 首
    std::string loading_text() const;      // 正在读取网易云音乐…
    std::string progress_text() const;     // 正在导出 1 / 3
    std::string failures_title() const;    // 2 首导出失败，展开查看
    std::string export_checked_label() const;  // 导出所选 2 首
    static std::string song_artist(const LyricSong& song);  // 未知歌手 when unknown
    static std::string song_detail(const LyricSong& song);  // 歌手 / 专辑

private:
    std::optional<ExportBatch> begin_export(const std::vector<LyricSong>& songs);
    void remember(const LyricDocument& document);
    void forget_lyrics_request();

    MusicProvider provider_ = MusicProvider::netease;
    SearchKind kind_ = SearchKind::song;
    std::string query_;
    std::vector<LyricSong> songs_;
    std::vector<MusicCollection> collections_;
    std::optional<LyricSong> selected_;
    std::set<std::string> checked_;
    LyricDocument document_;
    std::map<std::string, LyricDocument> cache_;
    LyricOptions options_;
    std::filesystem::path output_directory_;
    std::string collection_title_;
    std::string pending_collection_title_;
    std::string notice_;

    bool searching_ = false;
    bool has_more_ = false;
    int page_ = 1;
    std::optional<SearchRequest> last_search_;
    std::uint64_t search_serial_ = 0;

    bool loading_lyrics_ = false;
    std::uint64_t lyrics_serial_ = 0;
    std::uint64_t local_count_ = 0;

    bool exporting_ = false;
    bool stop_requested_ = false;
    std::size_t progress_ = 0;
    std::size_t export_total_ = 0;
    std::size_t skipped_ = 0;
    std::vector<std::string> failures_;
    std::vector<std::filesystem::path> exported_;
};

}  // namespace mu
