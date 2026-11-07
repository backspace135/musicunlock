#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mu {

using Bytes = std::vector<std::uint8_t>;
using ByteSpan = std::span<const std::uint8_t>;

/// Errors carry a user-facing UTF-8 message, matching the macOS app wording.
class DecodeError : public std::runtime_error {
public:
    enum class Kind { invalid, unsupported };
    DecodeError(Kind kind, const std::string& message) : std::runtime_error(message), kind_(kind) {}
    Kind kind() const noexcept { return kind_; }

private:
    Kind kind_;
};

struct AudioMetadata {
    std::string title;
    std::string artist;
    std::string album;
    std::optional<Bytes> cover;

    bool empty() const { return title.empty() && artist.empty() && album.empty() && !cover; }
};

struct DecodedAudio {
    Bytes data;
    std::string file_extension;
    AudioMetadata metadata;
};

/// Native ports of the MIT-licensed Unlock Music container algorithms.
/// Audio is unwrapped without lossy transcoding or network access.
const std::set<std::string, std::less<>>& qq_extensions();
const std::set<std::string, std::less<>>& supported_extensions();
bool is_supported_extension(std::string_view ext);
std::string platform_for(std::string_view ext);

/// An empty `kgm_mask` means the KGM table is unavailable.
DecodedAudio decode_audio(ByteSpan data, std::string_view file_extension, ByteSpan kgm_mask = {});
std::optional<std::string> sniff_audio(ByteSpan bytes);

}  // namespace mu
