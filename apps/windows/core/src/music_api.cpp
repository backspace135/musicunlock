#include "musicunlock/music_api.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <map>

#include <nlohmann/json.hpp>

#include "detail.h"
#include "musicunlock/text.h"

namespace mu {

namespace {

using Json = nlohmann::json;
using Params = std::map<std::string, std::string>;

constexpr const char* user_agent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 "
    "Safari/537.36 Edg/130.0.0.0";
constexpr std::size_t max_response = 20'000'000;

const Json null_json;

/// Value for the first key that is present (even as null), like chained `item[a] ?? item[b]` in Swift.
const Json& first(const Json& object, std::initializer_list<const char*> keys) {
    if (!object.is_object()) return null_json;
    for (const char* key : keys) {
        if (auto it = object.find(key); it != object.end()) return *it;
    }
    return null_json;
}

const Json& field(const Json& object, const char* key) { return first(object, {key}); }

/// Dictionary or an empty object, like `as? [String: Any] ?? [:]`.
const Json& object_or_empty(const Json& value) {
    static const Json empty = Json::object();
    return value.is_object() ? value : empty;
}

/// Array of dictionaries; anything else (or a mixed array) is empty, like `as? [[String: Any]] ?? []`.
std::vector<const Json*> objects(const Json& value) {
    std::vector<const Json*> out;
    if (!value.is_array()) return out;
    for (const auto& item : value) {
        if (!item.is_object()) return {};
        out.push_back(&item);
    }
    return out;
}

/// NSNumber.intValue or Int(String), else 0.
std::int64_t number(const Json& value) {
    if (value.is_number_integer()) return value.get<std::int64_t>();
    if (value.is_number_unsigned()) return static_cast<std::int64_t>(value.get<std::uint64_t>());
    if (value.is_number_float()) {
        const double d = value.get<double>();
        return std::isfinite(d) && std::fabs(d) < 9e18 ? static_cast<std::int64_t>(d) : 0;
    }
    if (value.is_boolean()) return value.get<bool>() ? 1 : 0;
    if (value.is_string()) {
        const std::string& text = value.get_ref<const std::string&>();
        std::string_view digits(text);
        const bool negative = !digits.empty() && digits.front() == '-';
        if (!digits.empty() && (digits.front() == '-' || digits.front() == '+')) digits.remove_prefix(1);
        if (digits.empty() || digits.size() > 18 ||
            !std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            return 0;
        }
        const std::int64_t parsed = std::stoll(std::string(digits));
        return negative ? -parsed : parsed;
    }
    return 0;
}

/// String or NSNumber.stringValue, else "".
std::string string(const Json& value) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_number_integer()) return std::to_string(value.get<std::int64_t>());
    if (value.is_number_unsigned()) return std::to_string(value.get<std::uint64_t>());
    if (value.is_boolean()) return value.get<bool>() ? "1" : "0";
    if (value.is_number_float()) {
        const double d = value.get<double>();
        if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 9e15) {
            return std::to_string(static_cast<std::int64_t>(d));
        }
        return value.dump();
    }
    return "";
}

std::string join_names(const Json& list, bool entities) {
    std::string out;
    for (const Json* item : objects(list)) {
        if (!out.empty()) out += " / ";
        const std::string name = string(field(*item, "name"));
        out += entities ? MusicApi::decode_entities(name) : name;
    }
    return out;
}

bool all_digits(std::string_view text) {
    return std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}

/// Query component encoding: everything except RFC 3986 unreserved characters.
std::string url_encode(std::string_view text) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    for (char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
            c == '_' || c == '~') {
            out.push_back(ch);
        } else {
            out += {'%', hex[c >> 4], hex[c & 15]};
        }
    }
    return out;
}

std::string with_query(std::string url, const Params& params) {
    char separator = '?';
    for (const auto& [name, value] : params) {
        url += separator;
        url += url_encode(name) + "=" + url_encode(value);
        separator = '&';
    }
    return url;
}

std::string host_of(std::string_view url) {
    const std::size_t start = url.find("://") + 3;
    return std::string(url.substr(start, url.find('/', start) - start));
}

