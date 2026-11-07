#include "jni_support.h"

#include <android/log.h>

#include <cstdlib>
#include <new>

#include "musicunlock/lyrics.h"
#include "musicunlock/music_api.h"
#include "musicunlock/text.h"

namespace mu::jni {

namespace {

JavaVM* java_vm = nullptr;
Classes cached{};

constexpr const char* package = "io/github/backspace135/musicunlock/core/";

std::string class_path(const char* name) { return std::string(package) + name; }

jmethodID method(JNIEnv* env, jclass type, const char* name, const char* signature) {
    jmethodID id = env->GetMethodID(type, name, signature);
    if (!id) {
        __android_log_print(ANDROID_LOG_FATAL, "musicunlock", "missing method %s%s", name, signature);
        std::abort();
    }
    return id;
}

/// Raises `type` with a UTF-8 message through its (String) constructor.
void throw_with_message(JNIEnv* env, jclass type, jmethodID init, std::string_view message) {
    jstring text = string(env, message);
    if (!text) return;  // OutOfMemoryError already pending
    auto exception = static_cast<jthrowable>(env->NewObject(type, init, text));
    if (exception) env->Throw(exception);
}

std::string java_message(JNIEnv* env, jthrowable throwable) {
    auto message = static_cast<jstring>(env->CallObjectMethod(throwable, cached.throwable_message));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return {};
    }
    std::string text = utf8(env, message);
    if (message) env->DeleteLocalRef(message);
    return text;
}

}  // namespace

JavaVM* vm() { return java_vm; }

ThreadEnv::ThreadEnv() {
    const jint status = java_vm->GetEnv(reinterpret_cast<void**>(&env_), JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        JavaVMAttachArgs args{JNI_VERSION_1_6, "musicunlock-native", nullptr};
        if (java_vm->AttachCurrentThread(&env_, &args) != JNI_OK) {
            __android_log_print(ANDROID_LOG_FATAL, "musicunlock", "cannot attach a native thread to the JVM");
            std::abort();
        }
        attached_ = true;
    }
}

ThreadEnv::~ThreadEnv() {
    if (attached_) java_vm->DetachCurrentThread();
}

GlobalRef::GlobalRef(JNIEnv* env, jobject object)
    : ref_(object ? env->NewGlobalRef(object) : nullptr, [](jobject ref) {
          if (!ref) return;
          ThreadEnv env;
          env->DeleteGlobalRef(ref);
      }) {}

const Classes& classes() { return cached; }

jclass find_class(JNIEnv* env, const char* name) {
    jclass local = env->FindClass(name);
    if (!local) {
        __android_log_print(ANDROID_LOG_FATAL, "musicunlock", "missing class %s", name);
        std::abort();
    }
    auto global = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    return global;
}

std::string utf8(JNIEnv* env, jstring text) {
    if (!text) return {};
    const jsize length = env->GetStringLength(text);
    std::u16string units(static_cast<std::size_t>(length), u'\0');
    env->GetStringRegion(text, 0, length, reinterpret_cast<jchar*>(units.data()));
    return utf16_to_utf8(units);
}

jstring string(JNIEnv* env, std::string_view text) {
    // NewStringUTF expects modified UTF-8, which differs for NUL and characters outside the BMP (emoji).
    const std::u16string units = utf8_to_utf16(text);
    return env->NewString(reinterpret_cast<const jchar*>(units.data()), static_cast<jsize>(units.size()));
}

Bytes bytes(JNIEnv* env, jbyteArray array) {
    if (!array) return {};
    Bytes out(static_cast<std::size_t>(env->GetArrayLength(array)));
    env->GetByteArrayRegion(array, 0, static_cast<jsize>(out.size()), reinterpret_cast<jbyte*>(out.data()));
    return out;
}

std::string byte_string(JNIEnv* env, jbyteArray array) {
    if (!array) return {};
    std::string out(static_cast<std::size_t>(env->GetArrayLength(array)), '\0');
    env->GetByteArrayRegion(array, 0, static_cast<jsize>(out.size()), reinterpret_cast<jbyte*>(out.data()));
    return out;
}

jbyteArray byte_array(JNIEnv* env, const void* data, std::size_t size) {
    jbyteArray array = env->NewByteArray(static_cast<jsize>(size));
    if (array && size > 0) {
        env->SetByteArrayRegion(array, 0, static_cast<jsize>(size), static_cast<const jbyte*>(data));
    }
    return array;
}

