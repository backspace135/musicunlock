// Lyric, session and platform API tests mirroring apps/macos/Tests/MusicUnlockCoreTests/LyricsTests.swift.
// Requests run against a scripted transport; set MUSICUNLOCK_LIVE_TESTS=1 on Windows to hit the real platforms.
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "detail.h"
#include "musicunlock/lyrics.h"
#include "musicunlock/music_api.h"
#include "musicunlock/session.h"
#include "musicunlock/text.h"
#include "test_support.h"

namespace fs = std::filesystem;
using mu::Bytes;
using mu::LyricDocument;
using mu::LyricError;
using mu::LyricLine;
using mu::LyricOptions;

namespace {

bool contains(std::string_view text, std::string_view part) { return text.find(part) != std::string_view::npos; }

/// Answers requests from a table of URL fragments and records what was sent.
struct ScriptedTransport {
    std::vector<std::pair<std::string, mu::HttpResponse>> routes;
    std::shared_ptr<std::vector<mu::HttpRequest>> sent = std::make_shared<std::vector<mu::HttpRequest>>();

    void on(std::string fragment, std::string body, std::vector<std::string> cookies = {}, int status = 200) {
        routes.emplace_back(std::move(fragment), mu::HttpResponse{status, std::move(body), std::move(cookies)});
    }

