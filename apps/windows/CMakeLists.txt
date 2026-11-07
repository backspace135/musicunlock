# CLI, launcher and the shared core (shared/core) with its tests. Builds with MSVC on Windows and with Apple clang
# on the development Mac (core and tests only need the platform AES backend). The WinUI 3 app has its own MSBuild
# project.
cmake_minimum_required(VERSION 3.21)
project(MusicUnlockWindows VERSION 0.1.1 LANGUAGES CXX)

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

# Keep every executable at the top of the build folder, where the scripts and CI look for them.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
enable_testing()
add_subdirectory("${MU_REPO_ROOT}/shared/core" shared-core)

add_executable(musicunlock cli/main.cpp)
target_link_libraries(musicunlock PRIVATE musicunlock_core)
if(WIN32)
  set(MU_KGM_MASK "${MU_REPO_ROOT}/resources/kgm.mask")
  configure_file(cli/musicunlock.rc.in "${CMAKE_CURRENT_BINARY_DIR}/musicunlock.rc" @ONLY)
  target_sources(musicunlock PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/musicunlock.rc")
endif()

# Single-exe launcher. The payload format is portable and tested on the Mac too; extraction is Windows code.
add_library(musicunlock_launcher STATIC launcher/payload.cpp)
target_include_directories(musicunlock_launcher PUBLIC launcher)
if(WIN32)
  target_sources(musicunlock_launcher PRIVATE launcher/codec.cpp launcher/install.cpp)
  target_link_libraries(musicunlock_launcher PUBLIC bcrypt cabinet shell32 ole32)
  # The MusicUnlock.exe stub: scripts/pack.py embeds the app folder into a copy of it. It reuses the app's
  # resource script for the icon and version information; the include path resolves the icon file.
  set(MU_APP_RC "${CMAKE_CURRENT_SOURCE_DIR}/app/MusicUnlock.rc")
  configure_file(launcher/launcher.rc.in "${CMAKE_CURRENT_BINARY_DIR}/launcher.rc" @ONLY)
  add_executable(launcher WIN32 launcher/main.cpp "${CMAKE_CURRENT_BINARY_DIR}/launcher.rc" launcher/launcher.manifest)
  target_include_directories(launcher PRIVATE app)
  target_link_libraries(launcher PRIVATE musicunlock_launcher)
  # Resolve cabinet.dll and bcrypt.dll from System32 only, never from the download folder the exe sits in.
  target_link_options(launcher PRIVATE /DEPENDENTLOADFLAG:0x800)
  add_executable(launcher_pack launcher/pack.cpp)
  target_link_libraries(launcher_pack PRIVATE musicunlock_launcher)
  # Stand-in app for scripts/smoke_launcher.py.
  add_executable(launcher_probe WIN32 tests/launcher_probe.cpp)
  target_link_libraries(launcher_probe PRIVATE shell32)
endif()
add_executable(musicunlock_launcher_tests tests/launcher_tests.cpp)
target_link_libraries(musicunlock_launcher_tests PRIVATE musicunlock_launcher)
target_include_directories(musicunlock_launcher_tests PRIVATE "${MU_REPO_ROOT}/shared/core/include"
  "${MU_REPO_ROOT}/shared/core/tests")
target_compile_definitions(musicunlock_launcher_tests PRIVATE MU_TEST_SCRATCH="${CMAKE_CURRENT_BINARY_DIR}/test-scratch")
add_test(NAME launcher COMMAND musicunlock_launcher_tests)

# Spring curves shared with the WinUI app. Pure C++20, so the Mac runs the same tests.
add_executable(musicunlock_ui_math_tests tests/ui_math_tests.cpp app/SpringCurve.cpp)
target_include_directories(musicunlock_ui_math_tests PRIVATE app)
add_test(NAME ui_math COMMAND musicunlock_ui_math_tests)
