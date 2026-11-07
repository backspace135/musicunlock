plugins {
    alias(libs.plugins.android.application) apply false
    alias(libs.plugins.android.library) apply false
    alias(libs.plugins.kotlin.compose) apply false
}

// Keep every module's output under apps/android/build/, like the other platforms.
subprojects {
    layout.buildDirectory = rootProject.layout.buildDirectory.dir(name)
    // Every module uses the pinned build tools and NDK: :core compiles with the NDK and :app strips the packaged
    // libraries with it.
    plugins.withType<com.android.build.gradle.api.AndroidBasePlugin> {
        extensions.configure<com.android.build.api.dsl.CommonExtension>("android") {
            buildToolsVersion = libs.versions.buildTools.get()
            ndkVersion = libs.versions.ndk.get()
        }
    }
}