std::optional<LyricSong> netease_song(const Json& item) {
    const std::string id = string(field(item, "id"));
    if (id.empty()) return std::nullopt;
    const Json& al = field(item, "al");
    const Json& album = al.is_object() ? al : object_or_empty(field(item, "album"));
    return LyricSong{MusicProvider::netease,
                     id,
                     string(field(item, "name")),
                     join_names(first(item, {"ar", "artists"}), false),
                     string(field(album, "name")),
                     number(first(item, {"dt", "duration"})),
                     id,
                     string(field(album, "picUrl"))};
}

std::optional<LyricSong> qq_song(const Json& raw, const std::string& album_name = "") {
    const Json& info = field(raw, "songInfo");
    const Json& music = field(raw, "musicData");
    const Json& item = info.is_object() ? info : music.is_object() ? music : raw;
    const std::string id = string(first(item, {"mid", "songmid"}));
    if (id.empty()) return std::nullopt;
    const Json& album = object_or_empty(field(item, "album"));
    const std::string mid = string(album.contains("mid") ? field(album, "mid") : field(item, "albummid"));
    const Json& album_title = album.contains("name") ? field(album, "name") : field(item, "albumname");
    const bool has_album_title = album.contains("name") || item.contains("albumname");
    return LyricSong{MusicProvider::qq,
                     id,
                     MusicApi::decode_entities(string(first(item, {"title", "songname", "name"}))),
                     join_names(field(item, "singer"), true),
                     MusicApi::decode_entities(has_album_title ? string(album_title) : album_name),
                     number(field(item, "interval")) * 1000,
                     string(first(item, {"id", "songid"})),
                     mid.empty() ? "" : "https://y.gtimg.cn/music/photo_new/T002R300x300M000" + mid + ".jpg"};
}

struct Response {
    Json json;
    std::vector<Cookie> cookies;
};

}  // namespace

MusicApi::MusicApi(HttpTransport transport, std::function<std::int64_t()> clock_ms)
    : transport_(std::move(transport)), clock_ms_(std::move(clock_ms)) {
    if (!clock_ms_) {
        clock_ms_ = [] {
            using namespace std::chrono;
            return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        };
    }
}

std::uint32_t MusicApi::qq_token(std::string_view cookie) {
    std::map<std::string, std::string> values;
    for (std::size_t at = 0; at <= cookie.size();) {
        std::size_t end = cookie.find(';', at);
        if (end == std::string_view::npos) end = cookie.size();
        std::string_view item = cookie.substr(at, end - at);
        while (!item.empty() && (item.front() == ' ' || item.front() == '\t')) item.remove_prefix(1);
        while (!item.empty() && (item.back() == ' ' || item.back() == '\t')) item.remove_suffix(1);
        if (const std::size_t equals = item.find('='); equals != std::string_view::npos) {
            values.emplace(std::string(item.substr(0, equals)), std::string(item.substr(equals + 1)));  // first wins
        }
        at = end + 1;
    }
    std::string key;
    for (const char* name : {"qm_keyst", "qqmusic_key", "p_skey", "skey"}) {
        if (auto it = values.find(name); it != values.end()) {
            key = it->second;
            break;
        }
    }
    std::uint32_t hash = 5381;
    for (char16_t unit : utf8_to_utf16(key)) hash = hash * 33 + unit;
    return hash & 0x7fff'ffff;
}

