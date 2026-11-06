// Saved sessions are sealed with DPAPI for the signed-in Windows user (macOS keeps them in the Keychain).
#include <windows.h>

#include <dpapi.h>

#include "musicunlock/session.h"

namespace mu {

namespace {

// App-specific entropy, so a plain CryptUnprotectData call by another program does not open the files.
// It is not a secret; DPAPI's protection is the user's credentials.
constexpr std::string_view entropy_text = "MusicUnlock music session v1";

DATA_BLOB blob(std::string_view data) {
    return {static_cast<DWORD>(data.size()), reinterpret_cast<BYTE*>(const_cast<char*>(data.data()))};
}

}  // namespace

SessionCipher user_session_cipher() {
    SessionCipher cipher;
    cipher.seal = [](std::string_view plain) {
        DATA_BLOB input = blob(plain), entropy = blob(entropy_text), output{};
        if (!CryptProtectData(&input, L"MusicUnlock session", &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                              &output)) {
            throw LyricError("Windows 数据保护无法加密登录会话（错误 " + std::to_string(GetLastError()) + "）。");
        }
        std::string sealed(reinterpret_cast<const char*>(output.pbData), output.cbData);
        LocalFree(output.pbData);
        return sealed;
    };
    cipher.open = [](std::string_view sealed) -> std::optional<std::string> {
        DATA_BLOB input = blob(sealed), entropy = blob(entropy_text), output{};
        if (!CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
            return std::nullopt;
        }
        std::string plain(reinterpret_cast<const char*>(output.pbData), output.cbData);
        SecureZeroMemory(output.pbData, output.cbData);
        LocalFree(output.pbData);
        return plain;
    };
    return cipher;
}

}  // namespace mu
