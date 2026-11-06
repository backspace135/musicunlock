#pragma once
// SHA-256 (CNG) and chunked compression with the system Compression API (cabinet.dll). Windows only.

#include <memory>

#include "payload.h"

namespace launcher {

Sha256 sha256(std::span<const std::uint8_t> data);

/// Uninitialised byte buffer (the decompressed app folder is tens of MB; zeroing it first is wasted time).
struct Buffer {
    std::unique_ptr<std::uint8_t[]> bytes;
    std::size_t size = 0;
    std::span<const std::uint8_t> view() const { return {bytes.get(), size}; }
};

/// Splits `data` into chunks of `chunk_size` bytes and compresses them in parallel.
std::vector<EncodedChunk> compress(Algorithm algorithm, std::span<const std::uint8_t> data, std::uint32_t chunk_size);
/// Decompresses every chunk of `payload` in parallel. Throws FormatError when the data is damaged.
Buffer decompress(const Payload& payload);

struct SourceFile {
    std::string path;  // relative, '/' separated
    Bytes data;
};
/// A complete payload for `files`. Files are laid out by extension so similar ones share a chunk.
Bytes pack(std::vector<SourceFile> files, Algorithm algorithm, std::uint32_t chunk_size);

/// Worker threads for `jobs` independent jobs: one per logical processor, at most `jobs`.
unsigned worker_count(std::size_t jobs);
/// Runs body(0) ... body(count - 1) on worker_count(count) threads; rethrows the first exception.
void parallel_for(std::size_t count, const std::function<void(std::size_t)>& body);

}  // namespace launcher
