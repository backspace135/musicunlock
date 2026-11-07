#include "musicunlock/lyrics.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <set>

#include <nlohmann/json.hpp>

#include "detail.h"
#include "musicunlock/text.h"

namespace mu {

namespace {

constexpr std::int64_t no_time = std::numeric_limits<std::int64_t>::max();

bool is_digit(char c) { return c >= '0' && c <= '9'; }

std::size_t digit_run(std::string_view s, std::size_t at) {
    std::size_t end = at;
    while (end < s.size() && is_digit(s[end])) ++end;
    return end - at;
}

/// Decimal digits with an optional sign, like Swift's Int(String); nullopt on overflow or junk.
std::optional<std::int64_t> parse_int(std::string_view text) {
    bool negative = false;
    if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }
    if (text.empty()) return std::nullopt;
    std::int64_t value = 0;
    for (char c : text) {
        if (!is_digit(c)) return std::nullopt;
        if (value > (std::numeric_limits<std::int64_t>::max() - (c - '0')) / 10) return std::nullopt;
        value = value * 10 + (c - '0');
    }
    return negative ? -value : value;
}

std::int64_t fraction_ms(std::string_view digits) {
    const std::int64_t value = parse_int(digits).value_or(0);
    return value * (digits.size() == 1 ? 100 : digits.size() == 2 ? 10 : 1);
}

struct Stamp {
    std::size_t begin = 0;
    std::size_t end = 0;
    std::string_view minutes;
    std::string_view seconds;
    std::string_view fraction;
};

/// Matches \[(\d{1,6}):(\d{2})(?:[.:](\d{1,3}))?\] at `at`.
std::optional<Stamp> match_stamp(std::string_view s, std::size_t at) {
    if (at >= s.size() || s[at] != '[') return std::nullopt;
    const std::size_t minutes = digit_run(s, at + 1);
    std::size_t i = at + 1 + minutes;
    if (minutes < 1 || minutes > 6 || i >= s.size() || s[i] != ':') return std::nullopt;
    if (i + 2 >= s.size() || !is_digit(s[i + 1]) || !is_digit(s[i + 2])) return std::nullopt;
    Stamp stamp{at, 0, s.substr(at + 1, minutes), s.substr(i + 1, 2), {}};
    i += 3;
    if (i < s.size() && (s[i] == '.' || s[i] == ':')) {
        const std::size_t fraction = digit_run(s, i + 1);
        if (fraction >= 1 && fraction <= 3 && i + 1 + fraction < s.size() && s[i + 1 + fraction] == ']') {
            stamp.fraction = s.substr(i + 1, fraction);
            stamp.end = i + 2 + fraction;
            return stamp;
        }
    }
    if (i < s.size() && s[i] == ']') {
        stamp.end = i + 1;
        return stamp;
    }
    return std::nullopt;
}

std::vector<Stamp> find_stamps(std::string_view s) {
    std::vector<Stamp> stamps;
    for (std::size_t i = 0; i < s.size();) {
        if (auto stamp = match_stamp(s, i)) {
            stamps.push_back(*stamp);
            i = stamp->end;
        } else {
            ++i;
        }
    }
    return stamps;
}

/// First case-insensitive [offset:±n] tag with up to nine digits.
std::int64_t offset_tag(std::string_view text) {
    const std::string lower = ascii_lower(text);
    for (std::size_t at = lower.find("[offset:"); at != std::string::npos; at = lower.find("[offset:", at + 1)) {
        std::size_t i = at + 8;
        const std::size_t sign = i < lower.size() && (lower[i] == '+' || lower[i] == '-') ? 1 : 0;
        const std::size_t digits = digit_run(lower, i + sign);
        if (digits >= 1 && digits <= 9 && i + sign + digits < lower.size() && lower[i + sign + digits] == ']') {
            return parse_int(lower.substr(i, sign + digits)).value_or(0);
        }
    }
    return 0;
}

std::vector<std::string_view> split_lines(std::string_view text) {
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' || text[i] == '\n') {
            lines.push_back(text.substr(start, i - start));
            if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
            start = i + 1;
        }
    }
    lines.push_back(text.substr(start));
    return lines;
}

