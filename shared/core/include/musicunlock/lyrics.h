#pragma once
// Lyric model, LRC parsing and LRC / SRT / TXT / VTT export. Ported from
// apps/macos/Sources/MusicUnlockCore/Lyrics.swift with the same wording and rules.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "musicunlock/audio.h"

namespace mu {

/// User-facing, actionable lyric errors (UTF-8).
class LyricError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class MusicProvider { netease, qq };
inline constexpr MusicProvider all_providers[] = {MusicProvider::netease, MusicProvider::qq};
std::string_view provider_id(MusicProvider provider);
std::string_view provider_title(MusicProvider provider);
std::string_view provider_home(MusicProvider provider);
bool provider_owns(MusicProvider provider, std::string_view domain);

enum class SearchKind { song, album, playlist };
std::string_view search_kind_label(SearchKind kind);

struct LyricSong {
    MusicProvider provider = MusicProvider::netease;
    std::string song_id;
    std::string title;
    std::string artist;
    std::string album;
    std::int64_t duration_ms = 0;
    std::string numeric_id;
    std::string cover_url;

    std::string id() const;
    bool operator==(const LyricSong&) const = default;
};

struct MusicCollection {
    MusicProvider provider = MusicProvider::netease;
    std::string collection_id;
    SearchKind kind = SearchKind::album;
    std::string title;
    std::string subtitle;

    std::string id() const;
};

struct LyricLine {
    std::optional<std::int64_t> milliseconds;
    std::string text;
    bool operator==(const LyricLine&) const = default;
};

struct LyricDocument {
    std::string original;
    std::string translation;
    std::string romanization;
    std::string word_timed;

    /// True when the original has no lyric text (instrumental or missing).
    bool empty() const;
    bool operator==(const LyricDocument&) const = default;
};

enum class LyricFormat { lrc, srt, txt, vtt };
enum class LyricLayout { stagger, merge, separate };
enum class LyricEncoding { utf8, utf8_bom, utf16le };
std::string_view format_label(LyricFormat format);    // "LRC"
std::string_view format_extension(LyricFormat format);  // "lrc"
std::string_view layout_label(LyricLayout layout);    // "逐行交错"
std::string_view encoding_label(LyricEncoding encoding);  // "UTF-8 BOM"
Bytes encode_text(LyricEncoding encoding, std::string_view utf8);

struct LyricOptions {
    LyricFormat format = LyricFormat::lrc;
    LyricLayout layout = LyricLayout::stagger;
    LyricEncoding encoding = LyricEncoding::utf8;
    bool include_original = true;
    bool include_translation = true;
    bool include_romanization = false;
    bool include_pinyin = false;
    bool ignore_empty = true;
    bool skip_instrumental = true;
    bool word_timing = false;
    int precision = 3;
    std::int64_t offset = 0;
    std::string separator = " / ";
    std::string filename = "${name} - ${singer}";

    bool operator==(const LyricOptions&) const = default;
};

/// Settings persistence. Unknown or invalid fields fall back to defaults.
std::string options_to_json(const LyricOptions& options);
LyricOptions options_from_json(std::string_view json);

std::vector<LyricLine> parse_lyrics(std::string_view text);
/// NetEase YRC uses line and word durations in milliseconds.
std::string enhanced_lrc_from_yrc(std::string_view yrc);
std::string lyric_timestamp(std::int64_t milliseconds, LyricFormat format, int precision = 3);
std::string render_lyrics(const LyricDocument& document, const LyricOptions& options, std::int64_t duration_ms = 0);
std::string lyric_filename(const LyricSong& song, std::string_view name_template, int index);
/// Writes without overwriting; returns the created file.
std::filesystem::path save_lyrics(std::string_view text, const LyricSong& song, const LyricOptions& options, int index,
                                  const std::filesystem::path& directory);
/// Decodes a local LRC / TXT: UTF-8, UTF-16 with BOM, or GB18030 / GBK. Strips BOMs; rejects files over 5 MB.
std::string decode_lyric_file(ByteSpan data);

/// Mandarin to pinyin with tone marks through the system ICU (Windows) or CoreFoundation (Mac).
/// Returns nullopt when the system has no transliteration support.
std::optional<std::string> mandarin_to_latin(std::string_view text);

struct MusicLink {
    MusicProvider provider = MusicProvider::netease;
    SearchKind kind = SearchKind::song;
    std::string identifier;

    /// Accepts an ID, a QQ 14-character mid, or text containing a full music.163.com / y.qq.com link.
    static std::optional<MusicLink> parse(std::string_view input, MusicProvider provider, SearchKind kind);
    bool operator==(const MusicLink&) const = default;
};

}  // namespace mu
