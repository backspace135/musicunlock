// lyric_workspace.h's LyricWorkspace for LyricWorkspace.kt. The workspace is owned by one thread (Kotlin checks);
// requests and results cross as JSON (model_json.h).
#include "jni_support.h"
#include "model_json.h"
#include "musicunlock/lyric_workspace.h"
#include "musicunlock/text.h"

namespace mu::jni {

namespace {

LyricWorkspace& space(jlong handle) { return jni::handle<LyricWorkspace>(handle); }

template <class T>
jstring optional_json(JNIEnv* env, const std::optional<T>& value) {
    return value ? string(env, dump(to_json(*value))) : nullptr;
}

jlong create(JNIEnv* env, jclass) {
    return guarded(env, [&] { return make_handle(new LyricWorkspace()); });
}

void destroy(JNIEnv*, jclass, jlong handle) { delete &space(handle); }

/// provider, kind, searching, has_more, loading_lyrics, exporting, stop_requested, progress, export_total,
/// checked_count, line_count, can_retry, can_export_current, all_checked: the flags a screen needs, in one call.
jlongArray state(JNIEnv* env, jclass, jlong handle) {
    return guarded(env, [&] {
        const LyricWorkspace& w = space(handle);
        const jlong values[] = {index_of(w.provider(), providers), index_of(w.kind(), kinds), w.searching(),
                                w.has_more(), w.loading_lyrics(), w.exporting(), w.stop_requested(),
                                static_cast<jlong>(w.progress()), static_cast<jlong>(w.export_total()),
                                static_cast<jlong>(w.checked_count()), static_cast<jlong>(w.line_count()),
                                w.can_retry(), w.can_export_current(), w.all_checked()};
        constexpr jsize count = sizeof values / sizeof values[0];
        jlongArray array = env->NewLongArray(count);
        if (array) env->SetLongArrayRegion(array, 0, count, values);
        return array;
    });
}

/// Every text and list the screen shows, as one JSON object.
jstring texts(JNIEnv* env, jclass, jlong handle) {
    return guarded(env, [&] {
        const LyricWorkspace& w = space(handle);
        Json exported = Json::array();
        for (const auto& path : w.exported()) exported.push_back(path_to_utf8(path));
        const auto error = w.preview_error();
        return string(env, dump({{"query", w.query()},
                                 {"songs", array_of(w.songs(), [](const LyricSong& s) { return to_json(s); })},
                                 {"collections",
                                  array_of(w.collections(), [](const MusicCollection& c) { return to_json(c); })},
                                 {"selected", w.selected() ? to_json(*w.selected()) : Json(nullptr)},
                                 {"document", to_json(w.document())},
                                 {"options", Json::parse(options_to_json(w.options()))},
                                 {"preview", w.preview()},
                                 {"previewError", error ? Json(*error) : Json(nullptr)},
                                 {"failures", w.failures()},
                                 {"exported", exported},
                                 {"notice", w.notice()},
                                 {"resultsTitle", w.results_title()},
                                 {"resultsSubtitle", w.results_subtitle()},
                                 {"loadingText", w.loading_text()},
                                 {"progressText", w.progress_text()},
                                 {"failuresTitle", w.failures_title()},
                                 {"exportCheckedLabel", w.export_checked_label()}}));
    });
}

jboolean is_checked(JNIEnv* env, jclass, jlong handle, jstring id) {
    return guarded(env, [&] { return static_cast<jboolean>(space(handle).is_checked(utf8(env, id))); });
}

void set_provider(JNIEnv* env, jclass, jlong handle, jint provider) {
    guarded(env, [&] { space(handle).set_provider(nth(providers, provider)); });
}

void set_kind(JNIEnv* env, jclass, jlong handle, jint kind) {
    guarded(env, [&] { space(handle).set_kind(nth(kinds, kind)); });
}

void set_query(JNIEnv* env, jclass, jlong handle, jstring query) {
    guarded(env, [&] { space(handle).set_query(utf8(env, query)); });
}

jstring search(JNIEnv* env, jclass, jlong handle, jboolean more) {
    return guarded(env, [&] { return optional_json(env, space(handle).search(more)); });
}

jstring open(JNIEnv* env, jclass, jlong handle, jstring collection) {
    return guarded(env, [&] { return optional_json(env, space(handle).open(collection_from(parse(utf8(env, collection))))); });
}

jstring search_for_audio(JNIEnv* env, jclass, jlong handle, jstring title) {
    return guarded(env, [&] { return optional_json(env, space(handle).search_for_audio(utf8(env, title))); });
}

jstring search_finished(JNIEnv* env, jclass, jlong handle, jstring request, jstring result) {
    return guarded(env, [&] {
        return optional_json(env, space(handle).search_finished(search_request_from(parse(utf8(env, request))),
                                                                search_result_from(parse(utf8(env, result)))));
    });
}

void search_failed(JNIEnv* env, jclass, jlong handle, jstring request, jstring message) {
    guarded(env, [&] {
        space(handle).search_failed(search_request_from(parse(utf8(env, request))), utf8(env, message));
    });
}

void cancel_search(JNIEnv* env, jclass, jlong handle) {
    guarded(env, [&] { space(handle).cancel_search(); });
}

jstring select(JNIEnv* env, jclass, jlong handle, jstring id, jboolean reload) {
    return guarded(env, [&] { return optional_json(env, space(handle).select(utf8(env, id), reload)); });
}

jstring retry_lyrics(JNIEnv* env, jclass, jlong handle) {
    return guarded(env, [&] { return optional_json(env, space(handle).retry_lyrics()); });
}

void lyrics_loaded(JNIEnv* env, jclass, jlong handle, jstring request, jstring document) {
    guarded(env, [&] {
        space(handle).lyrics_loaded(lyrics_request_from(parse(utf8(env, request))),
                                    document_from(parse(utf8(env, document))));
    });
}

void lyrics_failed(JNIEnv* env, jclass, jlong handle, jstring request, jstring message) {
    guarded(env, [&] {
        space(handle).lyrics_failed(lyrics_request_from(parse(utf8(env, request))), utf8(env, message));
    });
}

void toggle(JNIEnv* env, jclass, jlong handle, jstring id) {
    guarded(env, [&] { space(handle).toggle(utf8(env, id)); });
}

void select_all(JNIEnv* env, jclass, jlong handle) {
    guarded(env, [&] { space(handle).select_all(); });
}

jboolean import_local(JNIEnv* env, jclass, jlong handle, jstring title, jstring text) {
    return guarded(env, [&] {
        return static_cast<jboolean>(space(handle).import_local(utf8(env, title), utf8(env, text)));
    });
}

jstring text(JNIEnv* env, jclass, jlong handle, jint tab) {
    return guarded(env, [&] { return string(env, space(handle).text(nth(tabs, tab))); });
}

void edit(JNIEnv* env, jclass, jlong handle, jint tab, jstring text) {
    guarded(env, [&] { space(handle).edit(nth(tabs, tab), utf8(env, text)); });
}

void set_options(JNIEnv* env, jclass, jlong handle, jstring options) {
    guarded(env, [&] { space(handle).set_options(options_from_json(utf8(env, options))); });
}

void set_output_directory(JNIEnv* env, jclass, jlong handle, jstring directory) {
    guarded(env, [&] { space(handle).set_output_directory(utf8_to_path(utf8(env, directory))); });
}

jstring copy_preview(JNIEnv* env, jclass, jlong handle) {
    return guarded(env, [&] { return string(env, space(handle).copy_preview()); });
}

jstring export_current(JNIEnv* env, jclass, jlong handle) {
    return guarded(env, [&] { return optional_json(env, space(handle).export_current()); });
}

jstring export_checked(JNIEnv* env, jclass, jlong handle) {
    return guarded(env, [&] { return optional_json(env, space(handle).export_checked()); });
}

void export_succeeded(JNIEnv* env, jclass, jlong handle, jstring job, jstring document, jstring file) {
    guarded(env, [&] {
        ExportOutcome outcome{document_from(parse(utf8(env, document))), std::nullopt};
        if (file) outcome.file = utf8_to_path(utf8(env, file));
        space(handle).export_succeeded(export_job_from(parse(utf8(env, job))), outcome);
    });
}

void export_failed(JNIEnv* env, jclass, jlong handle, jstring job, jstring message) {
    guarded(env, [&] { space(handle).export_failed(export_job_from(parse(utf8(env, job))), utf8(env, message)); });
}

void request_stop(JNIEnv* env, jclass, jlong handle) {
    guarded(env, [&] { space(handle).request_stop(); });
}

void end_export(JNIEnv* env, jclass, jlong handle) {
    guarded(env, [&] { space(handle).end_export(); });
}

void show_notice(JNIEnv* env, jclass, jlong handle, jstring text) {
    guarded(env, [&] { space(handle).show_notice(utf8(env, text)); });
}

void dismiss_notice(JNIEnv* env, jclass, jlong handle) {
    guarded(env, [&] { space(handle).dismiss_notice(); });
}

jstring song_artist(JNIEnv* env, jclass, jstring song) {
    return guarded(env, [&] { return string(env, LyricWorkspace::song_artist(song_from(parse(utf8(env, song))))); });
}

jstring song_detail(JNIEnv* env, jclass, jstring song) {
    return guarded(env, [&] { return string(env, LyricWorkspace::song_detail(song_from(parse(utf8(env, song))))); });
}

#define NATIVE(name, signature, function) {name, signature, reinterpret_cast<void*>(function)}
#define S "Ljava/lang/String;"

const JNINativeMethod methods[] = {
    NATIVE("nativeCreate", "()J", create),
    NATIVE("nativeDestroy", "(J)V", destroy),
    NATIVE("nativeState", "(J)[J", state),
    NATIVE("nativeTexts", "(J)" S, texts),
    NATIVE("nativeIsChecked", "(J" S ")Z", is_checked),
    NATIVE("nativeSetProvider", "(JI)V", set_provider),
    NATIVE("nativeSetKind", "(JI)V", set_kind),
    NATIVE("nativeSetQuery", "(J" S ")V", set_query),
    NATIVE("nativeSearch", "(JZ)" S, search),
    NATIVE("nativeOpen", "(J" S ")" S, open),
    NATIVE("nativeSearchForAudio", "(J" S ")" S, search_for_audio),
    NATIVE("nativeSearchFinished", "(J" S S ")" S, search_finished),
    NATIVE("nativeSearchFailed", "(J" S S ")V", search_failed),
    NATIVE("nativeCancelSearch", "(J)V", cancel_search),
    NATIVE("nativeSelect", "(J" S "Z)" S, select),
    NATIVE("nativeRetryLyrics", "(J)" S, retry_lyrics),
    NATIVE("nativeLyricsLoaded", "(J" S S ")V", lyrics_loaded),
    NATIVE("nativeLyricsFailed", "(J" S S ")V", lyrics_failed),
    NATIVE("nativeToggle", "(J" S ")V", toggle),
    NATIVE("nativeSelectAll", "(J)V", select_all),
    NATIVE("nativeImportLocal", "(J" S S ")Z", import_local),
    NATIVE("nativeText", "(JI)" S, text),
    NATIVE("nativeEdit", "(JI" S ")V", edit),
    NATIVE("nativeSetOptions", "(J" S ")V", set_options),
    NATIVE("nativeSetOutputDirectory", "(J" S ")V", set_output_directory),
    NATIVE("nativeCopyPreview", "(J)" S, copy_preview),
    NATIVE("nativeExportCurrent", "(J)" S, export_current),
    NATIVE("nativeExportChecked", "(J)" S, export_checked),
    NATIVE("nativeExportSucceeded", "(J" S S S ")V", export_succeeded),
    NATIVE("nativeExportFailed", "(J" S S ")V", export_failed),
    NATIVE("nativeRequestStop", "(J)V", request_stop),
    NATIVE("nativeEndExport", "(J)V", end_export),
    NATIVE("nativeShowNotice", "(J" S ")V", show_notice),
    NATIVE("nativeDismissNotice", "(J)V", dismiss_notice),
    NATIVE("nativeSongArtist", "(" S ")" S, song_artist),
    NATIVE("nativeSongDetail", "(" S ")" S, song_detail),
};

#undef S

}  // namespace

void register_workspace(JNIEnv* env) { register_natives(env, "LyricWorkspace", methods); }

}  // namespace mu::jni
