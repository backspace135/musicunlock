plugins {
    alias(libs.plugins.android.application) apply false
    alias(libs.plugins.android.library) apply false
    alias(libs.plugins.kotlin.compose) apply false
}

// Keep every module's output under apps/android/build/, like the other platforms.
subprojects {
    layout.buildDirectory = rootProject.layout.buildDirectory.dir(name)
}
