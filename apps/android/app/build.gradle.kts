plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
}

// scripts/device.py installs each branch's debug build next to the others: -Pmusicunlock.suffix=unlock builds
// io.github.backspace135.musicunlock.unlock, shown as "音匣 (unlock)". Release builds never take a suffix.
val suffix = providers.gradleProperty("musicunlock.suffix").orNull?.also {
    require(Regex("[a-z][a-z0-9_]*").matches(it)) { "musicunlock.suffix must be a lowercase identifier: $it" }
}

/** Copies the repository's LICENSE and THIRD_PARTY_NOTICES.txt into assets/licenses/ for the 开源许可 page. */
abstract class CopyLicenses : DefaultTask() {
    @get:InputFiles
    @get:PathSensitive(PathSensitivity.NAME_ONLY)
    abstract val sources: ConfigurableFileCollection

    @get:OutputDirectory
    abstract val outputDir: DirectoryProperty

    @TaskAction
    fun copy() {
        val target = outputDir.get().asFile.resolve("licenses")
        target.deleteRecursively()
        target.mkdirs()
        sources.forEach { it.copyTo(target.resolve(it.name)) }
    }
}

val copyLicenses = tasks.register<CopyLicenses>("copyLicenses") {
    val repository = rootDir.resolve("../..")
    sources.from(repository.resolve("LICENSE"), repository.resolve("THIRD_PARTY_NOTICES.txt"))
}

androidComponents {
    onVariants { variant ->
        variant.sources.assets?.addGeneratedSourceDirectory(copyLicenses, CopyLicenses::outputDir)
    }
}

android {
    namespace = "io.github.backspace135.musicunlock"
    compileSdk = libs.versions.compileSdk.get().toInt()

    defaultConfig {
        applicationId = "io.github.backspace135.musicunlock"
        minSdk = libs.versions.minSdk.get().toInt()
        targetSdk = libs.versions.compileSdk.get().toInt()
        // major * 10000 + minor * 100 + patch. Keep this next to versionName; 0.1.0 is 100.
        versionCode = 100
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
    implementation(libs.androidx.compose.material.icons.core)
    implementation(libs.androidx.compose.ui.tooling.preview)
    implementation(libs.androidx.lifecycle.runtime.compose)
    implementation(libs.androidx.lifecycle.viewmodel.compose)
    debugImplementation(libs.androidx.compose.ui.tooling)
    testImplementation(libs.junit)
}