std::string MusicApi::decode_entities(std::string_view value) {
    std::string text(value);
    text = replace_all(std::move(text), "&amp;", "&");
    text = replace_all(std::move(text), "&quot;", "\"");
    text = replace_all(std::move(text), "&apos;", "'");
    text = replace_all(std::move(text), "&lt;", "<");
    text = replace_all(std::move(text), "&gt;", ">");
    // &#(x[0-9a-fA-F]+|\d+);
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        if (text.compare(i, 2, "&#") == 0) {
            const bool hex = i + 2 < text.size() && text[i + 2] == 'x';
            std::size_t j = i + 2 + (hex ? 1 : 0);
            const std::size_t digits_start = j;
            while (j < text.size() && (hex ? std::isxdigit(static_cast<unsigned char>(text[j])) != 0
                                           : text[j] >= '0' && text[j] <= '9')) {
                ++j;
            }
            if (j > digits_start && j < text.size() && text[j] == ';') {
                const std::string digits = text.substr(digits_start, j - digits_start);
                std::uint64_t code = 0;
                bool overflow = digits.size() > 10;
                for (char c : digits) {
                    if (overflow) break;
                    const int digit = c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
                    code = code * (hex ? 16 : 10) + static_cast<std::uint64_t>(digit);
                    overflow = code > 0xffff'ffff;
                }
                // UnicodeScalar(UInt32) rejects surrogates and values above U+10FFFF.
                if (!overflow && code <= 0x10ffff && (code < 0xd800 || code > 0xdfff)) {
                    out += utf16_to_utf8(code > 0xffff ? std::u16string{static_cast<char16_t>(0xd800 + ((code - 0x10000) >> 10)),
                                                                        static_cast<char16_t>(0xdc00 + ((code - 0x10000) & 0x3ff))}
                                                       : std::u16string(1, static_cast<char16_t>(code)));
                    i = j + 1;
                    continue;
                }
                out.append(text, i, j + 1 - i);
                i = j + 1;
                continue;
            }
        }
        out.push_back(text[i++]);
    }
    return out;
}

namespace {

class Client {
public:
    Client(const HttpTransport& transport, const std::function<std::int64_t()>& clock_ms, CancelToken cancel = {})
        : transport_(transport), clock_ms_(clock_ms), cancel_(std::move(cancel)) {}

    Response request(const std::string& url, MusicProvider provider, const std::string& cookie) const {
        cancel_.check();
        HttpRequest request{url, "GET", {}, {}};
        request.headers = {{"User-Agent", user_agent},
                           {"Referer", std::string(provider_home(provider))},
                           {"Accept", "application/json"}};
        if (!cookie.empty()) request.headers.emplace_back("Cookie", cookie);
        const std::string title(provider_title(provider));
        HttpResponse response;
        try {
            response = transport_(request);
        } catch (const LyricError&) {
            throw;
        } catch (const Cancelled&) {
            throw;
        } catch (const std::exception&) {
            throw LyricError(title + "暂时无法连接，请稍后重试。");
        }
        cancel_.check();
        if (response.status < 200 || response.status >= 300) throw LyricError(title + "暂时无法连接，请稍后重试。");
        if (response.body.size() > max_response) throw LyricError("平台响应过大，请缩小搜索范围。");
        // QQ truncates some playlist descriptions mid-codepoint. Repair those bytes before JSON decoding.
        std::string text = repair_utf8(response.body);
        if (!trim_space(text).starts_with("{")) {
            // Some QQ endpoints wrap JSON in a fixed callback.
            const std::size_t start = text.find('{');
            const std::size_t end = text.rfind('}');
            if (start != std::string::npos && end != std::string::npos && end >= start) {
                text = text.substr(start, end - start + 1);
            }
        }
        Json json = Json::parse(text, nullptr, false);
        if (!json.is_object()) throw LyricError(title + "返回了无法识别的内容，请重试或登录后再试。");
        std::vector<Cookie> cookies;
        for (auto& received : parse_set_cookies(response.set_cookies, host_of(url), unix_now())) {
            if (provider_owns(provider, received.domain)) cookies.push_back(std::move(received));
        }
        return {std::move(json), std::move(cookies)};
    }

    Response netease(const std::string& path, Params params, const std::string& cookie = "") const {
        params["_t"] = std::to_string(clock_ms_());
        Response result = request(with_query("https://music.163.com" + path, params), MusicProvider::netease, cookie);
        const std::int64_t code = number(field(result.json, "code"));
        if (code == 301 || code == 302 || code == 401 || code == 50000005) {
            throw LyricError("这项内容需要登录网易云音乐，或登录已过期。请点击“账号登录”重新授权。");
        }
        if (code != 200 && (code < 800 || code > 803)) {
            for (const char* key : {"message", "msg"}) {
                if (const Json& message = field(result.json, key); message.is_string()) {
                    throw LyricError(message.get<std::string>());
                }
            }
            throw LyricError("网易云音乐请求失败（" + std::to_string(code) + "），请稍后重试。");
        }
        return result;
    }

