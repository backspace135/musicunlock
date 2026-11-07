# The C++ core shared by the Windows app and CLI and the Android app, with its tests.
# Included with add_subdirectory; the including project sets the language standard and compile options.
# src/ holds portable code only; platform implementations live in src/platform/<platform>/, so a project that
# compiles src/*.cpp (like the WinUI 3 app's MSBuild project) adds just its own platform folder.
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
# The portable AES in src/platform/portable/ replaces the platform crypto library where native code cannot reach
# one; elsewhere this option swaps it in, so the development Mac can run every core test against it.
option(MU_PORTABLE_AES "Use the portable AES instead of the platform crypto library" OFF)
if(WIN32)
  target_sources(musicunlock_core PRIVATE src/platform/win/dpapi_win.cpp src/platform/win/http_winhttp.cpp
    src/platform/win/platform_text_win.cpp)
  if(NOT MU_PORTABLE_AES)
    target_sources(musicunlock_core PRIVATE src/platform/win/crypto_bcrypt.cpp)
  endif()
  target_link_libraries(musicunlock_core PUBLIC bcrypt crypt32 winhttp)
elseif(APPLE)
  # No HTTP transport on the Mac: API tests use a scripted transport.
  target_sources(musicunlock_core PRIVATE src/platform/apple/platform_text_mac.cpp)
  if(NOT MU_PORTABLE_AES)
    target_sources(musicunlock_core PRIVATE src/platform/apple/crypto_commoncrypto.cpp)
  endif()
  target_link_libraries(musicunlock_core PUBLIC "-framework CoreFoundation")
elseif(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # Android, and Linux as the Android CI host: portable AES, no HTTP transport or session cipher in native code.
  # Text conversion (pinyin, GBK) is left out of the core: the Android app's JNI library implements it with the
  # platform's Java APIs, while the core tests, which run without a JVM, link this stand-in returning nullopt.
  set(MU_PORTABLE_AES ON)
  set(mu_no_platform_text ON)
  add_library(musicunlock_platform_text_none STATIC src/platform/portable/platform_text_none.cpp)
  target_include_directories(musicunlock_platform_text_none PRIVATE include src)
else()
  message(FATAL_ERROR "Only Windows and Android (products), macOS (development) and Linux (CI) are supported.")
endif()
if(MU_PORTABLE_AES)
  target_sources(musicunlock_core PRIVATE src/platform/portable/crypto_portable.cpp)
endif()

# The tests read fixtures and write scratch files at these paths; a cross-compiled test binary points them at the
# folders its runner copies to the device.
set(MU_TEST_FIXTURES_DIR "${MU_REPO_ROOT}/tests/fixtures/qmc" CACHE PATH "Fixtures the core tests read")
set(MU_TEST_SCRATCH_DIR "${CMAKE_CURRENT_BINARY_DIR}/test-scratch" CACHE PATH "Scratch folder for the core tests")

enable_testing()
add_executable(musicunlock_core_tests tests/core_tests.cpp tests/account_tests.cpp tests/import_tests.cpp
  tests/lyric_workspace_tests.cpp tests/lyrics_tests.cpp tests/queue_tests.cpp)
target_link_libraries(musicunlock_core_tests PRIVATE musicunlock_core)
if(TARGET musicunlock_platform_text_none)
  target_link_libraries(musicunlock_core_tests PRIVATE musicunlock_platform_text_none)
endif()
target_include_directories(musicunlock_core_tests PRIVATE src)
target_compile_definitions(musicunlock_core_tests PRIVATE
  _CRT_SECURE_NO_WARNINGS  # std::getenv for the opt-in live tests
  MU_FIXTURES_DIR="${MU_TEST_FIXTURES_DIR}"
  MU_TEST_SCRATCH="${MU_TEST_SCRATCH_DIR}")
if(MU_PORTABLE_AES)
  target_compile_definitions(musicunlock_core_tests PRIVATE MU_PORTABLE_AES)
endif()
if(mu_no_platform_text)
  target_compile_definitions(musicunlock_core_tests PRIVATE MU_NO_PLATFORM_TEXT)
endif()
add_test(NAME core COMMAND musicunlock_core_tests)