template <typename Key>
void stable_sort_by_time(std::vector<LyricLine>& lines, Key key) {
    std::stable_sort(lines.begin(), lines.end(), [&](const LyricLine& a, const LyricLine& b) { return key(a) < key(b); });
}

std::int64_t time_or_max(const LyricLine& line) { return line.milliseconds.value_or(no_time); }

std::string printf_string(const char* pattern, auto... values) {
    char buffer[96];
    const int length = std::snprintf(buffer, sizeof buffer, pattern, values...);
    return std::string(buffer, static_cast<std::size_t>(std::max(0, length)));
}

std::vector<LyricLine> aligned(std::vector<LyricLine> lines, const std::vector<LyricLine>& originals) {
    std::vector<std::int64_t> starts;
    for (const auto& line : originals) {
        if (line.milliseconds) starts.push_back(*line.milliseconds);
    }
    for (auto& line : lines) {
        if (!line.milliseconds || starts.empty()) continue;
        const std::int64_t time = *line.milliseconds;
        const auto nearest = *std::min_element(starts.begin(), starts.end(), [&](std::int64_t a, std::int64_t b) {
            return std::llabs(a - time) < std::llabs(b - time);
        });
        if (std::llabs(nearest - time) <= 80) line.milliseconds = nearest;
    }
    return lines;
}

/// Matches \[\d+:\d{2}(?:\.\d{1,3})?\] at `at`; returns the end offset.
std::optional<std::size_t> match_word_stamp(std::string_view s, std::size_t at) {
    if (s[at] != '[') return std::nullopt;
    const std::size_t minutes = digit_run(s, at + 1);
    std::size_t i = at + 1 + minutes;
    if (minutes == 0 || i + 2 >= s.size() || s[i] != ':' || !is_digit(s[i + 1]) || !is_digit(s[i + 2])) {
        return std::nullopt;
    }
    i += 3;
    if (i < s.size() && s[i] == '.') {
        const std::size_t fraction = digit_run(s, i + 1);
        if (fraction >= 1 && fraction <= 3 && i + 1 + fraction < s.size() && s[i + 1 + fraction] == ']') {
            return i + 2 + fraction;
        }
    }
    if (i < s.size() && s[i] == ']') return i + 1;
    return std::nullopt;
}

std::string shifted_word_lrc(std::string_view text, std::int64_t offset, int precision) {
    std::string result;
    for (std::size_t i = 0; i < text.size();) {
        if (auto end = match_word_stamp(text, i)) {
            const std::string_view token = text.substr(i, *end - i);
            const auto parsed = parse_lyrics(token);
            if (!parsed.empty() && parsed.front().milliseconds) {
                result += lyric_timestamp(*parsed.front().milliseconds + offset, LyricFormat::lrc, precision);
            } else {
                result += token;
            }
            i = *end;
        } else {
            result += text[i++];
        }
    }
    return result;
}

const char* format_id(LyricFormat f) {
    switch (f) {
        case LyricFormat::srt: return "srt";
        case LyricFormat::txt: return "txt";
        case LyricFormat::vtt: return "vtt";
        case LyricFormat::lrc: break;
    }
    return "lrc";
}

const char* layout_id(LyricLayout l) {
    return l == LyricLayout::merge ? "merge" : l == LyricLayout::separate ? "separate" : "stagger";
}

const char* encoding_id(LyricEncoding e) {
    return e == LyricEncoding::utf8_bom ? "utf8_bom" : e == LyricEncoding::utf16le ? "utf16le" : "utf8";
}

}  // namespace

// MARK: - Providers and model

