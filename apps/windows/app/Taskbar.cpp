#include "pch.h"

#include "Taskbar.h"

#include <propvarutil.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

// The PKEY_ constants are only declared without INITGUID.
#include <initguid.h>
#include <propkey.h>

namespace taskbar {

namespace {

std::wstring environment(const wchar_t* name) {
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if (length == 0) return {};
    std::wstring value(length, L'\0');
    value.resize(GetEnvironmentVariableW(name, value.data(), length));
    return value;
}

bool same_id(const wchar_t* a, const wchar_t* b) { return CompareStringOrdinal(a, -1, b, -1, TRUE) == CSTR_EQUAL; }

/// The single exe's path when the window should carry the identity, empty otherwise. Decided by claim_process.
std::wstring& single_exe() {
    static std::wstring path;
    return path;
}

/// The AppUserModelID stored in a shortcut, empty when it has none.
std::wstring shortcut_id(const wchar_t* link) {
    winrt::com_ptr<IShellLinkW> shortcut;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(shortcut.put())))) return {};
    const auto file = shortcut.try_as<IPersistFile>();
    const auto properties = shortcut.try_as<IPropertyStore>();
    if (!file || !properties || FAILED(file->Load(link, STGM_READ))) return {};
    PROPVARIANT value;
    PropVariantInit(&value);
    std::wstring id;
    if (SUCCEEDED(properties->GetValue(PKEY_AppUserModel_ID, &value)) && value.vt == VT_LPWSTR) id = value.pwszVal;
    PropVariantClear(&value);
    return id;
}

/// Whether Explorer started us from a shortcut or with an AppUserModelID other than ours. The launcher passes its
/// STARTF_TITLEISLINKNAME / STARTF_TITLEISAPPID on, so this is how the single exe itself was started.
bool started_as_someone_else() {
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    GetStartupInfoW(&startup);
    if (!startup.lpTitle) return false;
    if (startup.dwFlags & STARTF_TITLEISAPPID) return !same_id(startup.lpTitle, app_user_model_id);
    if (startup.dwFlags & STARTF_TITLEISLINKNAME) return !same_id(shortcut_id(startup.lpTitle).c_str(), app_user_model_id);
    return false;
}

const std::pair<const PROPERTYKEY*, const wchar_t*> window_keys[] = {
    {&PKEY_AppUserModel_RelaunchCommand, L"RelaunchCommand"},
    {&PKEY_AppUserModel_RelaunchDisplayNameResource, L"RelaunchDisplayNameResource"},
    {&PKEY_AppUserModel_RelaunchIconResource, L"RelaunchIconResource"},
    {&PKEY_AppUserModel_ID, L"ID"},  // set last and cleared last: the relaunch properties belong to it
};

void log(const wchar_t* stage, HWND window) {
    const auto path = environment(L"MUSICUNLOCK_TASKBAR_LOG");
    FILE* file = nullptr;
    if (path.empty() || _wfopen_s(&file, path.c_str(), L"a, ccs=UTF-8") != 0 || !file) return;
    if (!window) {
        PWSTR id = nullptr;
        fwprintf(file, L"%s ID: %s\n", stage,
                 SUCCEEDED(GetCurrentProcessExplicitAppUserModelID(&id)) && id ? id : L"(none)");
        CoTaskMemFree(id);
    } else {
        // Read back through a fresh property store, as the taskbar sees the window.
        winrt::com_ptr<IPropertyStore> store;
        if (const HRESULT result = SHGetPropertyStoreForWindow(window, IID_PPV_ARGS(store.put())); FAILED(result)) {
            fwprintf(file, L"%s store: failed 0x%08X\n", stage, static_cast<unsigned>(result));
        }
        for (const auto& [key, name] : window_keys) {
            PROPVARIANT value;
            PropVariantInit(&value);
            const bool found = store && SUCCEEDED(store->GetValue(*key, &value)) && value.vt == VT_LPWSTR;
            fwprintf(file, L"%s %s: %s\n", stage, name, found ? value.pwszVal : L"(none)");
            PropVariantClear(&value);
        }
    }
    fclose(file);
}

/// Sets the window's properties in the order of window_keys, or removes them all when `values` is empty.
void set(HWND window, const std::vector<std::wstring>& values) {
    winrt::com_ptr<IPropertyStore> store;
    if (FAILED(SHGetPropertyStoreForWindow(window, IID_PPV_ARGS(store.put())))) return;
    for (std::size_t i = 0; i < std::size(window_keys); ++i) {
        PROPVARIANT value;
        PropVariantInit(&value);  // VT_EMPTY removes the property
        if (values.empty() || SUCCEEDED(InitPropVariantFromString(values[i].c_str(), &value))) {
            store->SetValue(*window_keys[i].first, value);
        }
        PropVariantClear(&value);
    }
}

}  // namespace

void claim_process() {
    auto launcher = environment(L"MUSICUNLOCK_LAUNCHER_PATH");
    SetEnvironmentVariableW(L"MUSICUNLOCK_LAUNCHER_PATH", nullptr);  // not for programs the app opens
    // Run directly (development, build/dist) the app keeps the identity Windows derives from its path, so a
    // development build never joins or replaces the pinned single exe. Started from a shortcut without our ID
    // (one the user made, or Explorer's "pin to taskbar" on the exe file) the taskbar already follows that
    // shortcut, which points at the single exe; an explicit ID would split the window off from it.
    if (!launcher.empty() && !started_as_someone_else()) {
        single_exe() = std::move(launcher);
        // For the whole process too: the file dialogs add recent items under the process's ID.
        SetCurrentProcessExplicitAppUserModelID(app_user_model_id);
    }
    log(L"process", nullptr);
}

void label_window(HWND window, winrt::Microsoft::UI::Windowing::AppWindow const& app_window) {
    if (const auto& exe = single_exe(); !exe.empty()) {
        // Pinning creates a shortcut from these: the quoted single exe without arguments, its name and icon 1.
        set(window, {L"\"" + exe + L"\"", L"音匣", exe + L",-1", app_user_model_id});
        // A window's properties must be removed before it is destroyed, or their resources leak.
        app_window.Destroying([window](auto&&, auto&&) {
            set(window, {});
            log(L"cleared", window);
        });
    }
    log(L"window", window);
}

}  // namespace taskbar