    mu::HttpTransport transport() const {
        return [routes = routes, sent = sent](const mu::HttpRequest& request) {
            sent->push_back(request);
            for (const auto& [fragment, response] : routes) {
                if (contains(request.url, fragment)) return response;
            }
            throw std::runtime_error("offline");
        };
    }
};

std::string header(const mu::HttpRequest& request, std::string_view name) {
    for (const auto& [key, value] : request.headers) {
        if (key == name) return value;
    }
    return "";
}

mu::LyricSong song_ref(mu::MusicProvider provider, std::string id, std::string title = "", std::string artist = "") {
    mu::LyricSong song;
    song.provider = provider;
    song.song_id = std::move(id);
    song.title = std::move(title);
    song.artist = std::move(artist);
    return song;
}

mu::Cookie cookie(std::string name, std::string value, std::string domain) {
    mu::Cookie cookie;
    cookie.name = std::move(name);
    cookie.value = std::move(value);
    cookie.domain = std::move(domain);
    return cookie;
}

mu::MusicApi api_for(const ScriptedTransport& script) {
    return mu::MusicApi(script.transport(), [] { return std::int64_t{1700000000123}; });
}

TEST(repeated_timestamps_offset_and_enhanced_lrc) {
    const auto lines = mu::parse_lyrics("[ti:测试]\n[offset:-50]\n[00:01.20][00:03.000]重唱\n[00:02.000]\n[00:04.000]一[00:04.500]二");
    CHECK_EQ(lines, (std::vector<LyricLine>{{1150, "重唱"}, {1950, ""}, {2950, "重唱"}, {3950, "一二"}}), "parsed lines");
    CHECK_EQ(mu::enhanced_lrc_from_yrc("{\"t\":0}\n[1000,900](1000,400,0)一(1400,500,0)二"),
             std::string("[00:01.000]一[00:01.400]二"), "yrc");
    CHECK_EQ(mu::parse_lyrics("纯文字\r[00:05]　结尾　\r\n[al:专辑]\n"),
             (std::vector<LyricLine>{{5000, "结尾"}, {std::nullopt, "纯文字"}}), "CR line breaks, Unicode trimming, tags");
}

TEST(bilingual_srt_merges_cues_and_respects_blank_boundary) {
    const LyricDocument lyrics{"[00:01.000]甲\n[00:02.000]\n[00:03.000]乙", "[00:01.020]A\n[00:03.000]B", "", ""};
    LyricOptions options;
    options.format = mu::LyricFormat::srt;
    options.offset = 100;
    CHECK_EQ(mu::render_lyrics(lyrics, options, 6000),
             std::string("1\n00:00:01,100 --> 00:00:02,100\n甲\nA\n\n2\n00:00:03,100 --> 00:00:06,100\n乙\nB\n"), "srt");
    options.format = mu::LyricFormat::vtt;
    CHECK(mu::render_lyrics(lyrics, options).starts_with("WEBVTT\n\n00:00:01.100 --> 00:00:02.100"));
}

TEST(lrc_layouts_and_precision) {
    const LyricDocument lyrics{"[00:01.000]甲\n[00:03.000]乙", "[00:01.000]A\n[00:03.000]B", "", ""};
    LyricOptions options;
    CHECK_EQ(mu::render_lyrics(lyrics, options), std::string("[00:01.000]甲\n[00:01.000]A\n[00:03.000]乙\n[00:03.000]B\n"),
             "stagger");
    options.layout = mu::LyricLayout::merge;
    options.precision = 2;
    CHECK_EQ(mu::render_lyrics(lyrics, options), std::string("[00:01.00]甲 / A\n[00:03.00]乙 / B\n"), "merge");
    options.layout = mu::LyricLayout::separate;
    options.precision = 0;
    CHECK_EQ(mu::render_lyrics(lyrics, options), std::string("[00:01]甲\n[00:03]乙\n[00:01]A\n[00:03]B\n"), "separate");
    options.format = mu::LyricFormat::txt;
    CHECK_EQ(mu::render_lyrics(lyrics, options), std::string("甲\n乙\nA\nB\n"), "txt");
    CHECK_EQ(mu::lyric_timestamp(3'723'004, mu::LyricFormat::srt), std::string("01:02:03,004"), "srt hours");
    CHECK_EQ(mu::lyric_timestamp(-5, mu::LyricFormat::lrc, 1), std::string("[00:00.0]"), "negative clamps");
}

TEST(untimed_subtitle_and_invalid_selection_are_actionable_errors) {
    LyricOptions options;
    options.format = mu::LyricFormat::srt;
    CHECK_THROWS_AS(mu::render_lyrics({"纯文字歌词", "", "", ""}, options), LyricError, "untimed srt");
    options.format = mu::LyricFormat::txt;
    options.include_original = false;
    options.include_translation = false;
    CHECK_THROWS_AS(mu::render_lyrics({"[00:01]歌词", "", "", ""}, options), LyricError, "nothing selected");
    options.include_original = true;
    options.offset = std::numeric_limits<std::int64_t>::max();
    CHECK_THROWS_AS(mu::render_lyrics({"[00:01]歌词", "", "", ""}, options), LyricError, "offset range");
    options.offset = std::numeric_limits<std::int64_t>::min();
    CHECK_THROWS_AS(mu::render_lyrics({"[00:01]歌词", "", "", ""}, options), LyricError, "offset min");
    CHECK((LyricDocument{"[00:01]\n[00:02]", "", "", ""}.empty()));
    CHECK(!(LyricDocument{"[00:01]有字", "", "", ""}.empty()));
}

TEST(word_timing_preserves_word_stamps_and_rejects_unavailable_content) {
    LyricOptions options;
    options.include_translation = false;
    options.word_timing = true;
    options.offset = -100;
    const LyricDocument document{"[00:01.000]一二", "", "", "[00:01.000]一[00:01.400]二"};
    CHECK_EQ(mu::render_lyrics(document, options), std::string("[00:00.900]一[00:01.300]二\n"), "shifted words");
    CHECK_THROWS_AS(mu::render_lyrics({document.original, "", "", ""}, options), LyricError, "no word timing");
    options.include_translation = true;
    CHECK_THROWS_AS(mu::render_lyrics(document, options), LyricError, "translation with word timing");
}

TEST(pinyin_uses_system_transliteration) {
    const auto latin = mu::mandarin_to_latin("我们在重庆银行唱歌 Hello 了");
    CHECK(latin.has_value());
    CHECK_EQ(latin.value_or(""), std::string("wǒ men zài chóng qìng yín xíng chàng gē Hello le"), "pinyin");
    LyricOptions options;
    options.include_translation = false;
    options.include_pinyin = true;
    CHECK_EQ(mu::render_lyrics({"[00:01.000]你好", "", "", ""}, options), std::string("[00:01.000]你好\n[00:01.000]nǐ hǎo\n"),
             "pinyin track");
}

TEST(local_export_never_overwrites_and_preserves_encoding) {
    const fs::path directory = mu::utf8_to_path(MU_TEST_SCRATCH) / "lyric-export";
    fs::remove_all(directory);
    fs::create_directories(directory);
    const mu::LyricSong song = song_ref(mu::MusicProvider::netease, "1", "../../歌曲", "歌手");
    LyricOptions options;
    options.encoding = mu::LyricEncoding::utf8_bom;
    const auto first = mu::save_lyrics("原始", song, options, 1, directory);
    const auto second = mu::save_lyrics("修改", song, options, 1, directory);
    CHECK(first != second);
    CHECK(first.parent_path() == directory);
    CHECK_EQ(mu::path_to_utf8(first.filename()), std::string("_.._歌曲 - 歌手.lrc"), "sanitized name");
    CHECK_EQ(mu::path_to_utf8(second.filename()), std::string("_.._歌曲 - 歌手 (1).lrc"), "collision suffix");
    CHECK_EQ(mu::detail::read_file(first), (Bytes{0xef, 0xbb, 0xbf, 0xe5, 0x8e, 0x9f, 0xe5, 0xa7, 0x8b}), "bom + utf8");
    CHECK_EQ(mu::encode_text(mu::LyricEncoding::utf16le, "中文"), (Bytes{0xff, 0xfe, 0x2d, 0x4e, 0x87, 0x65}), "utf16");
    CHECK_EQ(mu::lyric_filename(song, "${index}. ${name} [${album}] ${id}", 7), std::string("7. .._.._歌曲 [] 1"), "template");
    CHECK_EQ(mu::lyric_filename(song, "CON", 1), std::string("_CON"), "device name");
    CHECK_EQ(mu::lyric_filename(song, " ... ", 1), std::string("歌词"), "fallback");
    fs::remove_all(directory);
}

TEST(local_lyric_files_decode_common_encodings) {
    CHECK_EQ(mu::decode_lyric_file(Bytes{0xef, 0xbb, 0xbf, 'a'}), std::string("a"), "utf8 bom");
    CHECK_EQ(mu::decode_lyric_file(Bytes{0xff, 0xfe, 0x2d, 0x4e}), std::string("中"), "utf16 le");
    CHECK_EQ(mu::decode_lyric_file(Bytes{0xfe, 0xff, 0x4e, 0x2d}), std::string("中"), "utf16 be");
    CHECK_EQ(mu::decode_lyric_file(Bytes{'[', '0', '0', ':', '0', '1', ']', 0xd6, 0xd0, 0xce, 0xc4}),
             std::string("[00:01]中文"), "gbk");
    CHECK_THROWS_AS(mu::decode_lyric_file(Bytes{0xff, 0xfe, 0x2d}), LyricError, "odd utf16");
    CHECK_THROWS_AS(mu::decode_lyric_file(Bytes(5'000'001, 'a')), LyricError, "too large");
}

TEST(options_round_trip_and_fall_back_to_defaults) {
    LyricOptions options;
    options.format = mu::LyricFormat::vtt;
    options.layout = mu::LyricLayout::separate;
    options.encoding = mu::LyricEncoding::utf16le;
    options.include_pinyin = true;
    options.precision = 1;
    options.offset = -250;
    options.separator = " | ";
    options.filename = "${id}";
    CHECK(mu::options_from_json(mu::options_to_json(options)) == options);
    CHECK(mu::options_from_json("not json") == LyricOptions{});
    const auto clamped = mu::options_from_json(R"({"format":"mp3","precision":9,"offset":999999999999,"filename":""})");
    CHECK(clamped.format == mu::LyricFormat::lrc);
    CHECK_EQ(clamped.precision, 3, "precision clamp");
    CHECK_EQ(clamped.offset, std::int64_t{86'400'000}, "offset clamp");
    CHECK_EQ(clamped.filename, LyricOptions{}.filename, "empty filename");
}

TEST(music_links_select_correct_provider_and_kind) {
    using mu::MusicLink;
    using mu::MusicProvider;
    using mu::SearchKind;
    const auto netease = MusicLink::parse("分享歌曲 https://music.163.com/#/song?id=186016&userid=1", MusicProvider::qq, SearchKind::playlist);
    CHECK(netease == (MusicLink{MusicProvider::netease, SearchKind::song, "186016"}));
    const auto qq = MusicLink::parse("https://y.qq.com/n/ryqq/albumDetail/000MkMni19ClKG", MusicProvider::netease, SearchKind::song);
    CHECK(qq == (MusicLink{MusicProvider::qq, SearchKind::album, "000MkMni19ClKG"}));
    const auto playlist = MusicLink::parse("https://y.qq.com/n/ryqq/playlist/3235982757，快来听", MusicProvider::netease, SearchKind::song);
    CHECK(playlist == (MusicLink{MusicProvider::qq, SearchKind::playlist, "3235982757"}));
    CHECK(MusicLink::parse(" 186016 ", MusicProvider::qq, SearchKind::album) == (MusicLink{MusicProvider::qq, SearchKind::album, "186016"}));
    CHECK(!MusicLink::parse("https://music.163.com.example.com/song?id=1", MusicProvider::netease, SearchKind::song));
    CHECK(!MusicLink::parse("晴天 周杰伦", MusicProvider::netease, SearchKind::song));
    CHECK(mu::provider_owns(MusicProvider::netease, ".music.163.com"));
    CHECK(!mu::provider_owns(MusicProvider::qq, "notqq.com"));
}

TEST(set_cookie_parsing_and_session_rules) {
    const std::int64_t now = 1'800'000'000;
    const auto cookies = mu::parse_set_cookies(
        {"MUSIC_U=abc; Max-Age=3600; Domain=music.163.com; Path=/; HttpOnly",
         "__csrf=tok; Expires=Wed, 21 Oct 2015 07:28:00 GMT; Path=/",
         "NMTID=x; expires=Thu, 01-Jan-2099 00:00:00 GMT; Secure", "broken"},
        "music.163.com", now);
    CHECK_EQ(cookies.size(), std::size_t{3}, "cookie count");
    CHECK_EQ(cookies[0].domain, std::string(".music.163.com"), "domain attribute");
    CHECK_EQ(cookies[0].expires, std::optional<std::int64_t>(now + 3600), "max-age");
    CHECK_EQ(cookies[1].expires, std::optional<std::int64_t>(1445412480), "rfc 1123 date");
    CHECK(cookies[1].expired(now));
    CHECK_EQ(cookies[2].expires, std::optional<std::int64_t>(4070908800), "dashed date");
    CHECK(cookies[2].secure);
    CHECK_EQ(mu::cookie_header(cookies, now), std::string("MUSIC_U=abc; NMTID=x"), "header skips expired");
    CHECK(mu::has_music_session(cookies, mu::MusicProvider::netease, now));
    CHECK(!mu::has_music_session(cookies, mu::MusicProvider::netease, now + 7200));

    const std::vector<mu::Cookie> qq = {cookie("qqmusic_key", "k", ".y.qq.com"), cookie("uin", "1", ".qq.com"),
                                         cookie("other", "1", ".example.com")};
    const auto accepted = mu::accept_session(qq, mu::MusicProvider::qq, now);
    CHECK(accepted && accepted->size() == 2);
    CHECK(!mu::accept_session({qq[0]}, mu::MusicProvider::qq, now));
    CHECK(mu::cookies_from_json(mu::cookies_to_json(cookies)) == cookies);
    CHECK(mu::cookies_from_json("[{\"name\":\"\"}, 5]").empty());
}

TEST(entities_and_qq_token) {
    CHECK_EQ(mu::MusicApi::decode_entities("&amp;#65;&#x4e2d;&#25991;&lt;&quot;&#xD800;&#99999999999;"),
             std::string("A中文<\"&#xD800;&#99999999999;"), "entities");
    CHECK_EQ(mu::MusicApi::decode_entities("&#x1F3B5;"), std::string("\xF0\x9F\x8E\xB5"), "astral");
    CHECK_EQ(mu::MusicApi::qq_token("uin=1; qm_keyst=abc中; qm_keyst=zzz"), std::uint32_t{2090089496}, "g_tk");
    CHECK_EQ(mu::MusicApi::qq_token(""), std::uint32_t{5381}, "empty g_tk");
}

TEST(netease_requests_and_parsing) {
    ScriptedTransport script;
    script.on("/api/search/get/web",
              R"({"code":200,"result":{"songCount":31,"songs":[{"id":186016,"name":"晴天","artists":[{"name":"周杰伦"},{"name":"B"}],
                  "album":{"name":"叶惠美","picUrl":"https://p.example/1.jpg"},"duration":269000},{"name":"无 ID"}]}})");
    const auto api = api_for(script);
    const auto page = api.search("晴天 & 雨", mu::MusicProvider::netease, mu::SearchKind::song, 1, "MUSIC_U=1");
    CHECK_EQ(script.sent->size(), std::size_t{1}, "one request");
    const auto& request = script.sent->front();
    CHECK_EQ(request.url, std::string("https://music.163.com/api/search/get/web?_t=1700000000123&limit=30&offset=0&s=%E6%99%B4%E5%A4%A9%20%26%20%E9%9B%A8&type=1"),
             "sorted, encoded query");
    CHECK_EQ(header(request, "Cookie"), std::string("MUSIC_U=1"), "cookie header");
    CHECK_EQ(header(request, "Referer"), std::string("https://music.163.com/"), "referer");
    CHECK_EQ(page.songs.size(), std::size_t{1}, "songs without id are skipped");
    CHECK(page.has_more);
    CHECK(page.songs.at(0) == (mu::LyricSong{mu::MusicProvider::netease, "186016", "晴天", "周杰伦 / B", "叶惠美", 269000,
                                             "186016", "https://p.example/1.jpg"}));

    ScriptedTransport login;
    login.on("/api/song/lyric?", R"({"code":301})");
    CHECK_THROWS_AS(api_for(login).lyrics(song_ref(mu::MusicProvider::netease, "1"), ""), LyricError, "login required");
    ScriptedTransport failing;
    failing.on("/api/", R"({"code":400,"message":"参数错误"})");
    try {
        api_for(failing).search("x", mu::MusicProvider::netease, mu::SearchKind::album, 1, "");
        CHECK(false);
    } catch (const LyricError& error) {
        CHECK_EQ(std::string(error.what()), std::string("参数错误"), "server message");
    }
    CHECK_THROWS_AS(api_for(ScriptedTransport{}).search("x", mu::MusicProvider::netease, mu::SearchKind::song, 1, ""),
                    LyricError, "offline maps to LyricError");
}