std::string_view provider_id(MusicProvider provider) { return provider == MusicProvider::netease ? "netease" : "qq"; }
std::string_view provider_title(MusicProvider provider) {
    return provider == MusicProvider::netease ? "网易云音乐" : "QQ 音乐";
}
std::string_view provider_home(MusicProvider provider) {
    return provider == MusicProvider::netease ? "https://music.163.com/" : "https://y.qq.com/";
}

bool provider_owns(MusicProvider provider, std::string_view domain) {
    std::string host = ascii_lower(domain);
    while (!host.empty() && host.front() == '.') host.erase(0, 1);
    while (!host.empty() && host.back() == '.') host.pop_back();
    const std::string root = provider == MusicProvider::netease ? "music.163.com" : "y.qq.com";
    return host == root || host.ends_with("." + root) || (provider == MusicProvider::qq && host == "qq.com");
}

std::string_view search_kind_label(SearchKind kind) {
    return kind == SearchKind::song ? "歌曲" : kind == SearchKind::album ? "专辑" : "歌单";
}

std::string LyricSong::id() const { return std::string(provider_id(provider)) + ":" + song_id; }

std::string MusicCollection::id() const {
    return std::string(provider_id(provider)) + ":" + std::string(search_kind_label(kind)) + ":" + collection_id;
}

bool LyricDocument::empty() const {
    const auto lines = parse_lyrics(original);
    return std::all_of(lines.begin(), lines.end(), [](const LyricLine& line) { return line.text.empty(); });
}

std::string_view format_label(LyricFormat f) {
    switch (f) {
        case LyricFormat::srt: return "SRT";
        case LyricFormat::txt: return "TXT";
        case LyricFormat::vtt: return "VTT";
        case LyricFormat::lrc: break;
    }
    return "LRC";
}
std::string_view format_extension(LyricFormat f) { return format_id(f); }
std::string_view layout_label(LyricLayout l) {
    return l == LyricLayout::merge ? "同一行合并" : l == LyricLayout::separate ? "分段排列" : "逐行交错";
}
std::string_view encoding_label(LyricEncoding e) {
    return e == LyricEncoding::utf8_bom ? "UTF-8 BOM" : e == LyricEncoding::utf16le ? "UTF-16 LE" : "UTF-8";
}

Bytes encode_text(LyricEncoding encoding, std::string_view utf8) {
    Bytes out;
    if (encoding == LyricEncoding::utf16le) {
        out = {0xff, 0xfe};
        for (char16_t unit : utf8_to_utf16(utf8)) {
            out.push_back(static_cast<std::uint8_t>(unit));
            out.push_back(static_cast<std::uint8_t>(unit >> 8));
        }
        return out;
    }
    if (encoding == LyricEncoding::utf8_bom) out = {0xef, 0xbb, 0xbf};
    out.insert(out.end(), utf8.begin(), utf8.end());
    return out;
}

std::string options_to_json(const LyricOptions& o) {
    return nlohmann::json{{"format", format_id(o.format)},
                          {"layout", layout_id(o.layout)},
                          {"encoding", encoding_id(o.encoding)},
                          {"includeOriginal", o.include_original},
                          {"includeTranslation", o.include_translation},
                          {"includeRomanization", o.include_romanization},
                          {"includePinyin", o.include_pinyin},
                          {"ignoreEmpty", o.ignore_empty},
                          {"skipInstrumental", o.skip_instrumental},
                          {"wordTiming", o.word_timing},
                          {"precision", o.precision},
                          {"offset", o.offset},
                          {"separator", o.separator},
                          {"filename", o.filename}}
        .dump();
}

