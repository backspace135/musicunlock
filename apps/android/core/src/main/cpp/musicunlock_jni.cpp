// JNI entry point of libmusicunlock.so: caches the VM and classes, then registers each area's native methods.
#include <jni.h>

#include "jni_support.h"

namespace mu::jni {
void register_lyrics(JNIEnv* env);
void register_platform_text(JNIEnv* env);
}  // namespace mu::jni

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    mu::jni::load(vm, env);
    mu::jni::register_platform_text(env);
    mu::jni::register_lyrics(env);
    return JNI_VERSION_1_6;
}