TEST(netease_playlist_chunks_and_optional_word_timing) {
    ScriptedTransport script;
    std::string ids;
    for (int i = 1; i <= 301; ++i) ids += (i > 1 ? "," : "") + std::string("{\"id\":") + std::to_string(i) + "}";
    script.on("/api/v6/playlist/detail", R"({"code":200,"playlist":{"trackIds":[)" + ids + "]}}");
    script.on("/api/v3/song/detail", R"({"code":200,"songs":[{"id":1,"name":"一","ar":[{"name":"甲"}],"al":{"name":"专"},"dt":1000}]})");
    script.on("/api/song/lyric?", R"({"code":200,"lrc":{"lyric":"[00:01.00]一"},"tlyric":{"lyric":"[00:01.00]one"}})");
    script.on("/api/song/lyric/v1", "", {}, 500);
    const auto api = api_for(script);
    const auto songs = api.resolve({mu::MusicProvider::netease, mu::SearchKind::playlist, "9"}, "");
    CHECK_EQ(songs.size(), std::size_t{2}, "two detail chunks");
    CHECK_EQ(script.sent->size(), std::size_t{3}, "playlist + 2 detail requests");
    CHECK(contains(script.sent->at(2).url, "c=%5B%7B%22id%22%3A%22301%22%7D%5D"));
    const auto document = api.lyrics(songs.front(), "");
    CHECK_EQ(document, (LyricDocument{"[00:01.00]一", "[00:01.00]one", "", ""}), "yrc failure is ignored");

    mu::CancelToken cancel;
    cancel.cancel();
    CHECK_THROWS_AS(api.search("x", mu::MusicProvider::netease, mu::SearchKind::song, 1, "", cancel), mu::Cancelled, "cancel");
}

