// Lyrics workspace rules and wording, mirroring LyricsModel.swift on macOS. Requests run against a
// scripted transport; no network.
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "detail.h"
#include "musicunlock/lyric_workspace.h"
#include "musicunlock/text.h"
#include "test_support.h"

namespace fs = std::filesystem;

namespace {

mu::LyricSong song_of(std::string id, std::string title, std::int64_t duration = 0) {
    mu::LyricSong song;
    song.song_id = std::move(id);
    song.title = std::move(title);
    song.artist = "音匣示例";
    song.album = "旅途中的歌";
    song.duration_ms = duration;
    return song;
}

mu::MusicCollection collection_of(std::string id, mu::SearchKind kind, std::string title) {
    mu::MusicCollection collection;
    collection.collection_id = std::move(id);
    collection.kind = kind;
    collection.title = std::move(title);
    return collection;
}

/// Serves NetEase lyrics for every request and counts them.
struct LyricServer {
    std::shared_ptr<int> requests = std::make_shared<int>(0);
    std::string lyric = "[00:01.00]一";

    mu::MusicApi api() const {
        return mu::MusicApi([requests = requests, lyric = lyric](const mu::HttpRequest& request) {
            ++*requests;
            if (request.url.find("/api/song/lyric?") == std::string::npos) throw std::runtime_error("offline");
            return mu::HttpResponse{200, R"({"code":200,"lrc":{"lyric":")" + lyric + R"("}})", {}};
        });
    }
};

TEST(workspace_wording_and_durations) {
    CHECK_EQ(std::string(mu::lyric_tab_label(mu::LyricTab::preview)), std::string("导出预览"), "tab");
    CHECK_EQ(mu::format_duration(216'000), std::string("3:36"), "duration");
    CHECK_EQ(mu::format_duration(0), std::string(), "unknown duration");
    mu::LyricWorkspace workspace;
    CHECK_EQ(workspace.results_title(), std::string("搜索结果"), "title");
    CHECK_EQ(workspace.results_subtitle(), std::string("支持网易云音乐与 QQ 音乐"), "empty subtitle");
    CHECK_EQ(workspace.loading_text(), std::string("正在读取网易云音乐…"), "loading");
    auto song = song_of("1", "远行的旋律");
    CHECK_EQ(mu::LyricWorkspace::song_detail(song), std::string("音匣示例 / 旅途中的歌"), "detail");
    song.artist.clear();
    CHECK_EQ(mu::LyricWorkspace::song_artist(song), std::string("未知歌手"), "unknown artist");
    CHECK_EQ(mu::LyricWorkspace::song_detail(song), std::string("旅途中的歌"), "album only");
}

TEST(search_validates_input_and_resets_results) {
    mu::LyricWorkspace workspace;
    CHECK(!workspace.search());
    CHECK_EQ(workspace.notice(), std::string("输入歌曲、歌手，或粘贴歌曲 / 专辑 / 歌单的完整链接。"), "empty query");
    workspace.set_query("https://example.com/song/1");
    CHECK(!workspace.search());
    CHECK(!workspace.searching());
    CHECK(workspace.notice().starts_with("暂不支持这个链接。"));

    workspace.set_query("  远行  ");
    workspace.set_kind(mu::SearchKind::album);
    const auto request = workspace.search();
    CHECK(request && request->text == "远行" && request->kind == mu::SearchKind::album && request->page == 1 && !request->link);
    CHECK(workspace.searching());
    CHECK(workspace.notice().empty());
    mu::SearchResult result;
    result.collections = {collection_of("9", mu::SearchKind::album, "旅途中的歌"),
                          collection_of("9", mu::SearchKind::album, "重复")};
    result.has_more = true;
    CHECK(!workspace.search_finished(*request, result));
    CHECK_EQ(workspace.collections().size(), std::size_t{1}, "deduplicated");
    CHECK(workspace.has_more());

    const auto more = workspace.search(true);
    CHECK(more && more->more && more->page == 2 && more->text == "远行");
    result.collections = {collection_of("10", mu::SearchKind::album, "第二页")};
    result.has_more = false;
    workspace.search_finished(*more, result);
    CHECK_EQ(workspace.collections().size(), std::size_t{2}, "appended page");
    CHECK(!workspace.has_more());

    workspace.set_query("没有");
    const auto empty = workspace.search();
    CHECK(workspace.collections().empty());
    workspace.search_finished(*empty, {});
    CHECK_EQ(workspace.notice(), std::string("没有找到结果。试试歌曲名加歌手，或切换音乐来源。"), "nothing found");
}

TEST(stale_and_cancelled_results_are_ignored) {
    mu::LyricWorkspace workspace;
    workspace.set_query("a");
    const auto first = *workspace.search();
    workspace.set_query("b");
    const auto second = *workspace.search();
    mu::SearchResult result;
    result.songs = {song_of("1", "旧")};
    workspace.search_finished(first, result);
    CHECK(workspace.songs().empty());
    CHECK(workspace.searching());
    workspace.cancel_search();
    CHECK(!workspace.searching());
    workspace.search_finished(second, result);
    workspace.search_failed(second, "网络连接失败");
    CHECK(workspace.songs().empty());
    CHECK(workspace.notice().empty());

    const auto third = *workspace.search();
    workspace.search_failed(third, "网络连接失败，请检查网络或代理设置后重试。");
    CHECK(!workspace.searching());
    CHECK_EQ(workspace.notice(), std::string("网络连接失败，请检查网络或代理设置后重试。"), "error shown");
}

TEST(links_and_collections_resolve_with_titles) {
    mu::LyricWorkspace workspace;
    workspace.set_query("https://y.qq.com/n/ryqq/playlist/3235982757");
    const auto request = workspace.search();
    CHECK(request && request->link && request->link->kind == mu::SearchKind::playlist);
    CHECK(workspace.provider() == mu::MusicProvider::qq);
    CHECK(workspace.kind() == mu::SearchKind::playlist);
    CHECK_EQ(request->collection_title, std::string("QQ 音乐歌单"), "default title");
    mu::SearchResult result;
    result.songs = {song_of("1", "一"), song_of("2", "二"), song_of("1", "一")};
    const auto lyrics = workspace.search_finished(*request, result);
    CHECK_EQ(workspace.results_title(), std::string("QQ 音乐歌单"), "playlist header");
    CHECK_EQ(workspace.songs().size(), std::size_t{2}, "deduplicated songs");
    CHECK(lyrics && lyrics->song.song_id == "1");
    CHECK(workspace.loading_lyrics());
    CHECK(workspace.is_selected(workspace.songs().front().id()));

    const auto album = workspace.open(collection_of("18905", mu::SearchKind::album, "叶惠美"));
    CHECK(workspace.provider() == mu::MusicProvider::netease);
    CHECK_EQ(workspace.query(), std::string("https://music.163.com/album?id=18905"), "album link");
    CHECK(album && album->link && album->link->identifier == "18905");
    CHECK_EQ(workspace.results_title(), std::string("叶惠美"), "title while loading");
    CHECK(!workspace.selected());
    workspace.search_finished(*album, {});
    CHECK_EQ(workspace.results_title(), std::string("叶惠美"), "title kept");
    CHECK(!workspace.has_more());

    auto qq = collection_of("000MkMni19ClKG", mu::SearchKind::album, "专辑");
    qq.provider = mu::MusicProvider::qq;
    const auto qq_album = workspace.open(qq);
    CHECK_EQ(workspace.query(), std::string("https://y.qq.com/n/ryqq/albumDetail/000MkMni19ClKG"), "qq album link");
    CHECK(qq_album && qq_album->link);

    const auto audio = workspace.search_for_audio("远行的旋律");
    CHECK(audio && audio->text == "远行的旋律" && audio->kind == mu::SearchKind::song && !audio->link);
    CHECK_EQ(workspace.results_title(), std::string("搜索结果"), "plain search title");
}

TEST(selection_caches_edits_and_reports_missing_lyrics) {
    mu::LyricWorkspace workspace;
    workspace.set_query("x");
    const auto request = *workspace.search();
    mu::SearchResult result;
    result.songs = {song_of("1", "一"), song_of("2", "纯音乐")};
    const auto first = *workspace.search_finished(request, result);
    const auto second = workspace.select(result.songs[1].id());
    CHECK(second);
    workspace.lyrics_loaded(first, {"[00:01.00]旧", "", "", ""});  // replaced by the second selection
    CHECK(workspace.document().original.empty());
    workspace.lyrics_loaded(*second, {});
    CHECK_EQ(workspace.notice(), std::string("平台未提供“纯音乐”的歌词，可能是纯音乐。可导入本地 LRC 或手动编辑。"), "instrumental");

    const auto again = *workspace.select(result.songs[0].id());
    CHECK(!workspace.can_retry());
    workspace.lyrics_loaded(again, {"[00:01.00]一", "[00:01.00]one", "", "[00:01.00]<00:01.00>一"});
    CHECK_EQ(workspace.line_count(), std::size_t{1}, "lines");
    workspace.edit(mu::LyricTab::translation, "[00:01.00]uno");
    CHECK_EQ(workspace.document().word_timed, std::string("[00:01.00]<00:01.00>一"), "translation edit keeps word timing");
    workspace.edit(mu::LyricTab::original, "[00:01.00]一\n[00:02.00]二");
    CHECK(workspace.document().word_timed.empty());
    CHECK_EQ(workspace.text(mu::LyricTab::original), std::string("[00:01.00]一\n[00:02.00]二"), "original edit");

    CHECK(!workspace.select(result.songs[1].id()));  // cached
    CHECK(!workspace.loading_lyrics());
    CHECK(!workspace.select(result.songs[0].id()));
    CHECK_EQ(workspace.document().translation, std::string("[00:01.00]uno"), "edits survive switching songs");
    CHECK(workspace.can_retry());
    const auto retry = workspace.retry_lyrics();
    CHECK(retry && workspace.loading_lyrics());
    CHECK(!workspace.preview_error());
    workspace.lyrics_failed(*retry, "网络连接失败");
    CHECK(!workspace.loading_lyrics());
    CHECK_EQ(workspace.notice(), std::string("网络连接失败"), "lyric error");
}

TEST(checking_is_separate_from_previewing) {
    mu::LyricWorkspace workspace;
    workspace.set_query("x");
    const auto request = *workspace.search();
    mu::SearchResult result;
    result.songs = {song_of("1", "一"), song_of("2", "二")};
    workspace.search_finished(request, result);
    CHECK_EQ(workspace.checked_count(), std::size_t{0}, "nothing checked");
    workspace.toggle(result.songs[1].id());
    CHECK(workspace.is_checked(result.songs[1].id()));
    CHECK(workspace.is_selected(result.songs[0].id()));
    CHECK_EQ(workspace.results_subtitle(), std::string("2 首歌曲，已选 1 首"), "subtitle");
    CHECK(!workspace.all_checked());
    workspace.select_all();
    CHECK(workspace.all_checked());
    CHECK_EQ(workspace.export_checked_label(), std::string("导出所选 2 首"), "batch label");
    workspace.select_all();
    CHECK_EQ(workspace.checked_count(), std::size_t{0}, "select all toggles off");
    workspace.toggle(result.songs[0].id());
    workspace.set_query("新的搜索");
    workspace.search();
    CHECK_EQ(workspace.checked_count(), std::size_t{0}, "new search clears the batch");
}

TEST(local_lyrics_open_as_editable_songs) {
    const fs::path directory = mu::utf8_to_path(MU_TEST_SCRATCH) / "lyric-local";
    fs::remove_all(directory);
    fs::create_directories(directory);
    const auto file = directory / mu::utf8_to_path("本地.lrc");
    mu::detail::write_new_file(file, mu::Bytes{0xff, 0xfe, '[', 0, '0', 0, '0', 0, ':', 0, '0', 0, '1', 0, ']', 0, 0x2d, 0x4e});
    CHECK_EQ(mu::read_lyric_file(file), std::string("[00:01]中"), "utf16 file");
    CHECK_THROWS_AS(mu::read_lyric_file(directory / "missing.lrc"), mu::LyricError, "missing file");
    fs::resize_file(file, 5'000'001);
    CHECK_THROWS_AS(mu::read_lyric_file(file), mu::LyricError, "too large");

    mu::LyricWorkspace workspace;
    CHECK(workspace.import_local("本地", "[00:01]中"));
    CHECK_EQ(workspace.notice(), std::string("已打开本地歌词，可编辑并导出为其他格式。"), "notice");
    CHECK(workspace.selected() && workspace.selected()->artist == "本地歌词");
    CHECK(!workspace.can_retry());
    CHECK(workspace.import_local("第二首", "[00:02]文"));
    CHECK_EQ(workspace.songs().front().title, std::string("第二首"), "newest first");
    CHECK(workspace.songs()[0].id() != workspace.songs()[1].id());
    CHECK(!workspace.select(workspace.songs()[1].id()));
    CHECK_EQ(workspace.document().original, std::string("[00:01]中"), "cached local text");
    fs::remove_all(directory);
}

TEST(preview_follows_options_and_copy) {
    mu::LyricWorkspace workspace;
    CHECK(workspace.preview().empty());
    CHECK(!workspace.preview_error());
    workspace.import_local("歌", "[00:01.000]一\n[00:02.500]二");
    CHECK_EQ(workspace.preview(), std::string("[00:01.000]一\n[00:02.500]二\n"), "lrc preview");
    auto options = workspace.options();
    options.format = mu::LyricFormat::srt;
    workspace.set_options(options);
    CHECK(workspace.text(mu::LyricTab::preview).starts_with("1\n00:00:01,000 --> 00:00:02,500\n一"));
    CHECK_EQ(workspace.copy_preview(), workspace.preview(), "copied text");
    CHECK_EQ(workspace.notice(), std::string("已复制SRT歌词。"), "copy notice");
    options.include_original = options.include_translation = false;
    workspace.set_options(options);
    CHECK_EQ(workspace.preview_error().value_or(""), std::string("请至少选择一种歌词内容。"), "invalid options");
    CHECK(!workspace.can_export_current());
    CHECK(workspace.copy_preview().empty());
}

TEST(batch_export_counts_skips_failures_and_stops) {
    const fs::path directory = mu::utf8_to_path(MU_TEST_SCRATCH) / "lyric-batch";
    fs::remove_all(directory);
    LyricServer server;
    const auto api = server.api();
    mu::LyricWorkspace workspace;
    workspace.set_output_directory(directory);
    workspace.set_query("x");
    const auto request = *workspace.search();
    mu::SearchResult result;
    result.songs = {song_of("1", "一"), song_of("2", "无词"), song_of("3", "坏")};
    const auto loading = workspace.search_finished(request, result);
    CHECK(!workspace.export_checked());  // nothing checked
    workspace.select_all();
    workspace.edit(mu::LyricTab::original, "ignored while loading");

    const auto batch = workspace.export_checked();
    CHECK(batch && batch->jobs.size() == 3);
    CHECK(fs::is_directory(directory));
    CHECK(workspace.exporting());
    CHECK(!workspace.loading_lyrics());  // the in-flight preview fetch is dropped
    workspace.lyrics_loaded(*loading, {"[00:01.00]迟到", "", "", ""});
    CHECK(workspace.document().original.empty());
    CHECK(!workspace.export_current());
    CHECK(!workspace.search());
    CHECK(!workspace.select(result.songs[1].id()));
    CHECK_EQ(batch->jobs[2].index, 3, "index");

    const auto first = mu::export_song(api, batch->jobs[0], batch->options, batch->directory, "");
    CHECK(first.file && first.file->parent_path() == directory);
    CHECK_EQ(mu::path_to_utf8(first.file->filename()), std::string("一 - 音匣示例.lrc"), "file name");
    workspace.export_succeeded(batch->jobs[0], first);
    const int fetched = *server.requests;
    auto empty_job = batch->jobs[1];
    empty_job.document = mu::LyricDocument{};
    const auto skipped = mu::export_song(api, empty_job, batch->options, batch->directory, "");
    CHECK(!skipped.file);
    CHECK_EQ(*server.requests, fetched, "cached documents are not fetched");
    workspace.export_succeeded(empty_job, skipped);
    workspace.export_failed(batch->jobs[2], "网络连接失败");
    CHECK_EQ(workspace.progress_text(), std::string("正在导出 3 / 3"), "progress");
    CHECK_EQ(workspace.failures_title(), std::string("1 首导出失败，展开查看"), "failure title");
    CHECK_EQ(workspace.failures().front(), std::string("坏：网络连接失败"), "failure reason");
    workspace.end_export();
    CHECK(!workspace.exporting());
    CHECK_EQ(workspace.notice(), std::string("导出完成：成功 1 首，跳过 1 首，失败 1 首。可关闭“跳过无歌词歌曲”后重试。"), "summary");

    // Exporting the same song again keeps the earlier file.
    CHECK(!workspace.select(result.songs[0].id()));
    const auto again = workspace.export_current();
    CHECK(again && again->jobs.size() == 1 && again->jobs[0].document);
    const auto second = mu::export_song(api, again->jobs[0], again->options, again->directory, "");
    CHECK_EQ(mu::path_to_utf8(second.file->filename()), std::string("一 - 音匣示例 (1).lrc"), "no overwrite");
    CHECK_EQ(*server.requests, fetched, "edited document exported without fetching");
    workspace.request_stop();
    CHECK(workspace.stop_requested());
    workspace.end_export();
    CHECK_EQ(workspace.notice(), std::string("已停止：成功 0 首，跳过 0 首，失败 0 首。"), "stopped summary");

    mu::CancelToken cancel;
    cancel.cancel();
    auto fetch = batch->jobs[2];
    fetch.document.reset();
    CHECK_THROWS_AS(mu::export_song(api, fetch, batch->options, batch->directory, "", cancel), mu::Cancelled, "cancel");

    workspace.set_output_directory(directory / mu::utf8_to_path("一 - 音匣示例.lrc") / "sub");
    CHECK(!workspace.export_current());
    CHECK(workspace.notice().starts_with("无法创建歌词输出目录："));
    fs::remove_all(directory);
}

TEST(run_search_resolves_links_or_searches) {
    std::vector<std::string> urls;
    const mu::MusicApi api([&urls](const mu::HttpRequest& request) {
        urls.push_back(request.url);
        if (request.url.find("/api/search/get/web") != std::string::npos) {
            return mu::HttpResponse{200, R"({"code":200,"result":{"songCount":1,"songs":[{"id":1,"name":"晴天"}]}})", {}};
        }
        return mu::HttpResponse{200, R"({"code":200,"songs":[{"id":186016,"name":"晴天","ar":[],"al":{},"dt":1000}]})", {}};
    });
    mu::LyricWorkspace workspace;
    workspace.set_query("晴天");
    const auto search = mu::run_search(api, *workspace.search(), "");
    CHECK(search.songs.size() == 1 && !search.has_more);
    workspace.set_query("https://music.163.com/song?id=186016");
    const auto resolved = mu::run_search(api, *workspace.search(), "");
    CHECK(resolved.songs.size() == 1 && resolved.songs[0].song_id == "186016");
    CHECK_EQ(urls.size(), std::size_t{2}, "one request each");
}

}  // namespace
