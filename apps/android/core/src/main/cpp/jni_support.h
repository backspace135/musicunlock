#pragma once
// Plumbing shared by the JNI bridge: the JavaVM, thread attachment, UTF-8 <-> Java strings, byte arrays, and the
// exception mapping in both directions. No C++ exception crosses into Java: every entry point runs its body in
// `guarded`. No Java exception unwinds C++ frames: every call into Kotlin is followed by `check_java`.
#include <jni.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

#include "musicunlock/audio.h"

namespace mu::jni {

/// Caches the VM and the classes below. Called first from JNI_OnLoad.
void load(JavaVM* vm, JNIEnv* env);
JavaVM* vm();

/// The calling thread's JNIEnv. A thread the JVM does not know yet (started by native code) is attached for the
/// lifetime of the scope and detached when it ends; threads that came from Java are left as they are.
class ThreadEnv {
public:
    ThreadEnv();
    ~ThreadEnv();
    ThreadEnv(const ThreadEnv&) = delete;
    ThreadEnv& operator=(const ThreadEnv&) = delete;

    JNIEnv* get() const { return env_; }
    JNIEnv* operator->() const { return env_; }

private:
    JNIEnv* env_ = nullptr;
    bool attached_ = false;
};

/// A JNI global reference owned by C++. Copies share it; the last one deletes it from whichever thread it is on.
class GlobalRef {
public:
    GlobalRef() = default;
    GlobalRef(JNIEnv* env, jobject object);
    jobject get() const { return ref_.get(); }
    explicit operator bool() const { return ref_ != nullptr; }

private:
    std::shared_ptr<_jobject> ref_;
};

/// Classes and members looked up once in JNI_OnLoad: FindClass on a thread attached from native code only sees
/// system classes, so app classes must be resolved while the app's class loader is on the stack.
struct Classes {
    jclass string;
    jclass core_exception;
    jclass decode_exception;
    jclass lyric_exception;
    jclass cancelled_exception;
    jclass cancellation;  // java.util.concurrent.CancellationException, the base of Kotlin's
    jclass out_of_memory;
    jmethodID core_exception_init;      // (String)
    jmethodID decode_exception_init;    // (int, String)
    jmethodID lyric_exception_init;     // (String)
    jmethodID cancelled_exception_init; // ()
    jmethodID decode_exception_kind;    // ()int
    jmethodID throwable_message;        // ()String
};
const Classes& classes();

/// Resolves a class with the app's class loader, as a global reference. Only valid during JNI_OnLoad.
jclass find_class(JNIEnv* env, const char* name);

std::string utf8(JNIEnv* env, jstring text);  // null becomes ""
jstring string(JNIEnv* env, std::string_view utf8);
Bytes bytes(JNIEnv* env, jbyteArray array);  // null becomes empty
std::string byte_string(JNIEnv* env, jbyteArray array);
jbyteArray byte_array(JNIEnv* env, const void* data, std::size_t size);
inline jbyteArray byte_array(JNIEnv* env, ByteSpan data) { return byte_array(env, data.data(), data.size()); }
inline jbyteArray byte_array(JNIEnv* env, std::string_view data) { return byte_array(env, data.data(), data.size()); }

/// A Java exception thrown by a Kotlin callback that has no core equivalent. `guarded` rethrows the original.
class JavaError : public std::runtime_error {
public:
    JavaError(GlobalRef throwable, const std::string& message) : std::runtime_error(message), throwable_(std::move(throwable)) {}
    jobject throwable() const { return throwable_.get(); }

private:
    GlobalRef throwable_;
};

/// After a call into Kotlin: clears a pending exception and throws its core equivalent (CancellationException
/// becomes Cancelled, LyricException LyricError, DecodeException DecodeError; anything else JavaError).
void check_java(JNIEnv* env);

/// Throws the Kotlin exception matching the C++ exception being handled. Call only from a catch block.
void throw_current(JNIEnv* env);

/// Runs an entry point's body, turning any C++ exception into a pending Kotlin exception and a default result.
template <class F>
auto guarded(JNIEnv* env, F&& body) -> decltype(body()) {
    using Result = decltype(body());
    try {
        return body();
    } catch (...) {
        throw_current(env);
    }
    if constexpr (!std::is_void_v<Result>) return Result{};
}

/// A native object owned by a Kotlin wrapper through a jlong handle.
template <class T>
T& handle(jlong value) {
    return *reinterpret_cast<T*>(static_cast<std::intptr_t>(value));
}

template <class T>
jlong make_handle(T* object) {
    return static_cast<jlong>(reinterpret_cast<std::intptr_t>(object));
}

/// Registers `methods` on the Kotlin object `class_name`; aborts loading when they do not match.
void register_natives(JNIEnv* env, const char* class_name, const JNINativeMethod* methods, std::size_t count);

template <std::size_t N>
void register_natives(JNIEnv* env, const char* class_name, const JNINativeMethod (&methods)[N]) {
    register_natives(env, class_name, methods, N);
}

}  // namespace mu::jni