void check_java(JNIEnv* env) {
    jthrowable throwable = env->ExceptionOccurred();
    if (!throwable) return;
    env->ExceptionClear();
    if (env->IsInstanceOf(throwable, cached.cancellation)) {
        env->DeleteLocalRef(throwable);
        throw Cancelled();
    }
    const std::string message = java_message(env, throwable);
    if (env->IsInstanceOf(throwable, cached.lyric_exception)) {
        env->DeleteLocalRef(throwable);
        throw LyricError(message);
    }
    if (env->IsInstanceOf(throwable, cached.decode_exception)) {
        const jint kind = env->CallIntMethod(throwable, cached.decode_exception_kind);
        env->ExceptionClear();
        env->DeleteLocalRef(throwable);
        throw DecodeError(kind == 1 ? DecodeError::Kind::unsupported : DecodeError::Kind::invalid, message);
    }
    GlobalRef original(env, throwable);
    env->DeleteLocalRef(throwable);
    throw JavaError(std::move(original), message);
}

void throw_current(JNIEnv* env) {
    if (env->ExceptionCheck()) return;  // a JNI call already raised one (e.g. OutOfMemoryError)
    try {
        throw;
    } catch (const JavaError& error) {
        env->Throw(static_cast<jthrowable>(error.throwable()));
    } catch (const Cancelled&) {
        auto exception = static_cast<jthrowable>(env->NewObject(cached.cancelled_exception,
                                                                cached.cancelled_exception_init));
        if (exception) env->Throw(exception);
    } catch (const DecodeError& error) {
        jstring text = string(env, repair_utf8(error.what()));
        if (!text) return;
        const jint kind = error.kind() == DecodeError::Kind::unsupported ? 1 : 0;
        auto exception = static_cast<jthrowable>(
            env->NewObject(cached.decode_exception, cached.decode_exception_init, kind, text));
        if (exception) env->Throw(exception);
    } catch (const LyricError& error) {
        throw_with_message(env, cached.lyric_exception, cached.lyric_exception_init, repair_utf8(error.what()));
    } catch (const std::bad_alloc&) {
        env->ThrowNew(cached.out_of_memory, "native allocation failed");
    } catch (const std::exception& error) {
        throw_with_message(env, cached.core_exception, cached.core_exception_init, repair_utf8(error.what()));
    } catch (...) {
        throw_with_message(env, cached.core_exception, cached.core_exception_init, "未知错误");
    }
}

void register_natives(JNIEnv* env, const char* class_name, const JNINativeMethod* methods, std::size_t count) {
    const std::string name = class_path(class_name);
    jclass type = env->FindClass(name.c_str());
    if (!type || env->RegisterNatives(type, methods, static_cast<jint>(count)) != JNI_OK) {
        __android_log_print(ANDROID_LOG_FATAL, "musicunlock", "cannot register the natives of %s", name.c_str());
        std::abort();
    }
    env->DeleteLocalRef(type);
}

void load(JavaVM* vm, JNIEnv* env) {
    java_vm = vm;
    cached.string = find_class(env, "java/lang/String");
    cached.core_exception = find_class(env, class_path("CoreException").c_str());
    cached.decode_exception = find_class(env, class_path("DecodeException").c_str());
    cached.lyric_exception = find_class(env, class_path("LyricException").c_str());
    cached.cancelled_exception = find_class(env, class_path("CoreCancelledException").c_str());
    cached.cancellation = find_class(env, "java/util/concurrent/CancellationException");
    cached.out_of_memory = find_class(env, "java/lang/OutOfMemoryError");
    cached.core_exception_init = method(env, cached.core_exception, "<init>", "(Ljava/lang/String;)V");
    cached.decode_exception_init = method(env, cached.decode_exception, "<init>", "(ILjava/lang/String;)V");
    cached.lyric_exception_init = method(env, cached.lyric_exception, "<init>", "(Ljava/lang/String;)V");
    cached.cancelled_exception_init = method(env, cached.cancelled_exception, "<init>", "()V");
    cached.decode_exception_kind = method(env, cached.decode_exception, "kindCode", "()I");
    jclass throwable = env->FindClass("java/lang/Throwable");
    cached.throwable_message = method(env, throwable, "getMessage", "()Ljava/lang/String;");
    env->DeleteLocalRef(throwable);
}

}  // namespace mu::jni
