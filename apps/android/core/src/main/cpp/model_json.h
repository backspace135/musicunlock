#pragma once
// JSON forms of the core's lyric, request and session structs, as the Kotlin side reads and writes them
// (LyricModels.kt). Field names are the Kotlin property names. Enums travel as their ordinal in the core's order.
#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

#include "musicunlock/lyric_workspace.h"
#include "musicunlock/lyrics.h"
#include "musicunlock/music_api.h"
#include "musicunlock/qr_login.h"
#include "musicunlock/session.h"

namespace mu::jni {

using Json = nlohmann::json;

/// Core strings are UTF-8 but platform data may not be; never let a dump throw on it.
inline std::string dump(const Json& json) { return json.dump(-1, ' ', false, Json::error_handler_t::replace); }

/// Parses what Kotlin sent; malformed input is a bug on the Kotlin side, reported as an error.
inline Json parse(std::string_view text) {
    Json json = Json::parse(text, nullptr, false);
    if (json.is_discarded()) throw std::invalid_argument("invalid JSON from Kotlin");
    return json;
}

template <class Enum>
Enum enum_at(const Json& json, const char* key, std::initializer_list<Enum> values) {
    const int index = json.value(key, 0);
    return index >= 0 && static_cast<std::size_t>(index) < values.size() ? values.begin()[index] : *values.begin();
}

template <class Enum>
Enum nth(std::initializer_list<Enum> values, int index) {
    return index >= 0 && static_cast<std::size_t>(index) < values.size() ? values.begin()[index] : *values.begin();
}

template <class Enum>
int index_of(Enum value, std::initializer_list<Enum> values) {
    int index = 0;
    for (Enum candidate : values) {
        if (candidate == value) return index;
        ++index;
    }
    return 0;
}

inline constexpr auto providers = {MusicProvider::netease, MusicProvider::qq};
inline constexpr auto kinds = {SearchKind::song, SearchKind::album, SearchKind::playlist};
inline constexpr auto formats = {LyricFormat::lrc, LyricFormat::srt, LyricFormat::txt, LyricFormat::vtt};
inline constexpr auto layouts = {LyricLayout::stagger, LyricLayout::merge, LyricLayout::separate};
inline constexpr auto encodings = {LyricEncoding::utf8, LyricEncoding::utf8_bom, LyricEncoding::utf16le};
inline constexpr auto tabs = {LyricTab::original, LyricTab::translation, LyricTab::romanization, LyricTab::preview};
inline constexpr auto qr_states = {QrLoginState::loading, QrLoginState::waiting, QrLoginState::scanned,
                                   QrLoginState::confirmed, QrLoginState::expired, QrLoginState::failed};

inline Json to_json(const LyricSong& song) {
    return {{"provider", index_of(song.provider, providers)},
            {"songId", song.song_id},
            {"title", song.title},
            {"artist", song.artist},
            {"album", song.album},
            {"durationMs", song.duration_ms},
            {"numericId", song.numeric_id},
            {"coverUrl", song.cover_url},
            {"id", song.id()}};
}

inline LyricSong song_from(const Json& json) {
    LyricSong song;
    song.provider = enum_at(json, "provider", providers);
    song.song_id = json.value("songId", "");
    song.title = json.value("title", "");
    song.artist = json.value("artist", "");
    song.album = json.value("album", "");
    song.duration_ms = json.value("durationMs", std::int64_t{0});
    song.numeric_id = json.value("numericId", "");
    song.cover_url = json.value("coverUrl", "");
    return song;
}

inline Json to_json(const MusicCollection& collection) {
    return {{"provider", index_of(collection.provider, providers)},
            {"collectionId", collection.collection_id},
            {"kind", index_of(collection.kind, kinds)},
            {"title", collection.title},
            {"subtitle", collection.subtitle},
            {"id", collection.id()}};
}

inline MusicCollection collection_from(const Json& json) {
    MusicCollection collection;
    collection.provider = enum_at(json, "provider", providers);
    collection.collection_id = json.value("collectionId", "");
    collection.kind = enum_at(json, "kind", kinds);
    collection.title = json.value("title", "");
    collection.subtitle = json.value("subtitle", "");
    return collection;
}

inline Json to_json(const LyricDocument& document) {
    return {{"original", document.original},
            {"translation", document.translation},
            {"romanization", document.romanization},
            {"wordTimed", document.word_timed},
            {"empty", document.empty()}};
}

inline LyricDocument document_from(const Json& json) {
    return {json.value("original", ""), json.value("translation", ""), json.value("romanization", ""),
            json.value("wordTimed", "")};
}

inline Json to_json(const MusicLink& link) {
    return {{"provider", index_of(link.provider, providers)},
            {"kind", index_of(link.kind, kinds)},
            {"identifier", link.identifier}};
}

inline MusicLink link_from(const Json& json) {
    return {enum_at(json, "provider", providers), enum_at(json, "kind", kinds), json.value("identifier", "")};
}

template <class T, class F>
Json array_of(const std::vector<T>& items, F&& convert) {
    Json array = Json::array();
    for (const auto& item : items) array.push_back(convert(item));
    return array;
}

inline Json to_json(const SearchResult& result) {
    return {{"songs", array_of(result.songs, [](const LyricSong& s) { return to_json(s); })},
            {"collections", array_of(result.collections, [](const MusicCollection& c) { return to_json(c); })},
            {"hasMore", result.has_more}};
}

inline SearchResult search_result_from(const Json& json) {
    SearchResult result;
    for (const auto& song : json.value("songs", Json::array())) result.songs.push_back(song_from(song));
    for (const auto& item : json.value("collections", Json::array())) result.collections.push_back(collection_from(item));
    result.has_more = json.value("hasMore", false);
    return result;
}

inline Json to_json(const SearchRequest& request) {
    return {{"serial", request.serial},
            {"text", request.text},
            {"provider", index_of(request.provider, providers)},
            {"kind", index_of(request.kind, kinds)},
            {"page", request.page},
            {"more", request.more},
            {"link", request.link ? to_json(*request.link) : Json(nullptr)},
            {"collectionTitle", request.collection_title}};
}

inline SearchRequest search_request_from(const Json& json) {
    SearchRequest request;
    request.serial = json.value("serial", std::uint64_t{0});
    request.text = json.value("text", "");
    request.provider = enum_at(json, "provider", providers);
    request.kind = enum_at(json, "kind", kinds);
    request.page = json.value("page", 1);
    request.more = json.value("more", false);
    if (auto it = json.find("link"); it != json.end() && it->is_object()) request.link = link_from(*it);
    request.collection_title = json.value("collectionTitle", "");
    return request;
}

inline Json to_json(const LyricsRequest& request) {
    return {{"serial", request.serial}, {"song", to_json(request.song)}};
}

inline LyricsRequest lyrics_request_from(const Json& json) {
    return {json.value("serial", std::uint64_t{0}), song_from(json.value("song", Json::object()))};
}

inline Json to_json(const ExportJob& job) {
    return {{"song", to_json(job.song)},
            {"index", job.index},
            {"document", job.document ? to_json(*job.document) : Json(nullptr)}};
}

inline ExportJob export_job_from(const Json& json) {
    ExportJob job{song_from(json.value("song", Json::object())), json.value("index", 1), std::nullopt};
    if (auto it = json.find("document"); it != json.end() && it->is_object()) job.document = document_from(*it);
    return job;
}

/// Options travel in the core's own JSON form (options_to_json), so the core keeps its clamping rules.
inline Json to_json(const ExportBatch& batch) {
    return {{"jobs", array_of(batch.jobs, [](const ExportJob& job) { return to_json(job); })},
            {"options", Json::parse(options_to_json(batch.options))}};
}

inline Json to_json(const QrTicket& ticket) {
    return {{"key", ticket.key}, {"url", ticket.url}, {"device", ticket.device}};
}

inline QrTicket ticket_from(const Json& json) {
    return {json.value("key", ""), json.value("url", ""), json.value("device", "")};
}

}  // namespace mu::jni
