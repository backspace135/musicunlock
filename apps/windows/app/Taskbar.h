#pragma once
// Taskbar identity. Started through the single exe (launcher/main.cpp), the app runs from an extracted copy in
// runtime\<version>\; pinning its window would pin that copy, which the next update removes, and starting from the
// pin would skip the launcher. So the window gets an explicit AppUserModelID and relaunch properties that point
// back at the single exe.
//
// MUSICUNLOCK_LAUNCHER_PATH  set by the launcher: the single exe's full path
// MUSICUNLOCK_TASKBAR_LOG    test diagnostics (screenshot.py): append the identity as read back from the process and
//                            the window, once labelled and again once cleared

#include <windows.h>

#include <winrt/Microsoft.UI.Windowing.h>

namespace taskbar {

/// Shared by the app and every shortcut that starts it (the installer's too, #10). Never change it: pinned buttons
/// and shortcuts that carry the old one would become a second button.
inline constexpr wchar_t app_user_model_id[] = L"MusicUnlock.App";

/// Call once at start-up, after COM and before any window: decides the identity and sets it on the process.
void claim_process();
/// Labels the main window before it is shown; the labels are cleared again when the window is destroyed.
void label_window(HWND window, winrt::Microsoft::UI::Windowing::AppWindow const& app_window);

}  // namespace taskbar
