// macOS backend so the portable core and its tests also run on the development Mac.
#include <CommonCrypto/CommonCryptor.h>

#include "detail.h"

namespace mu::detail {

std::optional<Bytes> aes128_ecb(AesMode mode, ByteSpan input, ByteSpan key) {
    if (key.size() != kCCKeySizeAES128) return std::nullopt;
    Bytes output(input.size() + kCCBlockSizeAES128);
    std::size_t written = 0;
    const CCCryptorStatus status =
        CCCrypt(mode == AesMode::encrypt ? kCCEncrypt : kCCDecrypt, kCCAlgorithmAES,
                kCCOptionECBMode | kCCOptionPKCS7Padding, key.data(), key.size(), nullptr, input.data(),
                input.size(), output.data(), output.size(), &written);
    if (status != kCCSuccess) return std::nullopt;
    output.resize(written);
    return output;
}

}  // namespace mu::detail
