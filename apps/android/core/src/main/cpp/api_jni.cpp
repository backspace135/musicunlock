// music_api.h and the background half of lyric_workspace.h for MusicApi.kt and CancelToken.kt.
// Requests go through a Kotlin HttpTransport (OkHttp in the app), called back synchronously on the thread that runs
// the core, which ThreadEnv attaches to the JVM when C++ started it.
#include <thread>

#include "jni_support.h"
#include "model_json.h"
#include "musicunlock/lyric_workspace.h"
#include "musicunlock/music_api.h"
#include "musicunlock/text.h"

namespace mu::jni {

namespace {

jclass callbacks = nullptr;
jmethodID http_method = nullptr;       // CoreCallbacks.http(HttpTransport, String, String, String[], byte[])
jfieldID result_status = nullptr;      // NativeHttpResult.status
jfieldID result_body = nullptr;        // NativeHttpResult.body
jfieldID result_cookies = nullptr;     // NativeHttpResult.setCookies

/// mu::HttpTransport backed by a Kotlin HttpTransport.
class KotlinTransport {
public:
    KotlinTransport(JNIEnv* env, jobject transport) : transport_(env, transport) {}

    HttpResponse operator()(const HttpRequest& request) const {
        ThreadEnv env;
        jobjectArray headers = env->NewObjectArray(static_cast<jsize>(request.headers.size() * 2), classes().string, nullptr);
        jstring url = string(env.get(), request.url);
        jstring method = string(env.get(), request.method);
        jbyteArray body = byte_array(env.get(), request.body);
        check_java(env.get());
        for (std::size_t i = 0; i < request.headers.size(); ++i) {
            jstring name = string(env.get(), request.headers[i].first);
            jstring value = string(env.get(), request.headers[i].second);
            env->SetObjectArrayElement(headers, static_cast<jsize>(i * 2), name);
            env->SetObjectArrayElement(headers, static_cast<jsize>(i * 2 + 1), value);
            env->DeleteLocalRef(name);
            env->DeleteLocalRef(value);
        }
        jobject result = env->CallStaticObjectMethod(callbacks, http_method, transport_.get(), url, method, headers, body);
        env->DeleteLocalRef(url);
        env->DeleteLocalRef(method);
        env->DeleteLocalRef(headers);
        env->DeleteLocalRef(body);
        check_java(env.get());
        HttpResponse response;
        response.status = env->GetIntField(result, result_status);
        auto data = static_cast<jbyteArray>(env->GetObjectField(result, result_body));
        response.body = byte_string(env.get(), data);
        env->DeleteLocalRef(data);
        auto cookies = static_cast<jobjectArray>(env->GetObjectField(result, result_cookies));
        const jsize count = cookies ? env->GetArrayLength(cookies) : 0;
        for (jsize i = 0; i < count; ++i) {
            auto cookie = static_cast<jstring>(env->GetObjectArrayElement(cookies, i));
            response.set_cookies.push_back(utf8(env.get(), cookie));
            env->DeleteLocalRef(cookie);
        }
        if (cookies) env->DeleteLocalRef(cookies);
        env->DeleteLocalRef(result);
        return response;
    }

private:
    GlobalRef transport_;
};

const MusicApi& api(jlong handle) { return jni::handle<MusicApi>(handle); }
const CancelToken& token(jlong handle) { return jni::handle<CancelToken>(handle); }
MusicProvider provider_at(jint index) { return nth(providers, index); }

// CancelToken: one native token per request, shared with the Kotlin CancelToken while the request runs.

jlong token_create(JNIEnv* env, jclass) {
    return guarded(env, [&] { return make_handle(new CancelToken()); });
}

void token_cancel(JNIEnv*, jclass, jlong handle) { token(handle).cancel(); }

void token_destroy(JNIEnv*, jclass, jlong handle) { delete &jni::handle<CancelToken>(handle); }

// MusicApi

jlong api_create(JNIEnv* env, jclass, jobject transport) {
    return guarded(env, [&] { return make_handle(new MusicApi(KotlinTransport(env, transport))); });
}

void api_destroy(JNIEnv*, jclass, jlong handle) { delete &jni::handle<MusicApi>(handle); }

jstring api_search(JNIEnv* env, jclass, jlong handle, jstring query, jint provider, jint kind, jint page,
                   jstring cookie, jlong cancel) {
    return guarded(env, [&] {
        const SearchPage result = api(handle).search(utf8(env, query), provider_at(provider),
                                                     nth(kinds, kind), page, utf8(env, cookie),
                                                     token(cancel));
        return string(env, dump(to_json(SearchResult{result.songs, result.collections, result.has_more})));
    });
}

jstring api_resolve(JNIEnv* env, jclass, jlong handle, jstring link, jstring cookie, jlong cancel) {
    return guarded(env, [&] {
        const auto songs = api(handle).resolve(link_from(parse(utf8(env, link))), utf8(env, cookie), token(cancel));
        return string(env, dump(array_of(songs, [](const LyricSong& song) { return to_json(song); })));
    });
}

jstring api_lyrics(JNIEnv* env, jclass, jlong handle, jstring song, jstring cookie, jlong cancel) {
    return guarded(env, [&] {
        const auto document = api(handle).lyrics(song_from(parse(utf8(env, song))), utf8(env, cookie), token(cancel));
        return string(env, dump(to_json(document)));
    });
}

jstring api_run_search(JNIEnv* env, jclass, jlong handle, jstring request, jstring cookie, jlong cancel) {
    return guarded(env, [&] {
        const auto result = run_search(api(handle), search_request_from(parse(utf8(env, request))), utf8(env, cookie),
                                       token(cancel));
        return string(env, dump(to_json(result)));
    });
}

jstring api_export_song(JNIEnv* env, jclass, jlong handle, jstring job, jstring options, jstring directory,
                        jstring cookie, jlong cancel) {
    return guarded(env, [&] {
        const auto outcome = export_song(api(handle), export_job_from(parse(utf8(env, job))),
                                         options_from_json(utf8(env, options)), utf8_to_path(utf8(env, directory)),
                                         utf8(env, cookie), token(cancel));
        return string(env, dump({{"document", to_json(outcome.document)},
                                 {"file", outcome.file ? Json(path_to_utf8(*outcome.file)) : Json(nullptr)}}));
    });
}

jstring api_account_name(JNIEnv* env, jclass, jlong handle, jint provider, jstring cookie) {
    return guarded(env, [&]() -> jstring {
        const auto name = api(handle).account_name(provider_at(provider), utf8(env, cookie));
        return name ? string(env, *name) : nullptr;
    });
}

/// account_name on a thread C++ starts, so the transport's callback runs on a thread the JVM does not know yet.
/// Only for the tests of ThreadEnv.
jstring api_account_name_on_native_thread(JNIEnv* env, jclass, jlong handle, jint provider, jstring cookie) {
    return guarded(env, [&]() -> jstring {
        const std::string text = utf8(env, cookie);
        std::optional<std::string> name;
        std::exception_ptr failure;
        std::thread([&] {
            try {
                name = api(handle).account_name(provider_at(provider), text);
            } catch (...) {
                failure = std::current_exception();
            }
        }).join();
        if (failure) std::rethrow_exception(failure);
        return name ? string(env, *name) : nullptr;
    });
}

jlong qq_token(JNIEnv* env, jclass, jstring cookie) {
    return guarded(env, [&] { return static_cast<jlong>(MusicApi::qq_token(utf8(env, cookie))); });
}

jstring decode_entities(JNIEnv* env, jclass, jstring text) {
    return guarded(env, [&] { return string(env, MusicApi::decode_entities(utf8(env, text))); });
}

#define NATIVE(name, signature, function) {name, signature, reinterpret_cast<void*>(function)}

const JNINativeMethod token_methods[] = {
    NATIVE("nativeCreate", "()J", token_create),
    NATIVE("nativeCancel", "(J)V", token_cancel),
    NATIVE("nativeDestroy", "(J)V", token_destroy),
};

#define S "Ljava/lang/String;"

const JNINativeMethod api_methods[] = {
    NATIVE("nativeCreate", "(Lio/github/backspace135/musicunlock/core/HttpTransport;)J", api_create),
    NATIVE("nativeDestroy", "(J)V", api_destroy),
    NATIVE("nativeSearch", "(J" S "III" S "J)" S, api_search),
    NATIVE("nativeResolve", "(J" S S "J)" S, api_resolve),
    NATIVE("nativeLyrics", "(J" S S "J)" S, api_lyrics),
    NATIVE("nativeRunSearch", "(J" S S "J)" S, api_run_search),
    NATIVE("nativeExportSong", "(J" S S S S "J)" S, api_export_song),
    NATIVE("nativeAccountName", "(JI" S ")" S, api_account_name),
    NATIVE("nativeAccountNameOnNativeThread", "(JI" S ")" S, api_account_name_on_native_thread),
    NATIVE("nativeQqToken", "(" S ")J", qq_token),
    NATIVE("nativeDecodeEntities", "(" S ")" S, decode_entities),
};

#undef S

}  // namespace

void register_api(JNIEnv* env) {
    register_natives(env, "CancelToken", token_methods);
    register_natives(env, "MusicApi", api_methods);
    callbacks = find_class(env, "io/github/backspace135/musicunlock/core/CoreCallbacks");
    http_method = env->GetStaticMethodID(
        callbacks, "http",
        "(Lio/github/backspace135/musicunlock/core/HttpTransport;Ljava/lang/String;Ljava/lang/String;"
        "[Ljava/lang/String;[B)Lio/github/backspace135/musicunlock/core/NativeHttpResult;");
    jclass result = env->FindClass("io/github/backspace135/musicunlock/core/NativeHttpResult");
    result_status = result ? env->GetFieldID(result, "status", "I") : nullptr;
    result_body = result ? env->GetFieldID(result, "body", "[B") : nullptr;
    result_cookies = result ? env->GetFieldID(result, "setCookies", "[Ljava/lang/String;") : nullptr;
    if (!http_method || !result_status || !result_body || !result_cookies) std::abort();
    env->DeleteLocalRef(result);
}

}  // namespace mu::jni
