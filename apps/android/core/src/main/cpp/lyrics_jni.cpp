// The free functions of lyrics.h, lyric_workspace.h and qr_login.h for Lyrics.kt and the lyric model enums.
#include "jni_support.h"
#include "model_json.h"
#include "musicunlock/lyric_workspace.h"
#include "musicunlock/lyrics.h"
#include "musicunlock/qr_login.h"

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

/// One label table call per enum: which = 0 provider title, 1 provider home, 2 provider id, 3 search kind,
/// 4 format label, 5 format extension, 6 layout, 7 encoding, 8 lyric tab, 9 QR state message.
jstring label(JNIEnv* env, jclass, jint which, jint index) {
    return guarded(env, [&] {
        std::string_view text;
        switch (which) {
            case 0: text = provider_title(nth(providers, index)); break;
            case 1: text = provider_home(nth(providers, index)); break;
            case 2: text = provider_id(nth(providers, index)); break;
            case 3: text = search_kind_label(nth(kinds, index)); break;
            case 4: text = format_label(nth(formats, index)); break;
            case 5: text = format_extension(nth(formats, index)); break;
            case 6: text = layout_label(nth(layouts, index)); break;
            case 7: text = encoding_label(nth(encodings, index)); break;
            case 8: text = lyric_tab_label(nth(tabs, index)); break;
            case 9: text = qr_login_message(nth(qr_states, index)); break;
            default: break;
        }
        return string(env, text);
    });
}

jboolean provider_owns_domain(JNIEnv* env, jclass, jint provider, jstring domain) {
    return guarded(env, [&] { return static_cast<jboolean>(provider_owns(nth(providers, provider), utf8(env, domain))); });
}

jstring normalize_options(JNIEnv* env, jclass, jstring json) {
    return guarded(env, [&] { return string(env, options_to_json(options_from_json(utf8(env, json)))); });
}

jbyteArray encode(JNIEnv* env, jclass, jint encoding, jstring text) {
    return guarded(env, [&] { return byte_array(env, encode_text(nth(encodings, encoding), utf8(env, text))); });
}

jstring parse_lines(JNIEnv* env, jclass, jstring text) {
    return guarded(env, [&] {
        Json lines = Json::array();
        for (const auto& line : parse_lyrics(utf8(env, text))) {
            lines.push_back({{"milliseconds", line.milliseconds ? Json(*line.milliseconds) : Json(nullptr)},
                             {"text", line.text}});
        }
        return string(env, dump(lines));
    });
}

jstring yrc_to_lrc(JNIEnv* env, jclass, jstring yrc) {
    return guarded(env, [&] { return string(env, enhanced_lrc_from_yrc(utf8(env, yrc))); });
}

jstring timestamp(JNIEnv* env, jclass, jlong milliseconds, jint format, jint precision) {
    return guarded(env, [&] { return string(env, lyric_timestamp(milliseconds, nth(formats, format), precision)); });
}

jstring render(JNIEnv* env, jclass, jstring document, jstring options, jlong duration_ms) {
    return guarded(env, [&] {
        return string(env, render_lyrics(document_from(parse(utf8(env, document))),
                                         options_from_json(utf8(env, options)), duration_ms));
    });
}

jstring filename(JNIEnv* env, jclass, jstring song, jstring name_template, jint index) {
    return guarded(env, [&] {
        return string(env, lyric_filename(song_from(parse(utf8(env, song))), utf8(env, name_template), index));
    });
}

jstring parse_link(JNIEnv* env, jclass, jstring input, jint provider, jint kind) {
    return guarded(env, [&]() -> jstring {
        const auto link = MusicLink::parse(utf8(env, input), nth(providers, provider), nth(kinds, kind));
        return link ? string(env, dump(to_json(*link))) : nullptr;
    });
}

jstring duration(JNIEnv* env, jclass, jlong milliseconds) {
    return guarded(env, [&] { return string(env, format_duration(milliseconds)); });
}

jstring qr_code(JNIEnv* env, jclass, jstring text) {
    return guarded(env, [&] {
        const QrMatrix matrix = qr_matrix(utf8(env, text));
        std::string modules;
        modules.reserve(matrix.modules.size());
        for (bool dark : matrix.modules) modules.push_back(dark ? '1' : '0');
        return string(env, dump({{"size", matrix.size}, {"modules", modules}, {"path", qr_path_data(matrix)}}));
    });
}

jint qr_state(JNIEnv*, jclass, jint code) { return index_of(qr_login_state(code), qr_states); }

jstring qr_refusal(JNIEnv* env, jclass, jlong code, jstring message) {
    return guarded(env, [&] { return string(env, qr_refusal_message(code, utf8(env, message))); });
}

#define NATIVE(name, signature, function) {name, signature, reinterpret_cast<void*>(function)}
#define S "Ljava/lang/String;"

const JNINativeMethod methods[] = {
    NATIVE("nativeDecodeLyricFile", "([B)" S, decode_lyric_file),
    NATIVE("nativeMandarinToLatin", "(" S ")" S, mandarin_to_latin),
    NATIVE("nativeLabel", "(II)" S, label),
    NATIVE("nativeProviderOwns", "(I" S ")Z", provider_owns_domain),
    NATIVE("nativeNormalizeOptions", "(" S ")" S, normalize_options),
    NATIVE("nativeEncodeText", "(I" S ")[B", encode),
    NATIVE("nativeParseLyrics", "(" S ")" S, parse_lines),
    NATIVE("nativeEnhancedLrcFromYrc", "(" S ")" S, yrc_to_lrc),
    NATIVE("nativeLyricTimestamp", "(JII)" S, timestamp),
    NATIVE("nativeRenderLyrics", "(" S S "J)" S, render),
    NATIVE("nativeLyricFilename", "(" S S "I)" S, filename),
    NATIVE("nativeParseLink", "(" S "II)" S, parse_link),
    NATIVE("nativeFormatDuration", "(J)" S, duration),
    NATIVE("nativeQrCode", "(" S ")" S, qr_code),
    NATIVE("nativeQrState", "(I)I", qr_state),
    NATIVE("nativeQrRefusal", "(J" S ")" S, qr_refusal),
};

#undef S

}  // namespace

void register_lyrics(JNIEnv* env) { register_natives(env, "Lyrics", methods); }

}  // namespace mu::jni
