#include "musicunlock/lyric_workspace.h"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "detail.h"
#include "musicunlock/text.h"

namespace mu {

namespace {

template <typename Item>
std::vector<Item> deduplicate(std::vector<Item> items) {
    std::set<std::string> seen;
    std::erase_if(items, [&](const Item& item) { return !seen.insert(item.id()).second; });
    return items;
}

template <typename Item>
std::vector<Item> joined(std::vector<Item> first, const std::vector<Item>& second) {
    first.insert(first.end(), second.begin(), second.end());
    return first;
}

std::string collection_link(const MusicCollection& collection) {
    const bool album = collection.kind == SearchKind::album;
    if (collection.provider == MusicProvider::netease) {
        return std::string("https://music.163.com/") + (album ? "album" : "playlist") + "?id=" + collection.collection_id;
    }
    return std::string("https://y.qq.com/n/ryqq/") + (album ? "albumDetail" : "playlist") + "/" + collection.collection_id;
}

}  // namespace

std::string_view lyric_tab_label(LyricTab tab) {
    switch (tab) {
        case LyricTab::translation: return "译文";
        case LyricTab::romanization: return "罗马音";
        case LyricTab::preview: return "导出预览";
        case LyricTab::original: break;
    }
    return "原文";
}

std::string format_duration(std::int64_t milliseconds) {
    if (milliseconds <= 0) return {};
    char text[32];
    std::snprintf(text, sizeof text, "%lld:%02lld", static_cast<long long>(milliseconds / 60'000),
                  static_cast<long long>(milliseconds / 1000 % 60));
    return text;
}

SearchResult run_search(const MusicApi& api, const SearchRequest& request, const std::string& cookie,
                        const CancelToken& cancel) {
    SearchResult result;
    if (request.link) {
        result.songs = api.resolve(*request.link, cookie, cancel);
    } else {
        auto page = api.search(request.text, request.provider, request.kind, request.page, cookie, cancel);
        result = {std::move(page.songs), std::move(page.collections), page.has_more};
    }
    cancel.check();
    return result;
}

ExportOutcome export_song(const MusicApi& api, const ExportJob& job, const LyricOptions& options,
                          const std::filesystem::path& directory, const std::string& cookie, const CancelToken& cancel) {
    ExportOutcome outcome{job.document ? *job.document : api.lyrics(job.song, cookie, cancel), std::nullopt};
    cancel.check();
    if (options.skip_instrumental && outcome.document.empty()) return outcome;
    const std::string text = render_lyrics(outcome.document, options, job.song.duration_ms);
    outcome.file = save_lyrics(text, job.song, options, job.index, directory);
    return outcome;
}

std::string read_lyric_file(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) throw LyricError("无法读取这个歌词文件，请确认文件仍然存在。");
    if (size > 5'000'000) throw LyricError("歌词文件超过 5 MB，请选择较小的 LRC 或 TXT。");
    Bytes data;
    try {
        data = detail::read_file(path);
    } catch (const std::exception&) {
        throw LyricError("无法读取这个歌词文件，请确认文件仍然存在。");
    }
    return decode_lyric_file(data);
}

// MARK: - Search

std::optional<SearchRequest> LyricWorkspace::search(bool more) {
    if (exporting_) return std::nullopt;
    const std::string text = more ? (last_search_ ? last_search_->text : std::string()) : std::string(trim_space(query_));
    if (text.empty()) {
        notice_ = "输入歌曲、歌手，或粘贴歌曲 / 专辑 / 歌单的完整链接。";
        return std::nullopt;
    }
    SearchRequest request;
    request.serial = ++search_serial_;
    request.text = text;
    request.provider = more ? last_search_->provider : provider_;
    request.kind = more ? last_search_->kind : kind_;
    request.page = more ? page_ + 1 : 1;
    request.more = more;
    const std::string requested_title = std::exchange(pending_collection_title_, {});
    searching_ = true;
    notice_.clear();
    if (more) return request;
    forget_lyrics_request();
    selected_.reset();
    document_ = {};
    songs_.clear();
    collections_.clear();
    checked_.clear();
    collection_title_.clear();
    has_more_ = false;
    if (auto link = MusicLink::parse(text, request.provider, request.kind)) {
        provider_ = request.provider = link->provider;
        kind_ = request.kind = link->kind;
        if (link->kind != SearchKind::song) {
            request.collection_title = !requested_title.empty()
                ? requested_title
                : std::string(provider_title(link->provider)) + std::string(search_kind_label(link->kind));
        }
        collection_title_ = requested_title;  // known when opened from the list
        request.link = std::move(link);
    } else if (text.find("http://") != std::string::npos || text.find("https://") != std::string::npos) {
        ++search_serial_;
        searching_ = false;
        notice_ = "暂不支持这个链接。请使用 music.163.com 或 y.qq.com 的完整歌曲、专辑、歌单链接；短链接可先在浏览器打开后复制完整地址。";
        return std::nullopt;
    }
    return request;
}

std::optional<SearchRequest> LyricWorkspace::open(const MusicCollection& collection) {
    if (exporting_) return std::nullopt;
    query_ = collection_link(collection);
    provider_ = collection.provider;
    kind_ = collection.kind;
    pending_collection_title_ = collection.title;
    return search();
}

std::optional<SearchRequest> LyricWorkspace::search_for_audio(std::string title) {
    if (exporting_) return std::nullopt;
    query_ = std::move(title);
    kind_ = SearchKind::song;
    return search();
}

std::optional<LyricsRequest> LyricWorkspace::search_finished(const SearchRequest& request, SearchResult result) {
    if (request.serial != search_serial_ || !searching_) return std::nullopt;
    searching_ = false;
    if (request.link) {
        songs_ = deduplicate(std::move(result.songs));
        collections_.clear();
        has_more_ = false;
        collection_title_ = request.collection_title;
    } else {
        songs_ = deduplicate(request.more ? joined(songs_, result.songs) : std::move(result.songs));
        collections_ = deduplicate(request.more ? joined(collections_, result.collections) : std::move(result.collections));
        has_more_ = result.has_more;
        page_ = request.page;
        last_search_ = request;
    }
    if (songs_.empty() && collections_.empty()) notice_ = "没有找到结果。试试歌曲名加歌手，或切换音乐来源。";
    if (!request.more && !songs_.empty()) return select(songs_.front().id());
    return std::nullopt;
}

void LyricWorkspace::search_failed(const SearchRequest& request, std::string message) {
    if (request.serial != search_serial_ || !searching_) return;
    searching_ = false;
    notice_ = std::move(message);
}

void LyricWorkspace::cancel_search() {
    ++search_serial_;
    searching_ = false;
}

// MARK: - Lyrics

void LyricWorkspace::forget_lyrics_request() {
    ++lyrics_serial_;
    loading_lyrics_ = false;
}

void LyricWorkspace::remember(const LyricDocument& document) {
    if (selected_ && !loading_lyrics_) cache_[selected_->id()] = document;
}

std::optional<LyricsRequest> LyricWorkspace::select(const std::string& song_id, bool reload) {
    if (exporting_) return std::nullopt;
    const auto song = std::find_if(songs_.begin(), songs_.end(), [&](const LyricSong& s) { return s.id() == song_id; });
    if (song == songs_.end()) return std::nullopt;
    ++lyrics_serial_;
    selected_ = *song;
    const auto cached = cache_.find(song_id);
    document_ = cached != cache_.end() ? cached->second : LyricDocument{};
    loading_lyrics_ = cached == cache_.end() || reload;
    if (!loading_lyrics_) return std::nullopt;
    return LyricsRequest{lyrics_serial_, *song};
}

bool LyricWorkspace::can_retry() const {
    return selected_ && !selected_->song_id.starts_with("local:") && !loading_lyrics_ && !exporting_;
}

std::optional<LyricsRequest> LyricWorkspace::retry_lyrics() {
    if (!can_retry()) return std::nullopt;
    return select(selected_->id(), true);
}

void LyricWorkspace::lyrics_loaded(const LyricsRequest& request, LyricDocument document) {
    if (request.serial != lyrics_serial_ || !is_selected(request.song.id())) return;
    loading_lyrics_ = false;
    document_ = std::move(document);
    remember(document_);
    if (document_.empty()) {
        notice_ = "平台未提供“" + request.song.title + "”的歌词，可能是纯音乐。可导入本地 LRC 或手动编辑。";
    }
}

void LyricWorkspace::lyrics_failed(const LyricsRequest& request, std::string message) {
    if (request.serial != lyrics_serial_) return;
    loading_lyrics_ = false;
    notice_ = std::move(message);
}

// MARK: - Checking and editing

void LyricWorkspace::toggle(const std::string& song_id) {
    if (exporting_) return;
    if (!checked_.erase(song_id)) checked_.insert(song_id);
}

bool LyricWorkspace::all_checked() const {
    return std::all_of(songs_.begin(), songs_.end(), [&](const LyricSong& song) { return checked_.contains(song.id()); });
}

void LyricWorkspace::select_all() {
    if (exporting_) return;
    const bool all = all_checked();
    for (const auto& song : songs_) {
        if (all) checked_.erase(song.id());
        else checked_.insert(song.id());
    }
}

bool LyricWorkspace::import_local(std::string title, std::string text) {
    if (exporting_) return false;
    forget_lyrics_request();
    LyricSong song;
    song.provider = provider_;
    song.song_id = "local:" + std::to_string(++local_count_);
    song.title = std::move(title);
    song.artist = "本地歌词";
    selected_ = song;
    document_ = LyricDocument{std::move(text), "", "", ""};
    remember(document_);
    songs_.insert(songs_.begin(), std::move(song));
    notice_ = "已打开本地歌词，可编辑并导出为其他格式。";
    return true;
}

std::string LyricWorkspace::text(LyricTab tab) const {
    switch (tab) {
        case LyricTab::translation: return document_.translation;
        case LyricTab::romanization: return document_.romanization;
        case LyricTab::preview: return preview();
        case LyricTab::original: break;
    }
    return document_.original;
}

void LyricWorkspace::edit(LyricTab tab, std::string text) {
    if (exporting_ || !selected_ || loading_lyrics_) return;
    switch (tab) {
        case LyricTab::original:
            document_.original = std::move(text);
            document_.word_timed.clear();
            break;
        case LyricTab::translation: document_.translation = std::move(text); break;
        case LyricTab::romanization: document_.romanization = std::move(text); break;
        case LyricTab::preview: return;
    }
    remember(document_);
}

std::size_t LyricWorkspace::line_count() const { return parse_lyrics(document_.original).size(); }

// MARK: - Export

std::string LyricWorkspace::preview() const {
    if (!selected_) return {};
    try {
        return render_lyrics(document_, options_, selected_->duration_ms);
    } catch (const std::exception&) {
        return {};
    }
}

std::optional<std::string> LyricWorkspace::preview_error() const {
    if (!selected_ || loading_lyrics_) return std::nullopt;
    try {
        render_lyrics(document_, options_, selected_->duration_ms);
        return std::nullopt;
    } catch (const std::exception& error) {
        return std::string(error.what());
    }
}

bool LyricWorkspace::can_export_current() const {
    return selected_ && !loading_lyrics_ && !exporting_ && !preview_error();
}

std::string LyricWorkspace::copy_preview() {
    std::string text = preview();
    if (!text.empty()) notice_ = "已复制" + std::string(format_label(options_.format)) + "歌词。";
    return text;
}

std::optional<ExportBatch> LyricWorkspace::export_current() {
    if (!selected_ || loading_lyrics_) return std::nullopt;
    return begin_export({*selected_});
}

std::optional<ExportBatch> LyricWorkspace::export_checked() {
    std::vector<LyricSong> songs;
    for (const auto& song : songs_) {
        if (checked_.contains(song.id())) songs.push_back(song);
    }
    return begin_export(songs);
}

std::optional<ExportBatch> LyricWorkspace::begin_export(const std::vector<LyricSong>& songs) {
    if (exporting_ || songs.empty() || searching_) return std::nullopt;
    // Freeze the visible edit and keep an in-flight fetch from replacing it during the batch.
    if (loading_lyrics_) forget_lyrics_request();
    std::error_code error;
    std::filesystem::create_directories(output_directory_, error);
    if (!std::filesystem::is_directory(output_directory_, error)) {
        notice_ = "无法创建歌词输出目录：" + path_to_utf8(output_directory_);
        return std::nullopt;
    }
    ExportBatch batch{{}, options_, output_directory_};
    for (std::size_t i = 0; i < songs.size(); ++i) {
        const auto cached = cache_.find(songs[i].id());
        batch.jobs.push_back({songs[i], static_cast<int>(i + 1),
                              cached != cache_.end() ? std::optional(cached->second) : std::nullopt});
    }
    exporting_ = true;
    stop_requested_ = false;
    progress_ = 0;
    export_total_ = songs.size();
    skipped_ = 0;
    failures_.clear();
    exported_.clear();
    notice_.clear();
    return batch;
}

void LyricWorkspace::export_succeeded(const ExportJob& job, const ExportOutcome& outcome) {
    if (!exporting_) return;
    cache_[job.song.id()] = outcome.document;
    if (outcome.file) exported_.push_back(*outcome.file);
    else ++skipped_;
    ++progress_;
}

void LyricWorkspace::export_failed(const ExportJob& job, std::string message) {
    if (!exporting_) return;
    failures_.push_back(job.song.title + "：" + message);
    ++progress_;
}

void LyricWorkspace::end_export() {
    if (!exporting_) return;
    const bool stopped = std::exchange(stop_requested_, false);
    exporting_ = false;
    notice_ = std::string(stopped ? "已停止" : "导出完成") + "：成功 " + std::to_string(exported_.size()) + " 首，跳过 " +
              std::to_string(skipped_) + " 首，失败 " + std::to_string(failures_.size()) + " 首。" +
              (skipped_ > 0 ? "可关闭“跳过无歌词歌曲”后重试。" : "");
}

// MARK: - Wording

std::string LyricWorkspace::results_title() const { return collection_title_.empty() ? "搜索结果" : collection_title_; }

std::string LyricWorkspace::results_subtitle() const {
    if (songs_.empty()) return "支持网易云音乐与 QQ 音乐";
    return std::to_string(songs_.size()) + " 首歌曲，已选 " + std::to_string(checked_.size()) + " 首";
}

std::string LyricWorkspace::loading_text() const { return "正在读取" + std::string(provider_title(provider_)) + "…"; }

std::string LyricWorkspace::progress_text() const {
    return "正在导出 " + std::to_string(progress_) + " / " + std::to_string(export_total_);
}

std::string LyricWorkspace::failures_title() const {
    return std::to_string(failures_.size()) + " 首导出失败，展开查看";
}

std::string LyricWorkspace::export_checked_label() const { return "导出所选 " + std::to_string(checked_.size()) + " 首"; }

std::string LyricWorkspace::song_artist(const LyricSong& song) { return song.artist.empty() ? "未知歌手" : song.artist; }

std::string LyricWorkspace::song_detail(const LyricSong& song) {
    if (song.artist.empty() || song.album.empty()) return song.artist + song.album;
    return song.artist + " / " + song.album;
}

}  // namespace mu
