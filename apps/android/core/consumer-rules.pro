# Classes and members that libmusicunlock.so resolves by name (FindClass / GetMethodID / RegisterNatives) in
# JNI_OnLoad. Every Kotlin class the C++ core calls back into, or constructs, must be listed here; JNI_OnLoad aborts
# when one is missing, so a release build that strips one fails on its first start rather than later.

# Natives are registered by class and method name in JNI_OnLoad, so none may be renamed or removed, even when the
# app never calls the Kotlin function that uses it.
-keepclasseswithmembers class io.github.backspace135.musicunlock.core.** { native <methods>; }

# Exceptions the bridge throws into Kotlin, or reads back when a Kotlin callback throws them into the core.
-keep class io.github.backspace135.musicunlock.core.CoreException { <init>(java.lang.String); }
-keep class io.github.backspace135.musicunlock.core.LyricException { <init>(java.lang.String); }
-keep class io.github.backspace135.musicunlock.core.CoreCancelledException { <init>(); }
-keep class io.github.backspace135.musicunlock.core.DecodeException {
    <init>(int, java.lang.String);
    int kindCode();
}

# The core's text conversion (platform_text_jni.cpp).
-keep class io.github.backspace135.musicunlock.core.PlatformText {
    java.lang.String mandarinToLatin(java.lang.String);
    java.lang.String gb18030ToUtf8(byte[]);
}

# Conversion results (audio_jni.cpp).
-keep class io.github.backspace135.musicunlock.core.ConvertedAudio {
    <init>(java.lang.String, java.lang.String, java.lang.String, java.lang.String, java.lang.String, byte[]);
}

# Callbacks of the HTTP transport, the QR login poll and the session cipher (api_jni.cpp, session_jni.cpp).
-keep class io.github.backspace135.musicunlock.core.CoreCallbacks {
    io.github.backspace135.musicunlock.core.NativeHttpResult http(io.github.backspace135.musicunlock.core.HttpTransport, java.lang.String, java.lang.String, java.lang.String[], byte[]);
    void qrReport(io.github.backspace135.musicunlock.core.QrPollListener, int, java.lang.String);
    byte[] seal(io.github.backspace135.musicunlock.core.SessionCipher, byte[]);
    byte[] open(io.github.backspace135.musicunlock.core.SessionCipher, byte[]);
}
-keep class io.github.backspace135.musicunlock.core.NativeHttpResult {
    int status;
    byte[] body;
    java.lang.String[] setCookies;
}
# Named in native method signatures, so their names must stay.
-keep interface io.github.backspace135.musicunlock.core.HttpTransport
-keep interface io.github.backspace135.musicunlock.core.QrPollListener
-keep interface io.github.backspace135.musicunlock.core.SessionCipher