    Json qq(const std::string& path, Params params, const std::string& cookie) const {
        for (auto [name, value] : {std::pair{"format", std::string("json")}, std::pair{"inCharset", std::string("utf8")},
                                   std::pair{"outCharset", std::string("utf8")},
                                   std::pair{"g_tk", std::to_string(MusicApi::qq_token(cookie))}}) {
            params.emplace(name, value);  // caller's values win
        }
        Json json = request(with_query("https://c.y.qq.com" + path, params), MusicProvider::qq, cookie).json;
        if (const std::int64_t code = number(field(json, "code")); code != 0) {
            throw LyricError("QQ 音乐暂时未能提供内容（" + std::to_string(code) + "）。请登录后重试。");
        }
        return json;
    }

    std::vector<LyricSong> netease_details(const std::vector<std::string>& ids, const std::string& cookie) const {
        Json payload = Json::array();
        for (const auto& id : ids) payload.push_back({{"id", id}});
        const Json json = netease("/api/v3/song/detail", {{"c", payload.dump()}}, cookie).json;
        std::vector<LyricSong> songs;
        for (const Json* item : objects(field(json, "songs"))) {
            if (auto song = netease_song(*item)) songs.push_back(std::move(*song));
        }
        return songs;
    }

private:
    const HttpTransport& transport_;
    const std::function<std::int64_t()>& clock_ms_;
    CancelToken cancel_;
};

template <typename Make>
void append_songs(std::vector<LyricSong>& out, const Json& list, Make make) {
    for (const Json* item : objects(list)) {
        if (auto song = make(*item)) out.push_back(std::move(*song));
    }
}

}  // namespace

SearchPage MusicApi::search(std::string_view query, MusicProvider provider, SearchKind kind, int page,
                            const std::string& cookie, const CancelToken& cancel) const {
    const Client client(transport_, clock_ms_, cancel);
    constexpr int limit = 30;
    SearchPage output;
    if (provider == MusicProvider::netease) {
        const int type = kind == SearchKind::song ? 1 : kind == SearchKind::album ? 10 : 1000;
        const Json json = client.netease("/api/search/get/web",
                                         {{"s", std::string(query)},
                                          {"type", std::to_string(type)},
                                          {"limit", std::to_string(limit)},
                                          {"offset", std::to_string((page - 1) * limit)}},
                                         cookie)
                              .json;
        const Json& result = object_or_empty(field(json, "result"));
        if (kind == SearchKind::song) {
            append_songs(output.songs, field(result, "songs"), netease_song);
        } else {
            const bool album = kind == SearchKind::album;
            for (const Json* item : objects(field(result, album ? "albums" : "playlists"))) {
                const std::string id = string(field(*item, "id"));
                if (id.empty()) continue;
                const Json& owner = object_or_empty(field(*item, album ? "artist" : "creator"));
                output.collections.push_back({MusicProvider::netease, id, kind, string(field(*item, "name")),
                                              string(field(owner, album ? "name" : "nickname"))});
            }
        }
        const char* count = kind == SearchKind::song ? "songCount" : kind == SearchKind::album ? "albumCount" : "playlistCount";
        output.has_more = std::int64_t{page} * limit < number(field(result, count));
        return output;
    }
    const int type = kind == SearchKind::song ? 0 : kind == SearchKind::album ? 8 : 2;
    const bool playlist = kind == SearchKind::playlist;
    const Json json =
        playlist ? client.qq("/soso/fcgi-bin/client_music_search_songlist",
                             {{"query", std::string(query)},
                              {"page_no", std::to_string(page - 1)},
                              {"num_per_page", std::to_string(limit)},
                              {"remoteplace", "txt.yqq.playlist"}},
                             cookie)
                 : client.qq("/soso/fcgi-bin/client_search_cp",
                             {{"w", std::string(query)},
                              {"t", std::to_string(type)},
                              {"p", std::to_string(page)},
                              {"n", std::to_string(limit)},
                              {"cr", "1"}},
                             cookie);
    const Json& data = object_or_empty(field(json, "data"));
    const Json& result = playlist ? data : object_or_empty(field(data, kind == SearchKind::song ? "song" : "album"));
    if (kind == SearchKind::song) {
        append_songs(output.songs, field(result, "list"), [](const Json& item) { return qq_song(item); });
    } else {
        const bool album = kind == SearchKind::album;
        for (const Json* item : objects(field(result, "list"))) {
            const std::string id = string(first(*item, {album ? "albumMID" : "dissid", "albummid", "albumID"}));
            if (id.empty()) continue;
            const Json& creator = object_or_empty(field(*item, "creator"));
            const Json& subtitle = item->contains("singerName") ? field(*item, "singerName") : field(creator, "name");
            output.collections.push_back(
                {MusicProvider::qq, id, kind, decode_entities(string(first(*item, {album ? "albumName" : "dissname", "albumname"}))),
                 decode_entities(string(subtitle))});
        }
    }
    output.has_more = std::int64_t{page} * limit < number(first(result, {"totalnum", "sum", "display_num"}));
    return output;
}

