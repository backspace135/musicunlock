// Portable AES-128 (FIPS-197) for platforms whose crypto library native code cannot reach, such as Android.
// Table-based and not constant-time: it only unwraps NCM keys and seals NetEase eapi requests, never secrets.
#include "detail.h"

namespace mu::detail {

namespace {

constexpr std::size_t block_size = 16;
constexpr std::size_t rounds = 10;
using Block = std::array<std::uint8_t, block_size>;
using RoundKeys = std::array<std::uint8_t, block_size * (rounds + 1)>;

constexpr std::uint8_t xtime(std::uint8_t value) {
    return static_cast<std::uint8_t>(value << 1 ^ (value & 0x80 ? 0x1b : 0));
}

constexpr std::uint8_t multiply(std::uint8_t a, std::uint8_t b) {
    std::uint8_t product = 0;
    for (; b; b >>= 1, a = xtime(a)) {
        if (b & 1) product ^= a;
    }
    return product;
}

constexpr std::uint8_t rotl(std::uint8_t value, int shift) {
    return static_cast<std::uint8_t>(value << shift | value >> (8 - shift));
}

/// Walks the multiplicative group with generator 3: p runs over 3^i and q over its inverse 3^-i.
constexpr std::array<std::uint8_t, 256> make_sbox() {
    std::array<std::uint8_t, 256> box{};
    std::uint8_t p = 1;
    std::uint8_t q = 1;
    do {
        p = static_cast<std::uint8_t>(p ^ xtime(p));
        q = static_cast<std::uint8_t>(q ^ q << 1);
        q = static_cast<std::uint8_t>(q ^ q << 2);
        q = static_cast<std::uint8_t>(q ^ q << 4);
        if (q & 0x80) q ^= 0x09;
        box[p] = static_cast<std::uint8_t>(q ^ rotl(q, 1) ^ rotl(q, 2) ^ rotl(q, 3) ^ rotl(q, 4) ^ 0x63);
    } while (p != 1);
    box[0] = 0x63;
    return box;
}

constexpr auto sbox = make_sbox();

constexpr std::array<std::uint8_t, 256> make_inverse_sbox() {
    std::array<std::uint8_t, 256> box{};
    for (std::size_t i = 0; i < 256; ++i) box[sbox[i]] = static_cast<std::uint8_t>(i);
    return box;
}

constexpr auto inverse_sbox = make_inverse_sbox();

static_assert(sbox[0x00] == 0x63 && sbox[0x01] == 0x7c && sbox[0x53] == 0xed && sbox[0xff] == 0x16);

RoundKeys expand_key(ByteSpan key) {
    RoundKeys keys{};
    std::copy(key.begin(), key.end(), keys.begin());
    std::uint8_t rcon = 1;
    for (std::size_t i = block_size; i < keys.size(); i += 4) {
        std::array<std::uint8_t, 4> word{keys[i - 4], keys[i - 3], keys[i - 2], keys[i - 1]};
        if (i % block_size == 0) {
            word = {static_cast<std::uint8_t>(sbox[word[1]] ^ rcon), sbox[word[2]], sbox[word[3]], sbox[word[0]]};
            rcon = xtime(rcon);
        }
        for (std::size_t j = 0; j < 4; ++j) keys[i + j] = keys[i + j - block_size] ^ word[j];
    }
    return keys;
}

void add_round_key(Block& state, const RoundKeys& keys, std::size_t round) {
    for (std::size_t i = 0; i < block_size; ++i) state[i] ^= keys[round * block_size + i];
}

// The state is column-major: byte r + 4c is row r of column c. Row r rotates left by r columns.
void shift_rows(Block& state, bool inverse) {
    const Block old = state;
    for (std::size_t r = 1; r < 4; ++r) {
        for (std::size_t c = 0; c < 4; ++c) {
            const std::size_t from = inverse ? (c + 4 - r) % 4 : (c + r) % 4;
            state[r + 4 * c] = old[r + 4 * from];
        }
    }
}

void mix_columns(Block& state, const std::array<std::uint8_t, 4>& row) {
    for (std::size_t c = 0; c < 4; ++c) {
        const std::array<std::uint8_t, 4> a{state[4 * c], state[4 * c + 1], state[4 * c + 2], state[4 * c + 3]};
        for (std::size_t r = 0; r < 4; ++r) {
            state[4 * c + r] = multiply(a[0], row[(4 - r) % 4]) ^ multiply(a[1], row[(5 - r) % 4]) ^
                               multiply(a[2], row[(6 - r) % 4]) ^ multiply(a[3], row[(7 - r) % 4]);
        }
    }
}

constexpr std::array<std::uint8_t, 4> mix_row{2, 3, 1, 1};
constexpr std::array<std::uint8_t, 4> inverse_mix_row{14, 11, 13, 9};

void encrypt_block(Block& state, const RoundKeys& keys) {
    add_round_key(state, keys, 0);
    for (std::size_t round = 1; round <= rounds; ++round) {
        for (auto& b : state) b = sbox[b];
        shift_rows(state, false);
        if (round != rounds) mix_columns(state, mix_row);
        add_round_key(state, keys, round);
    }
}

void decrypt_block(Block& state, const RoundKeys& keys) {
    add_round_key(state, keys, rounds);
    for (std::size_t round = rounds; round-- > 0;) {
        shift_rows(state, true);
        for (auto& b : state) b = inverse_sbox[b];
        add_round_key(state, keys, round);
        if (round != 0) mix_columns(state, inverse_mix_row);
    }
}

}  // namespace

std::optional<Bytes> aes128_ecb(AesMode mode, ByteSpan input, ByteSpan key) {
    if (key.size() != block_size) return std::nullopt;
    const RoundKeys keys = expand_key(key);
    Bytes output(input.begin(), input.end());
    if (mode == AesMode::encrypt) {
        // PKCS#7 always pads, so whole-block input gains a full block of 0x10.
        const std::size_t padding = block_size - input.size() % block_size;
        output.insert(output.end(), padding, static_cast<std::uint8_t>(padding));
    } else if (output.empty() || output.size() % block_size != 0) {
        return std::nullopt;
    }
    for (std::size_t offset = 0; offset < output.size(); offset += block_size) {
        Block state;
        std::copy_n(output.begin() + static_cast<std::ptrdiff_t>(offset), block_size, state.begin());
        mode == AesMode::encrypt ? encrypt_block(state, keys) : decrypt_block(state, keys);
        std::copy(state.begin(), state.end(), output.begin() + static_cast<std::ptrdiff_t>(offset));
    }
    if (mode == AesMode::decrypt) {
        const std::size_t padding = output.back();
        if (padding == 0 || padding > block_size ||
            !std::all_of(output.end() - static_cast<std::ptrdiff_t>(padding), output.end(),
                         [&](std::uint8_t b) { return b == padding; })) {
            return std::nullopt;
        }
        output.resize(output.size() - padding);
    }
    return output;
}

}  // namespace mu::detail
