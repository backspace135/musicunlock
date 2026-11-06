#include "pch.h"

#include "Settings.h"

#include <shlobj.h>

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "musicunlock/text.h"

namespace settings {

namespace {

constexpr std::pair<mu::NamingPolicy, const char*> naming_ids[] = {
    {mu::NamingPolicy::original, "original"},
    {mu::NamingPolicy::artist_title, "artistTitle"},
    {mu::NamingPolicy::title, "title"},
};

std::filesystem::path file() { return data_directory() / L"settings.json"; }

}  // namespace

std::filesystem::path data_directory() {
    wchar_t override_path[MAX_PATH];
    if (GetEnvironmentVariableW(L"MUSICUNLOCK_DATA_DIR", override_path, MAX_PATH)) return override_path;
    PWSTR raw = nullptr;
    std::filesystem::path folder;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw))) folder = raw;
    CoTaskMemFree(raw);
    return folder / L"MusicUnlock";
}

Export load() {
    Export value;
    std::ifstream input(file(), std::ios::binary);
    if (!input) return value;
    std::stringstream text;
    text << input.rdbuf();
    const auto json = nlohmann::json::parse(text.str(), nullptr, false);
    if (!json.is_object()) return value;
    if (auto it = json.find("outputDirectory"); it != json.end() && it->is_string() && !it->get_ref<const std::string&>().empty()) {
        value.output_directory = mu::utf8_to_path(it->get<std::string>());
    }
    if (auto it = json.find("namingPolicy"); it != json.end() && it->is_string()) {
        for (const auto& [policy, id] : naming_ids) {
            if (*it == id) value.naming = policy;
        }
    }
    return value;
}

void save(const Export& value) {
    nlohmann::json json = nlohmann::json::object();
    if (value.output_directory) json["outputDirectory"] = mu::path_to_utf8(*value.output_directory);
    for (const auto& [policy, id] : naming_ids) {
        if (policy == value.naming) json["namingPolicy"] = id;
    }
    std::error_code error;
    std::filesystem::create_directories(data_directory(), error);
    // Write a sibling file, then swap it in, so a crash never leaves half a settings file.
    const auto target = file();
    auto temporary = target;
    temporary += L".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return;
        output << json.dump(2);
        if (!output) return;
    }
    MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

}  // namespace settings
