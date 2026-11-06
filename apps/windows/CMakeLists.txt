# Portable core, CLI and tests. Builds with MSVC on Windows and with Apple clang on the development Mac
# (core and tests only need the platform AES backend). The WinUI 3 app has its own MSBuild project.
cmake_minimum_required(VERSION 3.21)
project(MusicUnlockWindows VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Static CRT: the CLI ships as one exe with no VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

get_filename_component(MU_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

if(MSVC)
  add_compile_options(/utf-8 /W4 /permissive- /EHsc /Zc:__cplusplus)
  add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(musicunlock_core STATIC
  core/src/audio_decoder.cpp
  core/src/base64.cpp
  core/src/conversion.cpp
  core/src/file_io.cpp
  core/src/import.cpp
  core/src/legacy.cpp
  core/src/lyric_workspace.cpp
  core/src/lyrics.cpp
  core/src/music_api.cpp
  core/src/ncm.cpp
  core/src/qmc.cpp
  core/src/qr_login.cpp
  core/src/queue.cpp
  core/src/session.cpp
  core/src/session_store.cpp
  core/src/tags.cpp
  core/src/text.cpp
  third_party/qrcodegen/qrcodegen.cpp
)
target_include_directories(musicunlock_core PUBLIC core/include PRIVATE core/src)
target_include_directories(musicunlock_core SYSTEM PRIVATE third_party)
if(WIN32)
  target_sources(musicunlock_core PRIVATE core/src/crypto_bcrypt.cpp core/src/dpapi_win.cpp core/src/http_winhttp.cpp
    core/src/platform_text_win.cpp)
  target_link_libraries(musicunlock_core PUBLIC bcrypt crypt32 winhttp)
elseif(APPLE)
  # No HTTP transport on the Mac: API tests use a scripted transport.
  target_sources(musicunlock_core PRIVATE core/src/crypto_commoncrypto.cpp core/src/platform_text_mac.cpp)
  target_link_libraries(musicunlock_core PUBLIC "-framework CoreFoundation")
else()
  message(FATAL_ERROR "Only Windows (product) and macOS (development) are supported.")
endif()

add_executable(musicunlock cli/main.cpp)
target_link_libraries(musicunlock PRIVATE musicunlock_core)
if(WIN32)
  set(MU_KGM_MASK "${MU_REPO_ROOT}/resources/kgm.mask")
  configure_file(cli/musicunlock.rc.in "${CMAKE_CURRENT_BINARY_DIR}/musicunlock.rc" @ONLY)
  target_sources(musicunlock PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/musicunlock.rc")
endif()

enable_testing()
add_executable(musicunlock_core_tests tests/core_tests.cpp tests/account_tests.cpp tests/import_tests.cpp
  tests/lyric_workspace_tests.cpp tests/lyrics_tests.cpp tests/queue_tests.cpp)
target_link_libraries(musicunlock_core_tests PRIVATE musicunlock_core)
target_include_directories(musicunlock_core_tests PRIVATE core/src)
target_compile_definitions(musicunlock_core_tests PRIVATE
  _CRT_SECURE_NO_WARNINGS  # std::getenv for the opt-in live tests
  MU_FIXTURES_DIR="${MU_REPO_ROOT}/tests/fixtures/qmc"
  MU_TEST_SCRATCH="${CMAKE_CURRENT_BINARY_DIR}/test-scratch")
add_test(NAME core COMMAND musicunlock_core_tests)

# Single-exe launcher. The payload format is portable and tested on the Mac too.
add_library(musicunlock_launcher STATIC launcher/payload.cpp)
target_include_directories(musicunlock_launcher PUBLIC launcher)
add_executable(musicunlock_launcher_tests tests/launcher_tests.cpp)
target_link_libraries(musicunlock_launcher_tests PRIVATE musicunlock_launcher)
target_include_directories(musicunlock_launcher_tests PRIVATE core/include)
add_test(NAME launcher COMMAND musicunlock_launcher_tests)
