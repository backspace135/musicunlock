# Native part of :core. Gradle's externalNativeBuild builds libmusicunlock.so from here; scripts/build.py also
# configures it directly to build musicunlock_core_tests for the host (portable AES) or for an Android device.
cmake_minimum_required(VERSION 3.24)
project(MusicUnlockAndroid LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-Wall -Wextra -Wpedantic)

get_filename_component(MU_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../.." ABSOLUTE)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
enable_testing()
add_subdirectory("${MU_REPO_ROOT}/shared/core" shared-core)

if(ANDROID)
  # The whole core goes into the library, so linking it (with the NDK's --no-undefined) proves every symbol
  # resolves on Android before the JNI bridge (phase 2) starts calling into it.
  add_library(musicunlock SHARED musicunlock_jni.cpp)
  target_link_libraries(musicunlock PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,musicunlock_core>")
endif()
