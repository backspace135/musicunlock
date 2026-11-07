plugins {
    alias(libs.plugins.android.library)
}

/** Copies the repository's resources/kgm.mask (shared by every platform) into this module's assets. */
abstract class KgmMaskAssets : DefaultTask() {
    @get:InputFile
    @get:PathSensitive(PathSensitivity.NONE)
    abstract val mask: RegularFileProperty

    @get:OutputDirectory
    abstract val assets: DirectoryProperty

    @TaskAction
    fun copy() {
        val target = assets.get().asFile
        target.deleteRecursively()
        target.mkdirs()
        mask.get().asFile.copyTo(target.resolve("kgm.mask"))
    }
}

val kgmMaskAssets = tasks.register<KgmMaskAssets>("kgmMaskAssets") {
    mask = rootProject.layout.projectDirectory.file("../../resources/kgm.mask")
}

androidComponents {
    onVariants { variant ->
        variant.sources.assets?.addGeneratedSourceDirectory(kgmMaskAssets, KgmMaskAssets::assets)
        // The instrumented tests decode the shared fixtures (tests/fixtures/qmc), as the core tests do.
        variant.androidTest?.sources?.assets?.addStaticSourceDirectory(
            rootProject.layout.projectDirectory.dir("../../tests/fixtures").asFile.path)
    }
}

android {
    namespace = "io.github.backspace135.musicunlock.core"
    compileSdk = libs.versions.compileSdk.get().toInt()

    defaultConfig {
        minSdk = libs.versions.minSdk.get().toInt()
        // scripts/device.py test passes the branch's -Pmusicunlock.suffix (see app/build.gradle.kts), so sessions on
        // one phone each install their own test package and never replace or uninstall another's mid-run.
        testApplicationId = "io.github.backspace135.musicunlock.core.test" +
            (providers.gradleProperty("musicunlock.suffix").orNull?.let { ".$it" } ?: "")
        ndk { abiFilters += "arm64-v8a" }
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        // Keeps every class and method the C++ core calls back into when an app using :core is minified.
        consumerProguardFiles("consumer-rules.pro")
        externalNativeBuild {
            cmake {
                // The core's tests are built and run by scripts/build.py, not packaged.
                targets += "musicunlock"
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = libs.versions.cmake.get()
            // CMake's staging folder defaults to core/.cxx; keep it with the other build output, but outside
            // this module's build folder, which AGP expects to be wiped by clean.
            buildStagingDirectory = rootProject.layout.buildDirectory.dir("cxx").get().asFile
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

dependencies {
    // Part of the API: suspend functions and Flow.
    api(libs.kotlinx.coroutines.android)
    implementation(libs.androidx.core)
    // The app's HTTP transport for the core (OkHttpTransport is public API).
    api(libs.okhttp)
    androidTestImplementation(libs.androidx.test.core)
    androidTestImplementation(libs.androidx.test.runner)
    androidTestImplementation(libs.androidx.test.ext.junit)
    androidTestImplementation(libs.junit)
    androidTestImplementation(libs.kotlinx.coroutines.test)
    androidTestImplementation(libs.okhttp.mockwebserver)
    androidTestImplementation(libs.okhttp.tls)
}
