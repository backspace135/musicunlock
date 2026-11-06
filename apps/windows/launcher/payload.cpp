#include "payload.h"

#include <algorithm>
#include <cstring>
#include <unordered_set>

namespace launcher {
namespace {

constexpr char magic[8] = {'M', 'U', 'P', 'A', 'Y', 'L', 'D', 1};
constexpr std::uint8_t stamp_magic[8] = {'M', 'U', 'S', 'T', 'A', 'M', 'P', 1};
constexpr std::uint32_t max_files = 100'000;
constexpr std::uint64_t max_data_size = 4ull << 30;

class Reader {
public:
    Reader(std::span<const std::uint8_t> bytes, const char* what) : bytes_(bytes), what_(what) {}

    std::span<const std::uint8_t> take(std::size_t count) {
        if (count > bytes_.size() - position_) throw FormatError(std::string(what_) + " is truncated");
        auto part = bytes_.subspan(position_, count);
        position_ += count;
        return part;
    }
    template <typename T>
    T integer() {
        const auto part = take(sizeof(T));
        T value = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i) value |= static_cast<T>(static_cast<T>(part[i]) << (8 * i));
        return value;
    }
    std::size_t position() const { return position_; }
    bool done() const { return position_ == bytes_.size(); }

private:
    std::span<const std::uint8_t> bytes_;
    const char* what_;
    std::size_t position_ = 0;
};

template <typename T>
void put(Bytes& out, T value) {
    for (std::size_t i = 0; i < sizeof(T); ++i) out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}

bool known(Algorithm algorithm) {
    return algorithm == Algorithm::stored || algorithm == Algorithm::xpress_huff || algorithm == Algorithm::lzms;
}

bool reserved_device(std::string_view component) {
    const std::string stem = fold_case(component.substr(0, component.find('.')));
    static constexpr std::string_view names[] = {"con", "prn", "aux", "nul"};
    if (std::find(std::begin(names), std::end(names), stem) != std::end(names)) return true;
    return stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '0' && stem[3] <= '9';
}

bool valid_component(std::string_view component) {
    if (component.empty() || component.size() > 255 || component == "." || component == "..") return false;
    if (component.back() == '.' || component.back() == ' ') return false;
    for (const char c : component) {
        if (static_cast<unsigned char>(c) < 0x20 || std::strchr("<>:\"\\|?*", c)) return false;
    }
    return !reserved_device(component);
}

}  // namespace

std::string Payload::version() const { return hex(std::span(id).first(8)); }

Payload parse(std::span<const std::uint8_t> bytes) {
    Reader header(bytes, "payload");
    if (!std::equal(std::begin(magic), std::end(magic), header.take(sizeof magic).begin(),
                    [](char a, std::uint8_t b) { return static_cast<std::uint8_t>(a) == b; })) {
        throw FormatError("not a MusicUnlock payload");
    }
    Payload payload;
    payload.algorithm = static_cast<Algorithm>(header.integer<std::uint32_t>());
    const auto chunk_count = header.integer<std::uint32_t>();
    const auto file_count = header.integer<std::uint32_t>();
    const auto manifest_size = header.integer<std::uint32_t>();
    payload.data_size = header.integer<std::uint64_t>();
    const auto id = header.take(payload.id.size());
    std::copy(id.begin(), id.end(), payload.id.begin());
    header.take(header_size - header.position());
    if (!known(payload.algorithm)) throw FormatError("unknown compression algorithm");
    if (file_count > max_files || payload.data_size > max_data_size) throw FormatError("payload too large");

    std::uint64_t total = 0, compressed_total = 0;
    for (std::uint32_t i = 0; i < chunk_count; ++i) {
        Chunk chunk{header.integer<std::uint32_t>(), header.integer<std::uint32_t>()};
        if (chunk.size == 0 || chunk.size > max_chunk_size || chunk.compressed_size == 0) {
            throw FormatError("bad chunk size");
        }
        if (payload.algorithm == Algorithm::stored && chunk.compressed_size != chunk.size) {
            throw FormatError("stored chunk size mismatch");
        }
        total += chunk.size;
        compressed_total += chunk.compressed_size;
        payload.chunks.push_back(chunk);
    }
    if (total != payload.data_size) throw FormatError("chunks do not add up to the data size");

    payload.manifest = header.take(manifest_size);
    Reader manifest(payload.manifest, "manifest");
    std::unordered_set<std::string> seen;
    for (std::uint32_t i = 0; i < file_count; ++i) {
        FileEntry file;
        file.offset = manifest.integer<std::uint64_t>();
        file.size = manifest.integer<std::uint64_t>();
        const auto hash = manifest.take(file.sha256.size());
        std::copy(hash.begin(), hash.end(), file.sha256.begin());
        const auto path = manifest.take(manifest.integer<std::uint16_t>());
        file.path.assign(path.begin(), path.end());
        if (file.offset > payload.data_size || file.size > payload.data_size - file.offset) {
            throw FormatError("file outside the data: " + file.path);
        }
        if (!valid_path(file.path)) throw FormatError("unsafe path: " + file.path);
        if (!seen.insert(fold_case(file.path)).second) throw FormatError("duplicate path: " + file.path);
        payload.files.push_back(std::move(file));
    }
    if (!manifest.done()) throw FormatError("manifest has trailing bytes");
    // A file and a folder with the same name cannot both exist.
    for (const auto& file : payload.files) {
        for (auto slash = file.path.find('/'); slash != std::string::npos; slash = file.path.find('/', slash + 1)) {
            if (seen.contains(fold_case(std::string_view(file.path).substr(0, slash)))) {
                throw FormatError("file used as a folder: " + file.path);
            }
        }
    }
    payload.compressed = header.take(static_cast<std::size_t>(compressed_total));
    if (!header.done()) throw FormatError("payload has trailing bytes");
    return payload;
}