LyricOptions options_from_json(std::string_view text) {
    LyricOptions o;
    const auto json = nlohmann::json::parse(text, nullptr, false);
    if (!json.is_object()) return o;
    auto get_bool = [&](const char* key, bool& field) {
        if (auto it = json.find(key); it != json.end() && it->is_boolean()) field = it->get<bool>();
    };
    auto get_string = [&](const char* key) -> std::optional<std::string> {
        if (auto it = json.find(key); it != json.end() && it->is_string()) return it->get<std::string>();
        return std::nullopt;
    };
    for (auto f : {LyricFormat::lrc, LyricFormat::srt, LyricFormat::txt, LyricFormat::vtt}) {
        if (get_string("format") == format_id(f)) o.format = f;
    }
    for (auto l : {LyricLayout::stagger, LyricLayout::merge, LyricLayout::separate}) {
        if (get_string("layout") == layout_id(l)) o.layout = l;
    }
    for (auto e : {LyricEncoding::utf8, LyricEncoding::utf8_bom, LyricEncoding::utf16le}) {
        if (get_string("encoding") == encoding_id(e)) o.encoding = e;
    }
    get_bool("includeOriginal", o.include_original);
    get_bool("includeTranslation", o.include_translation);
    get_bool("includeRomanization", o.include_romanization);
    get_bool("includePinyin", o.include_pinyin);
    get_bool("ignoreEmpty", o.ignore_empty);
    get_bool("skipInstrumental", o.skip_instrumental);
    get_bool("wordTiming", o.word_timing);
    if (auto it = json.find("precision"); it != json.end() && it->is_number_integer()) {
        o.precision = std::clamp(it->get<int>(), 0, 3);
    }
    if (auto it = json.find("offset"); it != json.end() && it->is_number_integer()) {
        o.offset = std::clamp<std::int64_t>(it->get<std::int64_t>(), -86'400'000, 86'400'000);
    }
    if (auto value = get_string("separator")) o.separator = *value;
    if (auto value = get_string("filename"); value && !value->empty()) o.filename = *value;
    return o;
}

// MARK: - Parsing

std::vector<LyricLine> parse_lyrics(std::string_view input) {
    const std::int64_t adjustment = offset_tag(input);
    std::vector<LyricLine> lines;
    for (std::string_view raw : split_lines(input)) {
        const std::string_view row = trim_space(raw);
        if (row.empty()) continue;
        const auto stamps = find_stamps(row);
        if (stamps.empty()) {
            if (row.front() == '[' && row.find(':') != std::string_view::npos && row.back() == ']') continue;
            lines.push_back({std::nullopt, std::string(row)});
            continue;
        }
        // Enhanced LRC contains inline word timestamps; the first timestamp is the line start.
        const bool word_timed = stamps.size() > 1 && std::any_of(stamps.begin() + 1, stamps.end(), [&](const Stamp& s) {
            return !row.substr(0, s.begin).ends_with(']');
        });
        std::string text;
        std::size_t cursor = 0;
        for (const auto& stamp : stamps) {
            text.append(row.substr(cursor, stamp.begin - cursor));
            cursor = stamp.end;
        }
        text.append(row.substr(cursor));
        text = std::string(trim_space(text));
        for (std::size_t i = 0; i < (word_timed ? 1 : stamps.size()); ++i) {
            const auto& stamp = stamps[i];
            const std::int64_t minutes = parse_int(stamp.minutes).value_or(0);
            const std::int64_t seconds = parse_int(stamp.seconds).value_or(0);
            const std::int64_t ms = (minutes * 60 + seconds) * 1000 + fraction_ms(stamp.fraction) + adjustment;
            lines.push_back({std::max<std::int64_t>(0, ms), text});
        }
    }
    stable_sort_by_time(lines, time_or_max);
    return lines;
}

