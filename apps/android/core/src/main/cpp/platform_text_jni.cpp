// The core's platform text conversion on Android: pinyin through android.icu.text.Transliterator (API 29+) and
// GB18030 through java.nio.charset, both in PlatformText.kt. Replaces src/platform/portable/platform_text_none.cpp,
// which only the core tests link on Android.
#include "detail.h"
#include "jni_support.h"
#include "musicunlock/lyrics.h"

namespace mu {

namespace {

jclass platform_text = nullptr;
jmethodID mandarin_method = nullptr;
jmethodID gb18030_method = nullptr;

std::optional<std::string> result(JNIEnv* env, jstring text) {
    // A failing conversion is "unsupported" for the core, never an exception.
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return std::nullopt;
    }
    if (!text) return std::nullopt;
    std::string value = jni::utf8(env, text);
    env->DeleteLocalRef(text);
    return value;
}

}  // namespace

std::optional<std::string> mandarin_to_latin(std::string_view text) {
    jni::ThreadEnv env;
    jstring input = jni::string(env.get(), text);
    if (!input) return result(env.get(), nullptr);
    auto output = static_cast<jstring>(env->CallStaticObjectMethod(platform_text, mandarin_method, input));
    env->DeleteLocalRef(input);
    return result(env.get(), output);
}

namespace detail {

std::optional<std::string> gb18030_to_utf8(ByteSpan bytes) {
    jni::ThreadEnv env;
    jbyteArray input = jni::byte_array(env.get(), bytes);
    if (!input) return result(env.get(), nullptr);
    auto output = static_cast<jstring>(env->CallStaticObjectMethod(platform_text, gb18030_method, input));
    env->DeleteLocalRef(input);
    return result(env.get(), output);
}

}  // namespace detail

namespace jni {

void register_platform_text(JNIEnv* env) {
    platform_text = find_class(env, "io/github/backspace135/musicunlock/core/PlatformText");
    mandarin_method = env->GetStaticMethodID(platform_text, "mandarinToLatin", "(Ljava/lang/String;)Ljava/lang/String;");
    gb18030_method = env->GetStaticMethodID(platform_text, "gb18030ToUtf8", "([B)Ljava/lang/String;");
    if (!mandarin_method || !gb18030_method) std::abort();
}

}  // namespace jni

}  // namespace mu