Bytes encode(Algorithm algorithm, const std::vector<FileEntry>& files, const std::vector<EncodedChunk>& chunks,
             const HashFunction& sha256) {
    Bytes manifest;
    for (const auto& file : files) {
        put<std::uint64_t>(manifest, file.offset);
        put<std::uint64_t>(manifest, file.size);
        manifest.insert(manifest.end(), file.sha256.begin(), file.sha256.end());
        put<std::uint16_t>(manifest, static_cast<std::uint16_t>(file.path.size()));
        manifest.insert(manifest.end(), file.path.begin(), file.path.end());
    }
    std::uint64_t data_size = 0;
    for (const auto& chunk : chunks) data_size += chunk.size;
    const Sha256 id = sha256(manifest);

    Bytes out(std::begin(magic), std::end(magic));
    put<std::uint32_t>(out, static_cast<std::uint32_t>(algorithm));
    put<std::uint32_t>(out, static_cast<std::uint32_t>(chunks.size()));
    put<std::uint32_t>(out, static_cast<std::uint32_t>(files.size()));
    put<std::uint32_t>(out, static_cast<std::uint32_t>(manifest.size()));
    put<std::uint64_t>(out, data_size);
    out.insert(out.end(), id.begin(), id.end());
    out.resize(header_size);
    for (const auto& chunk : chunks) {
        put<std::uint32_t>(out, static_cast<std::uint32_t>(chunk.compressed.size()));
        put<std::uint32_t>(out, chunk.size);
    }
    out.insert(out.end(), manifest.begin(), manifest.end());
    for (const auto& chunk : chunks) out.insert(out.end(), chunk.compressed.begin(), chunk.compressed.end());
    return out;
}

Bytes encode_stamp(const Sha256& id, const std::vector<std::uint64_t>& write_times) {
    Bytes out(std::begin(stamp_magic), std::end(stamp_magic));
    out.insert(out.end(), id.begin(), id.end());
    put<std::uint32_t>(out, static_cast<std::uint32_t>(write_times.size()));
    for (const auto time : write_times) put<std::uint64_t>(out, time);
    return out;
}

std::optional<std::vector<std::uint64_t>> decode_stamp(std::span<const std::uint8_t> stamp, const Sha256& id,
                                                       std::size_t files) {
    try {
        Reader reader(stamp, "stamp");
        const auto magic_bytes = reader.take(sizeof stamp_magic);
        const auto stamp_id = reader.take(id.size());
        if (!std::equal(magic_bytes.begin(), magic_bytes.end(), std::begin(stamp_magic)) ||
            !std::equal(stamp_id.begin(), stamp_id.end(), id.begin()) || reader.integer<std::uint32_t>() != files) {
            return std::nullopt;
        }
        std::vector<std::uint64_t> times(files);
        for (auto& time : times) time = reader.integer<std::uint64_t>();
        if (!reader.done()) return std::nullopt;
        return times;
    } catch (const FormatError&) {
        return std::nullopt;
    }
}

bool valid_path(std::string_view path) {
    if (path.empty() || path.size() > 1024) return false;
    for (std::size_t start = 0;;) {
        const auto slash = path.find('/', start);
        if (!valid_component(path.substr(start, slash - start))) return false;
        if (slash == std::string_view::npos) return true;
        start = slash + 1;
    }
}

std::string fold_case(std::string_view text) {
    std::string out(text);
    for (auto& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

bool is_version_name(std::string_view name) {
    return name.size() == 16 &&
           std::all_of(name.begin(), name.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

std::string hex(std::span<const std::uint8_t> bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (const auto b : bytes) {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 15]);
    }
    return out;
}

std::wstring_view arguments_after_program(std::wstring_view command_line) {
    std::size_t end = 0;
    if (!command_line.empty() && command_line[0] == L'"') {
        // A quoted program name ends at the next quote; quotes cannot be escaped in it.
        end = command_line.find(L'"', 1);
        end = end == std::wstring_view::npos ? command_line.size() : end + 1;
    } else {
        end = command_line.find_first_of(L" \t");
        if (end == std::wstring_view::npos) end = command_line.size();
    }
    const auto rest = command_line.find_first_not_of(L" \t", end);
    return rest == std::wstring_view::npos ? std::wstring_view{} : command_line.substr(rest);
}

}  // namespace launcher
