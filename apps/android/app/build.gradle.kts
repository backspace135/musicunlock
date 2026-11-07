plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
}

// scripts/device.py installs each branch's debug build next to the others: -Pmusicunlock.suffix=unlock builds
// io.github.backspace135.musicunlock.unlock, shown as "音匣 (unlock)". Release builds never take a suffix.
val suffix = providers.gradleProperty("musicunlock.suffix").orNull?.also {
    require(Regex("[a-z][a-z0-9_]*").matches(it)) { "musicunlock.suffix must be a lowercase identifier: $it" }
}

android {
    namespace = "io.github.backspace135.musicunlock"
    compileSdk = libs.versions.compileSdk.get().toInt()

    defaultConfig {
        applicationId = "io.github.backspace135.musicunlock"
        minSdk = libs.versions.minSdk.get().toInt()
        targetSdk = libs.versions.compileSdk.get().toInt()
        versionCode = 1
        versionName = "0.1.0"
        ndk { abiFilters += "arm64-v8a" }
        manifestPlaceholders["appLabel"] = "@string/app_name"
    }

    buildTypes {
        debug {
            if (suffix != null) {
                applicationIdSuffix = ".$suffix"
                manifestPlaceholders["appLabel"] = "音匣 ($suffix)"
            }
        }
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"))
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        compose = true
    }
}

dependencies {
    implementation(project(":core"))
    implementation(libs.androidx.activity.compose)
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.compose.foundation)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.compose.ui.tooling.preview)
    debugImplementation(libs.androidx.compose.ui.tooling)
}
