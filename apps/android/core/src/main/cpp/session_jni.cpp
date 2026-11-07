// session.h for AccountStore.kt: cookie rules, and SessionStore sealed by a Kotlin SessionCipher (Android Keystore).
#include <algorithm>

#include "jni_support.h"
#include "model_json.h"
#include "musicunlock/session.h"
#include "musicunlock/text.h"

namespace mu::jni {

namespace {

jclass callbacks = nullptr;
jmethodID seal_method = nullptr;  // CoreCallbacks.seal(SessionCipher, byte[]): byte[]
jmethodID open_method = nullptr;  // CoreCallbacks.open(SessionCipher, byte[]): byte[]?

std::vector<Cookie> cookies(JNIEnv* env, jstring json) { return cookies_from_json(utf8(env, json)); }

/// The Kotlin cipher as the core's SessionCipher. seal failures surface as the Kotlin exception's core form
/// (SessionStore::save reports them); open failures mean "cannot decrypt".
SessionCipher cipher(JNIEnv* env, jobject object) {
    const GlobalRef target(env, object);
    return {
        [target](std::string_view plain) {
            ThreadEnv thread;
            jbyteArray input = byte_array(thread.get(), plain);
            auto output = static_cast<jbyteArray>(thread->CallStaticObjectMethod(callbacks, seal_method, target.get(), input));
            thread->DeleteLocalRef(input);
            check_java(thread.get());
            std::string sealed = byte_string(thread.get(), output);
            thread->DeleteLocalRef(output);
            return sealed;
        },
        [target](std::string_view sealed) -> std::optional<std::string> {
            ThreadEnv thread;
            jbyteArray input = byte_array(thread.get(), sealed);
            auto output = static_cast<jbyteArray>(thread->CallStaticObjectMethod(callbacks, open_method, target.get(), input));
            thread->DeleteLocalRef(input);
            if (thread->ExceptionCheck()) {
                thread->ExceptionClear();
                return std::nullopt;
            }
            if (!output) return std::nullopt;
            std::string plain = byte_string(thread.get(), output);
            thread->DeleteLocalRef(output);
            return plain;
        },
    };
}

SessionStore store(JNIEnv* env, jstring directory, jobject cipher_object) {
    return SessionStore(utf8_to_path(utf8(env, directory)), cipher(env, cipher_object));
}

jstring load(JNIEnv* env, jclass, jstring directory, jint provider, jobject cipher_object) {
    return guarded(env, [&]() -> jstring {
        const auto saved = store(env, directory, cipher_object).load(nth(providers, provider), unix_now());
        return saved ? string(env, cookies_to_json(*saved)) : nullptr;
    });
}

void save(JNIEnv* env, jclass, jstring directory, jint provider, jstring json, jobject cipher_object) {
    guarded(env, [&] { store(env, directory, cipher_object).save(nth(providers, provider), cookies(env, json)); });
}

jboolean remove(JNIEnv* env, jclass, jstring directory, jint provider, jobject cipher_object) {
    return guarded(env, [&] {
        return static_cast<jboolean>(store(env, directory, cipher_object).remove(nth(providers, provider)));
    });
}

jstring file(JNIEnv* env, jclass, jstring directory, jint provider, jobject cipher_object) {
    return guarded(env, [&] {
        return string(env, path_to_utf8(store(env, directory, cipher_object).file(nth(providers, provider))));
    });
}

jstring accept(JNIEnv* env, jclass, jstring json, jint provider) {
    return guarded(env, [&]() -> jstring {
        const auto session = accept_session(cookies(env, json), nth(providers, provider), unix_now());
        return session ? string(env, cookies_to_json(*session)) : nullptr;
    });
}

jboolean has_session(JNIEnv* env, jclass, jstring json, jint provider) {
    return guarded(env, [&] {
        return static_cast<jboolean>(has_music_session(cookies(env, json), nth(providers, provider), unix_now()));
    });
}

jstring header(JNIEnv* env, jclass, jstring json) {
    return guarded(env, [&] { return string(env, cookie_header(cookies(env, json), unix_now())); });
}

jstring parse_set_cookie_headers(JNIEnv* env, jclass, jobjectArray headers, jstring host) {
    return guarded(env, [&] {
        std::vector<std::string> values;
        const jsize count = env->GetArrayLength(headers);
        for (jsize i = 0; i < count; ++i) {
            auto value = static_cast<jstring>(env->GetObjectArrayElement(headers, i));
            values.push_back(utf8(env, value));
            env->DeleteLocalRef(value);
        }
        return string(env, cookies_to_json(parse_set_cookies(values, utf8(env, host), unix_now())));
    });
}

/// "a=1; b=2" as CookieManager.getCookie returns it: no attributes, so each pair is a cookie of `host`, path "/",
/// without expiry (parse_set_cookies' defaults for a header without attributes).
jstring from_cookie_header(JNIEnv* env, jclass, jstring header, jstring host) {
    return guarded(env, [&] {
        const std::string text = utf8(env, header);
        std::vector<std::string> pairs;
        for (std::size_t start = 0; start <= text.size();) {
            const std::size_t end = std::min(text.find(';', start), text.size());
            pairs.push_back(text.substr(start, end - start));
            start = end + 1;
        }
        return string(env, cookies_to_json(parse_set_cookies(pairs, utf8(env, host), unix_now())));
    });
}

jlong now(JNIEnv*, jclass) { return unix_now(); }

#define NATIVE(name, signature, function) {name, signature, reinterpret_cast<void*>(function)}
#define S "Ljava/lang/String;"
#define CIPHER "Lio/github/backspace135/musicunlock/core/SessionCipher;"

const JNINativeMethod methods[] = {
    NATIVE("nativeLoad", "(" S "I" CIPHER ")" S, load),
    NATIVE("nativeSave", "(" S "I" S CIPHER ")V", save),
    NATIVE("nativeRemove", "(" S "I" CIPHER ")Z", remove),
    NATIVE("nativeFile", "(" S "I" CIPHER ")" S, file),
    NATIVE("nativeAccept", "(" S "I)" S, accept),
    NATIVE("nativeHasSession", "(" S "I)Z", has_session),
    NATIVE("nativeCookieHeader", "(" S ")" S, header),
    NATIVE("nativeParseSetCookies", "([" S S ")" S, parse_set_cookie_headers),
    NATIVE("nativeCookiesFromHeader", "(" S S ")" S, from_cookie_header),
    NATIVE("nativeUnixNow", "()J", now),
};

#undef CIPHER
#undef S

}  // namespace

void register_session(JNIEnv* env) {
    register_natives(env, "Sessions", methods);
    callbacks = find_class(env, "io/github/backspace135/musicunlock/core/CoreCallbacks");
    seal_method = env->GetStaticMethodID(callbacks, "seal", "(Lio/github/backspace135/musicunlock/core/SessionCipher;[B)[B");
    open_method = env->GetStaticMethodID(callbacks, "open", "(Lio/github/backspace135/musicunlock/core/SessionCipher;[B)[B");
    if (!seal_method || !open_method) std::abort();
}

}  // namespace mu::jni
