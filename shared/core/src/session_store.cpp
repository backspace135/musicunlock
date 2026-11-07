#include "musicunlock/session.h"

#include <fstream>
#include <iterator>
#include <system_error>

namespace mu {

SessionStore::SessionStore(std::filesystem::path directory, SessionCipher cipher)
    : directory_(std::move(directory)), cipher_(std::move(cipher)) {}

std::filesystem::path SessionStore::file(MusicProvider provider) const {
    return directory_ / (std::string(provider_id(provider)) + ".bin");
}

std::optional<std::vector<Cookie>> SessionStore::load(MusicProvider provider, std::int64_t now) const {
    std::ifstream input(file(provider), std::ios::binary);
    if (!input) return std::nullopt;
    const std::string sealed{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const auto json = cipher_.open(sealed);
    if (!json) return std::nullopt;
    return accept_session(cookies_from_json(*json), provider, now);
}

void SessionStore::save(MusicProvider provider, const std::vector<Cookie>& cookies) const {
    const std::string sealed = cipher_.seal(cookies_to_json(cookies));
    std::error_code error;
    std::filesystem::create_directories(directory_, error);
    const auto target = file(provider);
    auto temporary = target;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(sealed.data(), static_cast<std::streamsize>(sealed.size()));
        if (!output) {
            output.close();
            std::filesystem::remove(temporary, error);
            throw LyricError("无法写入登录会话文件。");
        }
    }
    std::filesystem::rename(temporary, target, error);
    if (error) {
        const int code = error.value();
        std::filesystem::remove(temporary, error);
        throw LyricError("无法保存登录会话（错误 " + std::to_string(code) + "）。");
    }
}

bool SessionStore::remove(MusicProvider provider) const {
    std::error_code error;
    std::filesystem::remove(file(provider), error);
    return !std::filesystem::exists(file(provider), error);
}

}  // namespace mu
