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
std::filesystem::path lyrics_file() { return data_directory() / L"lyrics.json"; }

nlohmann::json read_json(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return nullptr;
    std::stringstream text;
    text << input.rdbuf();
    return nlohmann::json::parse(text.str(), nullptr, false);
}

std::optional<std::filesystem::path> read_path(const nlohmann::json& json, const char* key) {
    if (auto it = json.find(key); it != json.end() && it->is_string() && !it->get_ref<const std::string&>().empty()) {
        return mu::utf8_to_path(it->get<std::string>());
    }
    return std::nullopt;
}

void write_json(const std::filesystem::path& target, const nlohmann::json& json) {
    std::error_code error;
    std::filesystem::create_directories(target.parent_path(), error);
    // Write a sibling file, then swap it in, so a crash never leaves half a settings file.
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
    const auto json = read_json(file());
    if (!json.is_object()) return value;
    value.output_directory = read_path(json, "outputDirectory");
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
    write_json(file(), json);
}

Lyrics load_lyrics() {
    Lyrics value;
    const auto json = read_json(lyrics_file());
    if (!json.is_object()) return value;
    // The core's JSON form of the options; invalid or unknown fields fall back to the defaults there.
    if (auto it = json.find("options"); it != json.end()) value.options = mu::options_from_json(it->dump());
    value.output_directory = read_path(json, "outputDirectory");
    return value;
}

void save_lyrics(const Lyrics& value) {
    nlohmann::json json = nlohmann::json::object();
    json["options"] = nlohmann::json::parse(mu::options_to_json(value.options), nullptr, false);
    if (value.output_directory) json["outputDirectory"] = mu::path_to_utf8(*value.output_directory);
    write_json(lyrics_file(), json);
}

}  // namespace settings