std::vector<LyricSong> MusicApi::resolve(const MusicLink& link, const std::string& cookie, const CancelToken& cancel) const {
    const Client client(transport_, clock_ms_, cancel);
    std::vector<LyricSong> songs;
    if (link.provider == MusicProvider::netease) {
        if (link.kind == SearchKind::song) return client.netease_details({link.identifier}, cookie);
        if (link.kind == SearchKind::album) {
            const Json json = client.netease("/api/v1/album/" + link.identifier, {}, cookie).json;
            append_songs(songs, field(json, "songs"), netease_song);
            return songs;
        }
        const Json json = client.netease("/api/v6/playlist/detail", {{"id", link.identifier}, {"n", "1000"}, {"s", "0"}}, cookie).json;
        const Json& playlist = object_or_empty(field(json, "playlist"));
        std::vector<std::string> ids;
        for (const Json* item : objects(field(playlist, "trackIds"))) {
            if (std::string id = string(field(*item, "id")); !id.empty()) ids.push_back(std::move(id));
        }
        if (ids.empty()) {
            append_songs(songs, field(playlist, "tracks"), netease_song);
            return songs;
        }
        if (ids.size() > 10'000) throw LyricError("这个歌单超过 10,000 首，请拆分歌单后重试。");
        for (std::size_t start = 0; start < ids.size(); start += 300) {
            cancel.check();
            const auto end = ids.begin() + static_cast<std::ptrdiff_t>(std::min(start + 300, ids.size()));
            auto chunk = client.netease_details({ids.begin() + static_cast<std::ptrdiff_t>(start), end}, cookie);
            songs.insert(songs.end(), std::make_move_iterator(chunk.begin()), std::make_move_iterator(chunk.end()));
        }
        return songs;
    }
    const bool numeric = all_digits(link.identifier);
    if (link.kind == SearchKind::song) {
        const Json json = client.qq("/v8/fcg-bin/fcg_play_single_song.fcg",
                                    {{numeric ? "songid" : "songmid", link.identifier},
                                     {"tpl", "yqq_song_detail"},
                                     {"platform", "yqq"},
                                     {"needNewCode", "0"}},
                                    cookie);
        append_songs(songs, field(json, "data"), [](const Json& item) { return qq_song(item); });
        return songs;
    }
    if (link.kind == SearchKind::album) {
        const Json json = client.qq("/v8/fcg-bin/fcg_v8_album_info_cp.fcg", {{numeric ? "albumid" : "albummid", link.identifier}}, cookie);
        const Json& data = object_or_empty(field(json, "data"));
        const std::string album = string(field(data, "name"));
        append_songs(songs, field(data, "list"), [&](const Json& item) { return qq_song(item, album); });
        return songs;
    }
    const Json json = client.qq("/qzone/fcg-bin/fcg_ucc_getcdinfo_byids_cp.fcg",
                                {{"disstid", link.identifier},
                                 {"type", "1"},
                                 {"json", "1"},
                                 {"utf8", "1"},
                                 {"onlysong", "0"},
                                 {"new_format", "1"}},
                                cookie);
    const auto lists = objects(field(json, "cdlist"));
    if (lists.empty()) return songs;
    const Json& playlist = *lists.front();
    append_songs(songs, field(playlist, "songlist"), [](const Json& item) { return qq_song(item); });
    const std::int64_t total = number(first(playlist, {"total_song_num", "songnum"}));
    if (total > static_cast<std::int64_t>(songs.size())) {
        throw LyricError("QQ 音乐仅返回了歌单的 " + std::to_string(songs.size()) + " / " + std::to_string(total) +
                         " 首歌曲。请登录后重试，或拆分为较小歌单。");
    }
    return songs;
}

LyricDocument MusicApi::lyrics(const LyricSong& song, const std::string& cookie, const CancelToken& cancel) const {
    const Client client(transport_, clock_ms_, cancel);
    if (song.provider == MusicProvider::netease) {
        const Json json = client.netease("/api/song/lyric",
                                         {{"id", song.song_id}, {"lv", "-1"}, {"kv", "-1"}, {"tv", "-1"}, {"rv", "-1"}},
                                         cookie)
                              .json;
        auto lyric = [&](const char* key) {
            const Json& text = field(object_or_empty(field(json, key)), "lyric");
            return text.is_string() ? text.get<std::string>() : std::string();
        };
        LyricDocument result{lyric("lrc"), lyric("tlyric"), lyric("romalrc"), ""};
        // YRC is optional; its failure must not hide an otherwise available standard lyric.
        try {
            const Json word = client.netease("/api/song/lyric/v1",
                                             {{"id", song.song_id},
                                              {"lv", "0"},
                                              {"tv", "0"},
                                              {"rv", "0"},
                                              {"yv", "0"},
                                              {"ytv", "0"},
                                              {"yrv", "0"},
                                              {"cp", "false"}},
                                             cookie)
                                  .json;
            if (const Json& text = field(object_or_empty(field(word, "yrc")), "lyric"); text.is_string()) {
                result.word_timed = enhanced_lrc_from_yrc(text.get<std::string>());
            }
        } catch (const std::exception&) {
        }
        cancel.check();
        return result;
    }
    const Json json = client.qq("/lyric/fcgi-bin/fcg_query_lyric_new.fcg",
                                {{"songmid", song.song_id}, {"nobase64", "1"}, {"platform", "yqq"}, {"needNewCode", "0"}},
                                cookie);
    auto text = [&](const char* key) {
        const std::string value = string(field(json, key));
        if (value.find('[') != std::string::npos || value.find('\n') != std::string::npos) return decode_entities(value);
        if (auto decoded = detail::base64_decode(value)) {
            const std::string utf8(decoded->begin(), decoded->end());
            if (is_valid_utf8(utf8)) return decode_entities(utf8);
        }
        return decode_entities(value);
    };
    return {text("lyric"), text("trans"), text("roma"), ""};
}

QrTicket MusicApi::create_qr_code() const {
    const Client client(transport_, clock_ms_);
    const Json json = client.netease("/api/login/qrcode/unikey", {{"type", "3"}}).json;
    const Json& key = field(json, "unikey");
    if (!key.is_string() || key.get_ref<const std::string&>().empty()) throw LyricError("无法生成登录二维码，请重试。");
    return {key.get<std::string>(), "https://music.163.com/login?codekey=" + key.get<std::string>()};
}

QrStatus MusicApi::check_qr_code(const std::string& key) const {
    const Client client(transport_, clock_ms_);
    Response response = client.netease("/api/login/qrcode/client/login", {{"key", key}, {"type", "3"}});
    return {static_cast<int>(number(field(response.json, "code"))), std::move(response.cookies)};
}

std::optional<std::string> MusicApi::account_name(MusicProvider provider, const std::string& cookie) const {
    if (cookie.empty()) return std::nullopt;
    if (provider == MusicProvider::qq) return "QQ 音乐用户";
    const Client client(transport_, clock_ms_);
    const Json json = client.netease("/api/w/nuser/account/get", {}, cookie).json;
    const Json& profile = field(json, "profile");
    if (!profile.is_object()) return std::nullopt;
    const Json& nickname = field(profile, "nickname");
    return nickname.is_string() ? nickname.get<std::string>() : std::string("网易云用户");
}

}  // namespace mu
