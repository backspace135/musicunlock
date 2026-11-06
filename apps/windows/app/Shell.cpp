#include "pch.h"

#include "Shell.h"

#include <shellapi.h>
#include <shlobj.h>

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

void reveal(const std::filesystem::path& path) {
    if (PIDLIST_ABSOLUTE item = ILCreateFromPathW(path.c_str())) {
        SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
        ILFree(item);
    }
}

void open_folder(const std::filesystem::path& folder) {
    ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace shell
