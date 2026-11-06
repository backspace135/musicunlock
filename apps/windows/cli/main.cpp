// musicunlock <输出目录> <文件或目录> [更多文件…] — same usage and exit codes as the macOS CLI.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "musicunlock/conversion.h"
#include "musicunlock/import.h"
#include "musicunlock/text.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

void print(std::FILE* stream, const std::string& line) {
    const std::string text = line + "\n";
    std::fwrite(text.data(), 1, text.size(), stream);
}

#ifdef _WIN32
using NativeChar = wchar_t;

std::optional<fs::path> environment_path(const wchar_t* name) {
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if (length == 0) return std::nullopt;
    std::wstring value(length, L'\0');
    value.resize(GetEnvironmentVariableW(name, value.data(), length));
    return fs::path(value);
}

/// kgm.mask is embedded as RCDATA so the CLI stays a single file.
mu::Bytes bundled_mask() {
    HRSRC resource = FindResourceW(nullptr, L"KGM_MASK", MAKEINTRESOURCEW(10));  // RT_RCDATA
    if (!resource) return {};
    HGLOBAL loaded = LoadResource(nullptr, resource);
    const auto* data = static_cast<const std::uint8_t*>(LockResource(loaded));
    return data ? mu::Bytes(data, data + SizeofResource(nullptr, resource)) : mu::Bytes{};
}

#else
using NativeChar = char;

std::optional<fs::path> environment_path(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::optional<fs::path>(value) : std::nullopt;
}

mu::Bytes bundled_mask() { return {}; }
#endif

mu::Bytes load_mask() {
#ifdef _WIN32
    const auto override_path = environment_path(L"MUSICUNLOCK_KGM_MASK");
#else
    const auto override_path = environment_path("MUSICUNLOCK_KGM_MASK");
#endif
    if (!override_path) return bundled_mask();
    std::error_code error;
    const auto size = fs::file_size(*override_path, error);
    if (error) return {};
    std::FILE* file = nullptr;
#ifdef _WIN32
    if (_wfopen_s(&file, override_path->c_str(), L"rb") != 0) file = nullptr;
#else
    file = std::fopen(override_path->c_str(), "rb");
#endif
    if (!file) return {};
    mu::Bytes mask(static_cast<std::size_t>(size));
    mask.resize(std::fread(mask.data(), 1, mask.size(), file));
    std::fclose(file);
    return mask;
}

/// Folders are expanded; explicit files are kept even when unsupported, so they report a failure.
std::vector<fs::path> collect_sources(const std::vector<fs::path>& arguments) {
    std::vector<fs::path> sources;
    for (const auto& argument : arguments) {
        std::error_code error;
        if (!fs::is_directory(argument, error)) {
            sources.push_back(argument);
            continue;
        }
        auto found = mu::find_audio_files(argument);
        std::sort(found.begin(), found.end(), [](const mu::ImportedFile& a, const mu::ImportedFile& b) {
            return mu::natural_less(mu::path_to_utf8(a.path), mu::path_to_utf8(b.path));
        });
        for (auto& file : found) sources.push_back(std::move(file.path));
    }
    return sources;
}

int run(const std::vector<fs::path>& arguments) {
    if (arguments.size() < 2) {
        print(stdout, "用法: musicunlock <输出目录> <文件或目录> [更多文件…]");
        return 2;
    }
    try {
        const fs::path& directory = arguments[0];
        std::error_code error;
        fs::create_directories(directory, error);
        if (error && !fs::is_directory(directory)) {
            print(stderr, "无法创建输出目录: " + mu::path_to_utf8(directory));
            return 1;
        }
        const mu::Bytes mask = load_mask();
        const auto sources = collect_sources({arguments.begin() + 1, arguments.end()});
        if (sources.empty()) throw mu::DecodeError(mu::DecodeError::Kind::invalid, "未找到支持的文件");
        int failures = 0;
        for (const auto& source : sources) {
            try {
                const auto output = mu::convert_file(source, directory, mu::NamingPolicy::original, mask);
                print(stdout, "完成: " + mu::path_to_utf8(output.path));
            } catch (const std::exception& failure) {
                ++failures;
                print(stderr, "失败: " + mu::path_to_utf8(source.filename()) + ": " + failure.what());
            }
        }
        return failures == 0 ? 0 : 1;
    } catch (const std::exception& failure) {
        print(stderr, failure.what());
        return 1;
    }
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
#else
int main(int argc, char** argv) {
#endif
    std::vector<fs::path> arguments;
    for (int i = 1; i < argc; ++i) arguments.emplace_back(static_cast<const NativeChar*>(argv[i]));
    return run(arguments);
}
