# The C++ core shared by the Windows app and CLI (and, from phase 1, the Android app), with its tests.
# Included with add_subdirectory; the including project sets the language standard and compile options.
get_filename_component(MU_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

add_library(musicunlock_core STATIC
  src/audio_decoder.cpp
  src/base64.cpp
  src/conversion.cpp
  src/file_io.cpp
  src/import.cpp
  src/legacy.cpp
  src/lyric_workspace.cpp
  src/lyrics.cpp
  src/md5.cpp
  src/music_api.cpp
  src/ncm.cpp
  src/qmc.cpp
  src/qr_login.cpp
  src/queue.cpp
  src/session.cpp
  src/session_store.cpp
  src/tags.cpp
  src/text.cpp
  third_party/qrcodegen/qrcodegen.cpp
)
target_include_directories(musicunlock_core PUBLIC include PRIVATE src)
target_include_directories(musicunlock_core SYSTEM PRIVATE third_party)
if(WIN32)
  target_sources(musicunlock_core PRIVATE src/crypto_bcrypt.cpp src/dpapi_win.cpp src/http_winhttp.cpp
    src/platform_text_win.cpp)
  target_link_libraries(musicunlock_core PUBLIC bcrypt crypt32 winhttp)
elseif(APPLE)
  # No HTTP transport on the Mac: API tests use a scripted transport.
  target_sources(musicunlock_core PRIVATE src/crypto_commoncrypto.cpp src/platform_text_mac.cpp)
  target_link_libraries(musicunlock_core PUBLIC "-framework CoreFoundation")
elseif(ANDROID)
  # Placeholder: phase 1 adds the Android platform implementations.
  message(FATAL_ERROR "The Android platform implementations are not there yet.")
else()
  message(FATAL_ERROR "Only Windows (product) and macOS (development) are supported.")
endif()

enable_testing()
add_executable(musicunlock_core_tests tests/core_tests.cpp tests/account_tests.cpp tests/import_tests.cpp
  tests/lyric_workspace_tests.cpp tests/lyrics_tests.cpp tests/queue_tests.cpp)
target_link_libraries(musicunlock_core_tests PRIVATE musicunlock_core)
target_include_directories(musicunlock_core_tests PRIVATE src)
target_compile_definitions(musicunlock_core_tests PRIVATE
  _CRT_SECURE_NO_WARNINGS  # std::getenv for the opt-in live tests
  MU_FIXTURES_DIR="${MU_REPO_ROOT}/tests/fixtures/qmc"
  MU_TEST_SCRATCH="${CMAKE_CURRENT_BINARY_DIR}/test-scratch")
add_test(NAME core COMMAND musicunlock_core_tests)