std::string enhanced_lrc_from_yrc(std::string_view input) {
    std::vector<std::string> rows;
    for (std::string_view row : split_lines(input)) {
        // ^\[(\d+),(\d+)\]
        if (row.empty() || row.front() != '[') continue;
        const std::size_t start_digits = digit_run(row, 1);
        const std::size_t comma = 1 + start_digits;
        if (start_digits == 0 || comma >= row.size() || row[comma] != ',') continue;
        const std::size_t duration_digits = digit_run(row, comma + 1);
        const std::size_t close = comma + 1 + duration_digits;
        if (duration_digits == 0 || close >= row.size() || row[close] != ']') continue;
        const auto start = parse_int(row.substr(1, start_digits));
        if (!start) continue;
        // \((\d+),(\d+),\d+\)([^\(]*)
        std::string line;
        bool any = false;
        for (std::size_t i = close + 1; i < row.size();) {
            if (row[i] != '(') {
                ++i;
                continue;
            }
            std::size_t j = i + 1;
            const std::size_t time_digits = digit_run(row, j);
            bool ok = time_digits > 0;
            j += time_digits;
            for (int group = 0; ok && group < 2; ++group) {
                ok = j < row.size() && row[j] == ',' && digit_run(row, j + 1) > 0;
                if (ok) j += 1 + digit_run(row, j + 1);
            }
            if (!ok || j >= row.size() || row[j] != ')') {
                ++i;
                continue;
            }
            std::size_t text_end = row.find('(', j + 1);
            if (text_end == std::string_view::npos) text_end = row.size();
            const std::int64_t time = parse_int(row.substr(i + 1, time_digits)).value_or(*start);
            line += lyric_timestamp(time, LyricFormat::lrc, 3);
            line += row.substr(j + 1, text_end - j - 1);
            any = true;
            i = text_end;
        }
        if (any) rows.push_back(std::move(line));
    }
    std::string result;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (i) result += '\n';
        result += rows[i];
    }
    return result;
}

// MARK: - Export

