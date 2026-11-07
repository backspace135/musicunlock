// JNI entry point of libmusicunlock.so. The bridge to the shared core's API arrives in phase 2.
#include <jni.h>

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM*, void*) { return JNI_VERSION_1_6; }
