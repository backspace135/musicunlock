#include "pch.h"

#include "Shell.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include "musicunlock/audio.h"

namespace shell {

namespace {

std::filesystem::path known_folder(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    std::filesystem::path folder;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw))) folder = raw;
    CoTaskMemFree(raw);  // required even when the call fails
    return folder;
}

}  // namespace

std::filesystem::path downloads_folder() {
    auto folder = known_folder(FOLDERID_Downloads);
    return folder.empty() ? known_folder(FOLDERID_Profile) : folder;
}

namespace {

struct Filter {
    std::wstring name;
    std::wstring patterns;
};

std::vector<std::filesystem::path> show(HWND owner, const wchar_t* title, FILEOPENDIALOGOPTIONS options,
                                        const std::filesystem::path& initial = {}, std::vector<Filter> filters = {}) {
    winrt::com_ptr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.put())))) {
        return {};
    }
    FILEOPENDIALOGOPTIONS current = 0;
    dialog->GetOptions(&current);
    dialog->SetOptions(current | options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(title);
    if (!filters.empty()) {
        filters.push_back({L"所有文件", L"*.*"});
        std::vector<COMDLG_FILTERSPEC> specs;
        for (const auto& filter : filters) specs.push_back({filter.name.c_str(), filter.patterns.c_str()});
        dialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
    }
    if (!initial.empty()) {
        winrt::com_ptr<IShellItem> folder;
        if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(folder.put())))) {
            dialog->SetFolder(folder.get());
        }
    }
    if (FAILED(dialog->Show(owner))) return {};  // cancelled
    winrt::com_ptr<IShellItemArray> items;
    if (FAILED(dialog->GetResults(items.put()))) return {};
    DWORD count = 0;
    items->GetCount(&count);
    std::vector<std::filesystem::path> paths;
    for (DWORD i = 0; i < count; ++i) {
        winrt::com_ptr<IShellItem> item;
        PWSTR name = nullptr;
        if (SUCCEEDED(items->GetItemAt(i, item.put())) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name))) {
            paths.emplace_back(name);
        }
        CoTaskMemFree(name);
    }
    return paths;
}

}  // namespace

std::vector<std::filesystem::path> pick_audio(HWND owner, bool folders) {
    if (folders) return show(owner, L"添加音乐文件夹", FOS_PICKFOLDERS | FOS_ALLOWMULTISELECT);
    std::wstring patterns;
    for (const auto& ext : mu::supported_extensions()) {
        if (!patterns.empty()) patterns += L";";
        patterns += L"*." + std::wstring(ext.begin(), ext.end());
    }
    return show(owner, L"添加加密音乐文件", FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST, {}, {{L"加密音乐文件", patterns}});
}

std::optional<std::filesystem::path> pick_folder(HWND owner, const std::filesystem::path& initial, const wchar_t* title) {
    std::error_code error;
    auto paths = show(owner, title, FOS_PICKFOLDERS,
                      std::filesystem::is_directory(initial, error) ? initial : std::filesystem::path());
    if (paths.empty()) return std::nullopt;
    return paths.front();
}

std::optional<std::filesystem::path> pick_lyric_file(HWND owner) {
    auto paths = show(owner, L"打开本地歌词", FOS_FILEMUSTEXIST, {}, {{L"歌词文件（LRC、TXT）", L"*.lrc;*.txt"}});
    if (paths.empty()) return std::nullopt;
    return paths.front();
}

std::vector<std::filesystem::path> command_line_paths() {
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::filesystem::path> paths;
    for (int i = 1; arguments && i < count; ++i) {
        if (arguments[i][0] != L'-') paths.emplace_back(arguments[i]);
    }
    LocalFree(arguments);
    return paths;
}

void reveal(const std::filesystem::path& path) {
    if (PIDLIST_ABSOLUTE item = ILCreateFromPathW(path.c_str())) {
        SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
        ILFree(item);
    }
}

void open_folder(const std::filesystem::path& folder) {
    ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void open_bundled(const wchar_t* name) {
    wchar_t exe[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const auto file = std::filesystem::path(std::wstring(exe, length)).replace_filename(name);
    ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace shell
