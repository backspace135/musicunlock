#pragma once
// Payload embedded in the single-exe launcher: the app folder as a manifest plus compressed chunks.
// Portable (no Windows APIs), so the format and its validation are also tested on the development Mac.
//
//   header    64 bytes: "MUPAYLD\1", algorithm, chunk count, file count, manifest size, data size,
//             SHA-256 of the manifest (the runtime version id)
//   chunks    per chunk: compressed size, uncompressed size (u32 each)
//   manifest  per file: offset and size in the uncompressed data (u64), SHA-256, UTF-8 path ('/' separated)
//   data      the compressed chunks back to back; uncompressed they form one blob holding every file
//
// All integers are little-endian. The manifest stays uncompressed, so a warm start can verify the
// extracted folder without decompressing anything.

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace launcher {

using Bytes = std::vector<std::uint8_t>;
using Sha256 = std::array<std::uint8_t, 32>;

/// Compression API algorithm ids (COMPRESS_ALGORITHM_*); stored is for tests.
enum class Algorithm : std::uint32_t { stored = 0, xpress_huff = 4, lzms = 5 };

struct FileEntry {
    std::string path;  // relative, '/' separated, validated by valid_path
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    Sha256 sha256{};
};

struct Chunk {
    std::uint32_t compressed_size = 0;
    std::uint32_t size = 0;
};

/// A parsed payload. The spans point into the buffer given to parse().
struct Payload {
    Algorithm algorithm = Algorithm::stored;
    Sha256 id{};  // SHA-256 of the manifest bytes; the caller checks it (hashing is platform code)
    std::uint64_t data_size = 0;
    std::vector<Chunk> chunks;
    std::vector<FileEntry> files;
    std::span<const std::uint8_t> manifest;
    std::span<const std::uint8_t> compressed;

    /// Name of the runtime folder for this payload: the first 8 bytes of the id in hex.
    std::string version() const;
};

class FormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

constexpr std::size_t header_size = 64;
/// Chunks are compressed independently so the launcher can decompress them in parallel.
constexpr std::uint32_t max_chunk_size = 256u << 20;

/// Parses and validates the structure (bounds, chunk sizes, paths, duplicates). Throws FormatError.
Payload parse(std::span<const std::uint8_t> bytes);

struct EncodedChunk {
    std::uint32_t size = 0;  // uncompressed
    Bytes compressed;
};
using HashFunction = std::function<Sha256(std::span<const std::uint8_t>)>;

/// The whole payload; `sha256` hashes the manifest into the id (hashing is platform code).
Bytes encode(Algorithm algorithm, const std::vector<FileEntry>& files, const std::vector<EncodedChunk>& chunks,
             const HashFunction& sha256);

/// Stamp kept in an extracted folder: each file's last-write time right after extraction, in manifest order.
/// A warm start compares sizes and times against it instead of hashing every file.
Bytes encode_stamp(const Sha256& id, const std::vector<std::uint64_t>& write_times);
/// The write times, or nothing when the stamp belongs to another payload or is damaged.
std::optional<std::vector<std::uint64_t>> decode_stamp(std::span<const std::uint8_t> stamp, const Sha256& id,
                                                       std::size_t files);

/// Relative path whose components are safe Windows file names: no "..", drive, stream or reserved characters.
bool valid_path(std::string_view path);
/// ASCII lower case, for the case-insensitive comparisons Windows file systems make.
std::string fold_case(std::string_view text);
/// True for names produced by Payload::version() (16 lower-case hex digits).
bool is_version_name(std::string_view name);
std::string hex(std::span<const std::uint8_t> bytes);

/// The part of a Windows command line after the program name, unchanged, so arguments reach the app exactly
/// as the launcher received them. Follows CommandLineToArgvW's rules for the first argument.
std::wstring_view arguments_after_program(std::wstring_view command_line);

}  // namespace launcher
