// launcher_pack: embeds an app folder into a copy of the launcher, producing the single MusicUnlock.exe.
// Run through scripts/pack.py.
//
//   launcher_pack <app folder> <launcher.exe> <output.exe> [--algorithm lzms|xpress_huff] [--chunk-mb N]
//   launcher_pack --compare <app folder>   size and decompression time per algorithm and chunk size
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "codec.h"

namespace fs = std::filesystem;
using namespace launcher;

namespace {

std::string utf8(const std::wstring& text) {
    std::string out(WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), static_cast<int>(out.size()),
                        nullptr, nullptr);
    return out;
}

std::vector<SourceFile> read_folder(const fs::path& folder) {
    std::vector<SourceFile> files;
    for (const auto& item : fs::recursive_directory_iterator(folder)) {
        if (!item.is_regular_file()) continue;
        std::string path = utf8(item.path().lexically_relative(folder).generic_wstring());
        if (!valid_path(path)) throw std::runtime_error("unsupported file name: " + path);
        std::ifstream in(item.path(), std::ios::binary);
        Bytes data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        files.push_back({std::move(path), std::move(data)});
    }
    return files;
}

Algorithm algorithm_named(const std::string& name) {
    if (name == "lzms") return Algorithm::lzms;
    if (name == "xpress_huff") return Algorithm::xpress_huff;
    if (name == "stored") return Algorithm::stored;
    throw std::runtime_error("unknown algorithm: " + name);
}

const char* algorithm_name(Algorithm algorithm) {
    switch (algorithm) {
    case Algorithm::lzms: return "lzms";
    case Algorithm::xpress_huff: return "xpress_huff";
    case Algorithm::stored: break;
    }
    return "stored";
}

double ms_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void compare(const std::vector<SourceFile>& files) {
    std::size_t total = 0;
    for (const auto& file : files) total += file.data.size();
    // MB are 10^6 bytes, as in stage_app.py. A 64 MiB chunk holds the whole folder, so that row is single-threaded.
    std::printf("%zu files, %.1f MB, %u threads\n%-12s %9s %8s %11s %14s\n", files.size(), total / 1e6,
                worker_count(1024), "algorithm", "chunk MiB", "size MB", "compress s", "decompress ms");
    for (const auto algorithm : {Algorithm::xpress_huff, Algorithm::lzms}) {
        for (const std::uint32_t chunk_mb : {1u, 2u, 4u, 16u, 64u}) {
            const auto start = std::chrono::steady_clock::now();
            const Bytes bytes = pack(files, algorithm, chunk_mb << 20);
            const double compress_ms = ms_since(start);
            const Payload payload = parse(bytes);
            double best = 1e300;
            for (int run = 0; run < 5; ++run) {
                const auto begin = std::chrono::steady_clock::now();
                const Buffer data = decompress(payload);
                best = std::min(best, ms_since(begin));
            }
            std::printf("%-12s %9u %8.2f %11.1f %14.1f\n", algorithm_name(algorithm), chunk_mb, bytes.size() / 1e6,
                        compress_ms / 1000, best);
        }
    }
}

void embed(const Bytes& payload, const fs::path& stub, const fs::path& output) {
    fs::create_directories(output.parent_path());
    fs::copy_file(stub, output, fs::copy_options::overwrite_existing);
    HANDLE update = BeginUpdateResourceW(output.c_str(), FALSE);
    if (!update) throw std::runtime_error("BeginUpdateResource failed: " + std::to_string(GetLastError()));
    if (!UpdateResourceW(update, RT_RCDATA, L"PAYLOAD", MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                         const_cast<std::uint8_t*>(payload.data()), static_cast<DWORD>(payload.size()))) {
        const DWORD error = GetLastError();
        EndUpdateResourceW(update, TRUE);
        throw std::runtime_error("UpdateResource failed: " + std::to_string(error));
    }
    if (!EndUpdateResourceW(update, FALSE)) throw std::runtime_error("EndUpdateResource failed: " + std::to_string(GetLastError()));

    // Read it back the way the launcher will.
    HMODULE module = LoadLibraryExW(output.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    HRSRC resource = module ? FindResourceW(module, L"PAYLOAD", RT_RCDATA) : nullptr;
    const void* data = resource ? LockResource(LoadResource(module, resource)) : nullptr;
    const bool same = data && SizeofResource(module, resource) == payload.size() &&
                      std::equal(payload.begin(), payload.end(), static_cast<const std::uint8_t*>(data));
    if (module) FreeLibrary(module);
    if (!same) throw std::runtime_error("the embedded payload does not read back");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        std::vector<std::wstring> positional;
        Algorithm algorithm = Algorithm::lzms;
        std::uint32_t chunk_mb = 16;
        bool comparing = false;
        for (int i = 1; i < argc; ++i) {
            const std::wstring argument = argv[i];
            if (argument == L"--compare") {
                comparing = true;
            } else if (argument == L"--algorithm" && i + 1 < argc) {
                algorithm = algorithm_named(utf8(argv[++i]));
            } else if (argument == L"--chunk-mb" && i + 1 < argc) {
                chunk_mb = static_cast<std::uint32_t>(std::stoul(argv[++i]));
            } else {
                positional.push_back(argument);
            }
        }
        if (comparing && positional.size() == 1) {
            compare(read_folder(positional[0]));
            return 0;
        }
        if (comparing || positional.size() != 3) {
            std::fprintf(stderr, "usage: launcher_pack <app folder> <launcher.exe> <output.exe> [--algorithm lzms|xpress_huff]"
                                 " [--chunk-mb N]\n       launcher_pack --compare <app folder>\n");
            return 2;
        }
        const auto files = read_folder(positional[0]);
        if (std::none_of(files.begin(), files.end(), [](const SourceFile& file) { return file.path == "MusicUnlock.exe"; })) {
            throw std::runtime_error("MusicUnlock.exe is missing from the app folder");
        }
        std::size_t total = 0;
        for (const auto& file : files) total += file.data.size();
        const auto start = std::chrono::steady_clock::now();
        const Bytes payload = pack(files, algorithm, chunk_mb << 20);
        const double compress_ms = ms_since(start);
        embed(payload, positional[1], positional[2]);
        const Payload parsed = parse(payload);
        std::printf("%zu files, %.1f MB -> %.1f MB payload (%s, %u MiB chunks, %.1f s), version %s\n", files.size(),
                    total / 1e6, payload.size() / 1e6, algorithm_name(algorithm), chunk_mb, compress_ms / 1000,
                    parsed.version().c_str());
        std::printf("%s: %.1f MB\n", utf8(positional[2]).c_str(), fs::file_size(positional[2]) / 1e6);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "launcher_pack: %s\n", error.what());
        return 1;
    }
}