TEST(qq_requests_and_parsing) {
    ScriptedTransport script;
    // Callback-wrapped JSON with a description truncated mid-codepoint.
    script.on("client_search_cp", "callback({\"code\":0,\"data\":{\"song\":{\"totalnum\":30,\"list\":[{\"songmid\":\"003\","
                                  "\"songname\":\"A&amp;B\",\"singer\":[{\"name\":\"&#21608;\"}],\"albummid\":\"0AL\","
                                  "\"albumname\":\"专辑\",\"interval\":200,\"songid\":42,\"desc\":\"\xE4\xB8\"}]}}})");
    script.on("fcg_ucc_getcdinfo", R"({"code":0,"cdlist":[{"total_song_num":3,"songlist":[{"mid":"1","title":"一"}]}]})");
    script.on("fcg_query_lyric_new", R"({"code":0,"lyric":"WzAwOjAxLjAwXeS4gA==","trans":"[00:01.00]&#x6587;"})");
    const auto api = api_for(script);
    const auto page = api.search("x", mu::MusicProvider::qq, mu::SearchKind::song, 1, "uin=1; qm_keyst=abc中");
    CHECK(!page.has_more);
    CHECK(page.songs.size() == 1 &&
          page.songs[0] == (mu::LyricSong{mu::MusicProvider::qq, "003", "A&B", "周", "专辑", 200000, "42",
                                          "https://y.gtimg.cn/music/photo_new/T002R300x300M000" "0AL.jpg"}));
    CHECK(contains(script.sent->front().url, "g_tk=2090089496"));
    CHECK(contains(script.sent->front().url, "format=json"));
    try {
        api.resolve({mu::MusicProvider::qq, mu::SearchKind::playlist, "1"}, "");
        CHECK(false);
    } catch (const LyricError& error) {
        CHECK(contains(error.what(), "1 / 3"));
    }
    const auto document = api.lyrics(song_ref(mu::MusicProvider::qq, "003"), "");
    CHECK_EQ(document.original, std::string("[00:01.00]一"), "base64 lyric");
    CHECK_EQ(document.translation, std::string("[00:01.00]文"), "plain lyric with entities");

    ScriptedTransport refused;
    refused.on("c.y.qq.com", R"({"code":-1})");
    CHECK_THROWS_AS(api_for(refused).search("x", mu::MusicProvider::qq, mu::SearchKind::album, 1, ""), LyricError, "qq code");
    ScriptedTransport garbage;
    garbage.on("c.y.qq.com", "<html>");
    CHECK_THROWS_AS(api_for(garbage).search("x", mu::MusicProvider::qq, mu::SearchKind::album, 1, ""), LyricError, "not json");
}

TEST(qr_login_and_account) {
    ScriptedTransport script;
    script.on("qrcode/unikey", R"({"code":200,"unikey":"KEY"})");
    script.on("qrcode/client/login", R"({"code":803})",
              {"MUSIC_U=secret; Domain=.music.163.com; Max-Age=86400", "evil=1; Domain=.example.com"});
    script.on("account/get", R"({"code":200,"profile":{"nickname":"昵称"}})");
    const auto api = api_for(script);
    const auto ticket = api.create_qr_code();
    CHECK_EQ(ticket.url, std::string("https://music.163.com/login?codekey=KEY"), "qr url");
    const auto status = api.check_qr_code(ticket.key);
    CHECK_EQ(status.code, 803, "confirmed");
    CHECK(status.cookies.size() == 1 && status.cookies[0].name == "MUSIC_U");
    CHECK(mu::accept_session(status.cookies, mu::MusicProvider::netease, mu::unix_now()).has_value());
    CHECK_EQ(api.account_name(mu::MusicProvider::netease, "MUSIC_U=secret"), std::optional<std::string>("昵称"), "nickname");
    CHECK_EQ(api.account_name(mu::MusicProvider::qq, "uin=1"), std::optional<std::string>("QQ 音乐用户"), "qq name");
    CHECK(!api.account_name(mu::MusicProvider::netease, ""));
}

#ifdef _WIN32
TEST(live_first_party_search_resolve_lyrics_and_qr) {
    const char* flag = std::getenv("MUSICUNLOCK_LIVE_TESTS");
    if (!flag || std::string(flag) != "1") {
        std::cout << "  skipped: set MUSICUNLOCK_LIVE_TESTS=1 to check the real platforms\n";
        return;
    }
    const mu::MusicApi api(mu::make_system_transport());
    for (auto provider : mu::all_providers) {
        const std::string title(mu::provider_title(provider));
        for (auto kind : {mu::SearchKind::song, mu::SearchKind::album, mu::SearchKind::playlist}) {
            const auto page = api.search("周杰伦", provider, kind, 1, "");
            CHECK_EQ(kind == mu::SearchKind::song ? page.songs.empty() : page.collections.empty(), false,
                     title + std::string(mu::search_kind_label(kind)));
        }
        const bool netease = provider == mu::MusicProvider::netease;
        const auto songs = api.resolve(*mu::MusicLink::parse(netease ? "186016" : "0039MnYb0qxYhV", provider, mu::SearchKind::song), "");
        CHECK(!songs.empty() && songs.front().title == "晴天");
        if (songs.empty()) continue;
        const auto lyric = api.lyrics(songs.front(), "");
        CHECK(!lyric.empty() && contains(lyric.original, "["));
        CHECK(api.resolve(*mu::MusicLink::parse(netease ? "18905" : "000MkMni19ClKG", provider, mu::SearchKind::album), "").size() > 1);
        CHECK(api.resolve(*mu::MusicLink::parse(netease ? "3778678" : "3235982757", provider, mu::SearchKind::playlist), "").size() > 1);
    }
    const auto code = api.create_qr_code();
    CHECK(code.url.starts_with("https://music.163.com/"));
    CHECK_EQ(api.check_qr_code(code.key).code, 801, "waiting for scan");
}
#endif

}  // namespace
