plugins {
    alias(libs.plugins.android.library)
}

android {
    namespace = "io.github.backspace135.musicunlock.core"
    compileSdk = libs.versions.compileSdk.get().toInt()

    defaultConfig {
        minSdk = libs.versions.minSdk.get().toInt()
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
    androidTestImplementation(libs.androidx.test.core)
    androidTestImplementation(libs.androidx.test.runner)
    androidTestImplementation(libs.androidx.test.ext.junit)
    androidTestImplementation(libs.junit)
}
