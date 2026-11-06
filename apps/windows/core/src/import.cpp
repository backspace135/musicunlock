#include "musicunlock/import.h"

#include <algorithm>
#include <set>

#include "musicunlock/audio.h"
#include "musicunlock/text.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace mu {

namespace {

bool is_hidden(const fs::directory_entry& entry) {
    const std::string name = path_to_utf8(entry.path().filename());
    if (!name.empty() && name.front() == '.') return true;
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(entry.path().c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_HIDDEN);
#else
    return false;
#endif
}

bool has_supported_extension(const fs::path& path) {
    std::string ext = path_to_utf8(path.extension());
    if (!ext.empty()) ext.erase(0, 1);
    return is_supported_extension(ascii_lower(ext));
}

/// Case-folded on Windows, where paths differing only in case name the same file.
std::string identity(const fs::path& path) {
    const std::string text = path_to_utf8(path.lexically_normal());
#ifdef _WIN32
    std::u16string units = utf8_to_utf16(text);
    CharLowerBuffW(reinterpret_cast<wchar_t*>(units.data()), static_cast<DWORD>(units.size()));
    return utf16_to_utf8(units);
#else
    return text;
#endif
}

}  // namespace

std::vector<ImportedFile> find_audio_files(const fs::path& directory, std::size_t* skipped) {
    std::vector<ImportedFile> found;
    std::error_code error;
    fs::recursive_directory_iterator it(directory, fs::directory_options::skip_permission_denied, error);
    for (; !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
        if (is_hidden(*it)) {
            if (it->is_directory(error)) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(error)) continue;
        if (!has_supported_extension(it->path())) {
            if (skipped) ++*skipped;
            continue;
        }
        found.push_back({it->path(), it->file_size(error)});
    }
    return found;
}

ImportScan scan_import(const std::vector<fs::path>& inputs) {
    ImportScan scan;
    std::vector<ImportedFile> collected;
    for (const auto& input : inputs) {
        std::error_code error;
        const fs::path path = fs::absolute(input, error).lexically_normal();
        if (fs::is_directory(path, error)) {
            auto files = find_audio_files(path, &scan.skipped);
            collected.insert(collected.end(), files.begin(), files.end());
        } else if (fs::is_regular_file(path, error) && has_supported_extension(path)) {
            collected.push_back({path, fs::file_size(path, error)});
        } else {
            ++scan.skipped;
        }
    }
    std::set<std::string> seen;
    for (auto& file : collected) {
        if (seen.insert(identity(file.path)).second) scan.files.push_back(std::move(file));
    }
    std::sort(scan.files.begin(), scan.files.end(), [](const ImportedFile& a, const ImportedFile& b) {
        return natural_less(path_to_utf8(a.path), path_to_utf8(b.path));
    });
    return scan;
}

bool natural_less(std::string_view a, std::string_view b) {
    auto digit = [](char c) { return c >= '0' && c <= '9'; };
    auto fold = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; };
    std::size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (digit(a[i]) && digit(b[j])) {
            std::size_t ie = i, je = j;
            while (ie < a.size() && digit(a[ie])) ++ie;
            while (je < b.size() && digit(b[je])) ++je;
            std::string_view x = a.substr(i, ie - i), y = b.substr(j, je - j);
            while (x.size() > 1 && x.front() == '0') x.remove_prefix(1);
            while (y.size() > 1 && y.front() == '0') y.remove_prefix(1);
            if (x.size() != y.size()) return x.size() < y.size();
            if (x != y) return x < y;
            i = ie, j = je;
            continue;
        }
        const char x = fold(a[i]), y = fold(b[j]);
        if (x != y) return static_cast<unsigned char>(x) < static_cast<unsigned char>(y);
        ++i, ++j;
    }
    if ((a.size() - i) != (b.size() - j)) return a.size() - i < b.size() - j;
    return a < b;  // stable tie-break for case-only differences
}

}  // namespace mu
