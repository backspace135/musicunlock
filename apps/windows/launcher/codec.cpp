#include "codec.h"

#include <windows.h>
#include <bcrypt.h>
#include <compressapi.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <exception>
#include <mutex>
#include <thread>

namespace launcher {

Sha256 sha256(std::span<const std::uint8_t> data) {
    Sha256 digest{};
    // BCryptHash takes a ULONG length; hash larger inputs incrementally.
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &hash, nullptr, 0, nullptr, 0, 0) != 0) {
        throw std::runtime_error("BCryptCreateHash failed");
    }
    for (std::size_t done = 0; done < data.size();) {
        const auto part = static_cast<ULONG>(std::min<std::size_t>(data.size() - done, 1u << 30));
        BCryptHashData(hash, const_cast<PUCHAR>(data.data() + done), part, 0);
        done += part;
    }
    BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
    BCryptDestroyHash(hash);
    return digest;
}

unsigned worker_count(std::size_t jobs) {
    const unsigned processors = std::max(1u, std::thread::hardware_concurrency());
    return static_cast<unsigned>(std::max<std::size_t>(1, std::min<std::size_t>(processors, jobs)));
}

void parallel_for(std::size_t count, const std::function<void(std::size_t)>& body) {
    std::atomic<std::size_t> next{0};
    std::exception_ptr failure;
    std::mutex failure_lock;
    auto work = [&] {
        for (std::size_t i; (i = next++) < count;) {
            try {
                body(i);
            } catch (...) {
                const std::lock_guard guard(failure_lock);
                if (!failure) failure = std::current_exception();
                next = count;  // stop handing out work
            }
        }
    };
    std::vector<std::thread> threads;
    for (unsigned i = 1; i < worker_count(count); ++i) threads.emplace_back(work);
    work();
    for (auto& thread : threads) thread.join();
    if (failure) std::rethrow_exception(failure);
}

std::vector<EncodedChunk> compress(Algorithm algorithm, std::span<const std::uint8_t> data, std::uint32_t chunk_size) {
    if (chunk_size == 0 || chunk_size > max_chunk_size) throw std::invalid_argument("chunk size");
    std::vector<EncodedChunk> chunks((data.size() + chunk_size - 1) / chunk_size);
    parallel_for(chunks.size(), [&](std::size_t i) {
        const auto input = data.subspan(i * chunk_size, std::min<std::size_t>(chunk_size, data.size() - i * chunk_size));
        auto& chunk = chunks[i];
        chunk.size = static_cast<std::uint32_t>(input.size());
        if (algorithm == Algorithm::stored) {
            chunk.compressed.assign(input.begin(), input.end());
            return;
        }
        COMPRESSOR_HANDLE compressor = nullptr;
        if (!CreateCompressor(static_cast<DWORD>(algorithm), nullptr, &compressor)) {
            throw std::runtime_error("CreateCompressor failed: " + std::to_string(GetLastError()));
        }
        SIZE_T needed = 0;
        Compress(compressor, input.data(), input.size(), nullptr, 0, &needed);
        chunk.compressed.resize(needed);
        SIZE_T written = 0;
        const BOOL ok = Compress(compressor, input.data(), input.size(), chunk.compressed.data(), needed, &written);
        const DWORD error = GetLastError();
        CloseCompressor(compressor);
        if (!ok) throw std::runtime_error("Compress failed: " + std::to_string(error));
        chunk.compressed.resize(written);
    });
    return chunks;
}

Buffer decompress(const Payload& payload) {
    Buffer out{std::unique_ptr<std::uint8_t[]>(new std::uint8_t[std::max<std::uint64_t>(payload.data_size, 1)]),
               static_cast<std::size_t>(payload.data_size)};
    // Chunk offsets in the compressed and uncompressed data.
    std::vector<std::pair<std::size_t, std::size_t>> offsets;
    std::size_t in = 0, at = 0;
    for (const auto& chunk : payload.chunks) {
        offsets.emplace_back(in, at);
        in += chunk.compressed_size;
        at += chunk.size;
    }
    parallel_for(payload.chunks.size(), [&](std::size_t i) {
        const auto& chunk = payload.chunks[i];
        const auto input = payload.compressed.subspan(offsets[i].first, chunk.compressed_size);
        std::uint8_t* target = out.bytes.get() + offsets[i].second;
        if (payload.algorithm == Algorithm::stored) {
            std::memcpy(target, input.data(), input.size());
            return;
        }
        DECOMPRESSOR_HANDLE decompressor = nullptr;
        if (!CreateDecompressor(static_cast<DWORD>(payload.algorithm), nullptr, &decompressor)) {
            throw std::runtime_error("CreateDecompressor failed: " + std::to_string(GetLastError()));
        }
        SIZE_T written = 0;
        const BOOL ok = Decompress(decompressor, input.data(), input.size(), target, chunk.size, &written);
        CloseDecompressor(decompressor);
        if (!ok || written != chunk.size) throw FormatError("compressed data is damaged");
    });
    return out;
}

Bytes pack(std::vector<SourceFile> files, Algorithm algorithm, std::uint32_t chunk_size) {
    auto key = [](const SourceFile& file) {
        const auto dot = file.path.rfind('.');
        const auto slash = file.path.rfind('/');
        const bool has_extension = dot != std::string::npos && (slash == std::string::npos || dot > slash);
        return std::pair(fold_case(has_extension ? file.path.substr(dot) : ""), fold_case(file.path));
    };
    std::sort(files.begin(), files.end(), [&](const SourceFile& a, const SourceFile& b) { return key(a) < key(b); });
    std::vector<FileEntry> entries(files.size());
    std::size_t total = 0;
    for (const auto& file : files) total += file.data.size();
    Bytes data;
    data.reserve(total);
    for (std::size_t i = 0; i < files.size(); ++i) {
        entries[i].path = files[i].path;
        entries[i].offset = data.size();
        entries[i].size = files[i].data.size();
        data.insert(data.end(), files[i].data.begin(), files[i].data.end());
    }
    parallel_for(files.size(), [&](std::size_t i) { entries[i].sha256 = sha256(files[i].data); });
    return encode(algorithm, entries, compress(algorithm, data, chunk_size), sha256);
}

}  // namespace launcher
