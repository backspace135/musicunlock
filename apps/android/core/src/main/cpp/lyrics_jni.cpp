// lyrics.h for Lyrics.kt: local lyric files and the system pinyin.
#include "jni_support.h"
#include "musicunlock/lyrics.h"

namespace mu::jni {

namespace {

jstring decode_lyric_file(JNIEnv* env, jclass, jbyteArray data) {
    return guarded(env, [&] { return string(env, mu::decode_lyric_file(bytes(env, data))); });
}

jstring mandarin_to_latin(JNIEnv* env, jclass, jstring text) {
    return guarded(env, [&]() -> jstring {
        const auto latin = mu::mandarin_to_latin(utf8(env, text));
        return latin ? string(env, *latin) : nullptr;
    });
}

const JNINativeMethod methods[] = {
    {"nativeDecodeLyricFile", "([B)Ljava/lang/String;", reinterpret_cast<void*>(decode_lyric_file)},
    {"nativeMandarinToLatin", "(Ljava/lang/String;)Ljava/lang/String;", reinterpret_cast<void*>(mandarin_to_latin)},
};

}  // namespace

void register_lyrics(JNIEnv* env) { register_natives(env, "Lyrics", methods); }

}  // namespace mu::jni
