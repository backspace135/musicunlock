// AES through Windows CNG (bcrypt.dll), a system library.
#include <windows.h>
#include <bcrypt.h>

#include "detail.h"

namespace mu::detail {

std::optional<Bytes> aes128_ecb(AesMode mode, ByteSpan input, ByteSpan key) {
    if (key.size() != 16) return std::nullopt;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE handle = nullptr;
    std::optional<Bytes> result;
    auto mutable_bytes = [](ByteSpan bytes) { return const_cast<PUCHAR>(bytes.data()); };
    if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0)) &&
        BCRYPT_SUCCESS(BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE,
                                         reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_ECB)),
                                         sizeof(BCRYPT_CHAIN_MODE_ECB), 0)) &&
        BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(algorithm, &handle, nullptr, 0, mutable_bytes(key),
                                                  static_cast<ULONG>(key.size()), 0))) {
        Bytes output(input.size() + 16);
        ULONG written = 0;
        const NTSTATUS status =
            mode == AesMode::encrypt
                ? BCryptEncrypt(handle, mutable_bytes(input), static_cast<ULONG>(input.size()), nullptr, nullptr, 0,
                                output.data(), static_cast<ULONG>(output.size()), &written, BCRYPT_BLOCK_PADDING)
                : BCryptDecrypt(handle, mutable_bytes(input), static_cast<ULONG>(input.size()), nullptr, nullptr, 0,
                                output.data(), static_cast<ULONG>(output.size()), &written, BCRYPT_BLOCK_PADDING);
        if (BCRYPT_SUCCESS(status)) {
            output.resize(written);
            result = std::move(output);
        }
    }
    if (handle) BCryptDestroyKey(handle);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return result;
}

}  // namespace mu::detail