std::string lyric_timestamp(std::int64_t value, LyricFormat format, int precision) {
    const std::int64_t ms = std::max<std::int64_t>(0, value);
    if (format == LyricFormat::lrc) {
        const int digits = std::clamp(precision, 0, 3);
        std::string fraction;
        if (digits > 0) {
            const std::int64_t divisor = digits == 1 ? 100 : digits == 2 ? 10 : 1;
            fraction = "." + printf_string("%0*" PRId64, digits, (ms % 1000) / divisor);
        }
        return printf_string("[%02" PRId64 ":%02" PRId64, ms / 60'000, (ms / 1000) % 60) + fraction + "]";
    }
    return printf_string("%02" PRId64 ":%02" PRId64 ":%02" PRId64 "%c%03" PRId64, ms / 3'600'000, (ms / 60'000) % 60,
                      (ms / 1000) % 60, format == LyricFormat::srt ? ',' : '.', ms % 1000);
}

std::string render_lyrics(const LyricDocument& document, const LyricOptions& options, std::int64_t duration) {
    const std::int64_t offset = options.offset;
    if (offset == std::numeric_limits<std::int64_t>::min() || std::llabs(offset) > 86'400'000) {
        throw LyricError("时间偏移请设置在正负 24 小时以内。");
    }
    if (!options.include_original && !options.include_translation && !options.include_romanization &&
        !options.include_pinyin) {
        throw LyricError("请至少选择一种歌词内容。");
    }
    const auto original = parse_lyrics(document.original);
    std::vector<std::vector<LyricLine>> tracks;
    if (options.include_original) tracks.push_back(original);
    if (options.include_translation) tracks.push_back(aligned(parse_lyrics(document.translation), original));
    if (options.include_romanization) tracks.push_back(aligned(parse_lyrics(document.romanization), original));
    if (options.include_pinyin) {
        std::vector<LyricLine> pinyin;
        for (const auto& line : original) {
            auto latin = mandarin_to_latin(line.text);
            if (!latin) throw LyricError("此系统不支持中文拼音转换，请取消“拼音”，或升级到 Windows 10 1903 及更新版本。");
            pinyin.push_back({line.milliseconds, std::move(*latin)});
        }
        tracks.push_back(std::move(pinyin));
    }
    std::vector<std::vector<LyricLine>> kept;
    for (auto& track : tracks) {
        if (options.ignore_empty) {
            std::erase_if(track, [](const LyricLine& line) { return line.text.empty(); });
        }
        if (!track.empty()) kept.push_back(std::move(track));
    }
    if (kept.empty()) throw LyricError("所选内容没有可导出的歌词。可切换原文，或导入本地歌词。");
    const bool subtitle = options.format == LyricFormat::srt || options.format == LyricFormat::vtt;
    std::vector<LyricLine> rows;
    for (const auto& track : kept) rows.insert(rows.end(), track.begin(), track.end());
    if (subtitle && std::any_of(rows.begin(), rows.end(), [](const LyricLine& l) { return !l.milliseconds; })) {
        throw LyricError("歌词中有未标记时间的行，无法生成字幕。请补上 [分:秒.毫秒]，或改用 TXT。");
    }
    if (options.word_timing) {
        if (options.format != LyricFormat::lrc || !options.include_original || options.include_translation ||
            options.include_romanization || options.include_pinyin) {
            throw LyricError("逐字导出仅支持 LRC 原文，请关闭译文、罗马音和拼音。");
        }
        if (document.word_timed.empty()) throw LyricError("平台未提供这首歌的逐字歌词，请关闭逐字导出。");
        return shifted_word_lrc(document.word_timed, offset, options.precision) + "\n";
    }
    if (options.layout != LyricLayout::separate || subtitle) stable_sort_by_time(rows, time_or_max);
    if (subtitle || options.layout == LyricLayout::merge) {
        std::vector<LyricLine> merged;
        for (auto& line : rows) {
            if (!merged.empty() && line.milliseconds && line.milliseconds == merged.back().milliseconds) {
                if (!line.text.empty()) merged.back().text += (subtitle ? std::string("\n") : options.separator) + line.text;
            } else {
                merged.push_back(std::move(line));
            }
        }
        rows = std::move(merged);
    }
    if (subtitle) {
        // Empty timed rows are real cue boundaries even when not included in the export.
        std::set<std::int64_t> boundaries;
        for (const std::vector<LyricLine>* list : {&original, static_cast<const std::vector<LyricLine>*>(&rows)}) {
            for (const auto& line : *list) {
                if (line.milliseconds) boundaries.insert(*line.milliseconds);
            }
        }
        std::vector<std::string> cues;
        for (const auto& line : rows) {
            if (line.text.empty() || !line.milliseconds) continue;
            const std::int64_t raw_start = *line.milliseconds;
            const auto next = boundaries.upper_bound(raw_start);
            const std::int64_t raw_end =
                next != boundaries.end() ? *next : (duration > raw_start ? duration : raw_start + 5000);
            const std::int64_t start = std::max<std::int64_t>(0, raw_start + offset);
            const std::int64_t end = std::max<std::int64_t>(0, raw_end + offset);
            if (end <= start) continue;
            std::string cue = options.format == LyricFormat::srt ? std::to_string(cues.size() + 1) + "\n" : "";
            cue += lyric_timestamp(start, options.format) + " --> " + lyric_timestamp(end, options.format) + "\n" + line.text;
            cues.push_back(std::move(cue));
        }
        std::string result = options.format == LyricFormat::vtt ? "WEBVTT\n\n" : "";
        for (std::size_t i = 0; i < cues.size(); ++i) {
            if (i) result += "\n\n";
            result += cues[i];
        }
        return result + "\n";
    }
    std::string result;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (i) result += '\n';
        if (options.format == LyricFormat::lrc && rows[i].milliseconds) {
            result += lyric_timestamp(*rows[i].milliseconds + offset, LyricFormat::lrc, options.precision);
        }
        result += rows[i].text;
    }
    return result + "\n";
}

std::string lyric_filename(const LyricSong& song, std::string_view name_template, int index) {
    std::string name(name_template);
    const std::pair<const char*, std::string> values[] = {{"name", song.title},   {"singer", song.artist},
                                                          {"album", song.album},  {"id", song.song_id},
                                                          {"index", std::to_string(index)}};
    for (const auto& [key, value] : values) name = replace_all(std::move(name), "${" + std::string(key) + "}", value);
    return detail::sanitize_component(name, 160, "歌词", true);
}

std::filesystem::path save_lyrics(std::string_view text, const LyricSong& song, const LyricOptions& options, int index,
                                  const std::filesystem::path& directory) {
    const std::string name = lyric_filename(song, options.filename, index);
    const Bytes data = encode_text(options.encoding, text);
    for (int suffix = 0; suffix < 10000; ++suffix) {
        const std::string stem = suffix == 0 ? name : name + " (" + std::to_string(suffix) + ")";
        const auto target = directory / utf8_to_path(stem + "." + std::string(format_extension(options.format)));
        if (detail::write_new_file(target, data)) return target;
    }
    throw LyricError("同名文件过多，请更换文件名或输出文件夹。");
}

std::string decode_lyric_file(ByteSpan data) {
    if (data.size() > 5'000'000) throw LyricError("歌词文件超过 5 MB，请选择较小的 LRC 或 TXT。");
    std::optional<std::string> text;
    const bool little = data.size() >= 2 && data[0] == 0xff && data[1] == 0xfe;
    const bool big = data.size() >= 2 && data[0] == 0xfe && data[1] == 0xff;
    if (little || big) {
        if (data.size() % 2 == 0) {
            std::u16string units;
            for (std::size_t i = 2; i + 1 < data.size(); i += 2) {
                units.push_back(little ? static_cast<char16_t>(data[i] | data[i + 1] << 8)
                                       : static_cast<char16_t>(data[i] << 8 | data[i + 1]));
            }
            text = utf16_to_utf8(units);
        }
    } else {
        const std::string_view bytes(reinterpret_cast<const char*>(data.data()), data.size());
        // Chinese Windows LRC files are frequently GBK; GB18030 is its superset.
        text = is_valid_utf8(bytes) ? std::optional<std::string>(bytes) : detail::gb18030_to_utf8(data);
    }
    if (!text) throw LyricError("无法读取文字编码，请将歌词转为 UTF-8、UTF-16 或 GBK。");
    return replace_all(std::move(*text), "\xEF\xBB\xBF", "");
}

// MARK: - Links

namespace {

std::string percent_decode(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        auto hex = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        if (text[i] == '%' && i + 2 < text.size() && hex(text[i + 1]) >= 0 && hex(text[i + 2]) >= 0) {
            out.push_back(static_cast<char>(hex(text[i + 1]) * 16 + hex(text[i + 2])));
            i += 2;
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

bool is_alnum_id(std::string_view id, bool require_digit) {
    if (id.empty()) return false;
    bool digit = false;
    for (char c : id) {
        if (!(is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
        digit = digit || is_digit(c);
    }
    return !require_digit || digit;
}

/// Extent of https?://[^\s<>\"，]+ starting at `at`.
std::size_t url_end(std::string_view text, std::size_t at) {
    std::size_t i = at;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c <= 0x20 || c == '<' || c == '>' || c == '"') break;
        const std::string_view rest = text.substr(i);
        // Full-width comma and Unicode spaces commonly found in share text.
        if (rest.starts_with("\xEF\xBC\x8C") || rest.starts_with("\xE3\x80\x80") || rest.starts_with("\xC2\xA0") ||
            rest.starts_with("\xC2\x85") || rest.starts_with("\xE2\x80\xA8") || rest.starts_with("\xE2\x80\xA9")) {
            break;
        }
        ++i;
    }
    return i;
}

}  // namespace

std::optional<MusicLink> MusicLink::parse(std::string_view input, MusicProvider provider, SearchKind kind) {
    const std::string_view trimmed = trim_space(input);
    if (!trimmed.empty() && std::all_of(trimmed.begin(), trimmed.end(), is_digit)) {
        return MusicLink{provider, kind, std::string(trimmed)};
    }
    if (provider == MusicProvider::qq && trimmed.size() == 14 && is_alnum_id(trimmed, false)) {
        return MusicLink{MusicProvider::qq, kind, std::string(trimmed)};
    }
    std::size_t start = std::string_view::npos;
    for (std::size_t at = trimmed.find("http"); at != std::string_view::npos; at = trimmed.find("http", at + 1)) {
        if (trimmed.substr(at).starts_with("http://") || trimmed.substr(at).starts_with("https://")) {
            start = at;
            break;
        }
    }
    if (start == std::string_view::npos) return std::nullopt;
    const std::string url(trimmed.substr(start, url_end(trimmed, start) - start));

    // Host of the original URL (before "#/" normalisation), without userinfo or port.
    const std::size_t authority = url.find("://") + 3;
    const std::size_t authority_end = std::min({url.find('/', authority), url.find('?', authority), url.find('#', authority)});
    std::string host = url.substr(authority, authority_end == std::string::npos ? std::string::npos : authority_end - authority);
    if (auto at = host.rfind('@'); at != std::string::npos) host.erase(0, at + 1);
    if (auto colon = host.find(':'); colon != std::string::npos) host.erase(colon);
    host = ascii_lower(host);
    if (host.empty()) return std::nullopt;
    MusicProvider source;
    if (provider_owns(MusicProvider::netease, host)) source = MusicProvider::netease;
    else if (host == "y.qq.com" || host.ends_with(".y.qq.com")) source = MusicProvider::qq;
    else return std::nullopt;

    const std::string normalized = replace_all(url, "#/", "");
    const std::size_t rest_start = normalized.find("://") + 3;
    const std::size_t path_start = normalized.find_first_of("/?#", rest_start);
    std::string path, query;
    if (path_start != std::string::npos) {
        const std::size_t fragment = normalized.find('#', path_start);
        const std::string before_fragment = normalized.substr(path_start, fragment == std::string::npos ? std::string::npos : fragment - path_start);
        const std::size_t question = before_fragment.find('?');
        path = before_fragment.substr(0, question);
        if (question != std::string::npos) query = before_fragment.substr(question + 1);
    }
    std::vector<std::pair<std::string, std::optional<std::string>>> params;
    for (std::size_t at = 0; !query.empty() && at <= query.size();) {
        std::size_t amp = query.find('&', at);
        if (amp == std::string::npos) amp = query.size();
        const std::string item = query.substr(at, amp - at);
        const std::size_t equals = item.find('=');
        params.emplace_back(percent_decode(item.substr(0, equals)),
                            equals == std::string::npos ? std::nullopt
                                                        : std::optional<std::string>(percent_decode(item.substr(equals + 1))));
        at = amp + 1;
    }
    auto param = [&](std::initializer_list<std::string_view> keys) -> std::optional<std::string> {
        for (const auto& [name, value] : params) {
            if (std::find(keys.begin(), keys.end(), name) != keys.end()) return value;
        }
        return std::nullopt;
    };

    SearchKind detected = kind;
    if (normalized.find("playlist") != std::string::npos || normalized.find("songlist") != std::string::npos ||
        normalized.find("disstid") != std::string::npos) {
        detected = SearchKind::playlist;
    } else if (normalized.find("album") != std::string::npos) {
        detected = SearchKind::album;
    } else if (normalized.find("song") != std::string::npos) {
        detected = SearchKind::song;
    }
    std::optional<std::string> identifier;
    if (source == MusicProvider::netease) identifier = param({"id"});
    else if (detected == SearchKind::playlist) identifier = param({"disstid", "id"});
    else if (detected == SearchKind::album) identifier = param({"albummid", "albumid", "id"});
    else identifier = param({"songmid", "songid", "id"});
    if (!identifier) {
        std::string last = path.substr(path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1);
        identifier = replace_all(percent_decode(last), ".html", "");
    }
    if (!is_alnum_id(*identifier, true)) return std::nullopt;
    return MusicLink{source, detected, *identifier};
}

}  // namespace mu
