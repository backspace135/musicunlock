#pragma once
// Music platform login sessions: cookie parsing, validity rules and the serialized form that the
// app protects with DPAPI. Rules match MusicAccounts.swift on macOS.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "musicunlock/lyrics.h"

namespace mu {

struct Cookie {
    std::string name;
    std::string value;
    std::string domain;
    std::string path = "/";
    std::optional<std::int64_t> expires;  // Unix seconds; nullopt for session cookies
    bool secure = false;

    bool expired(std::int64_t now) const { return expires && *expires <= now; }
    bool operator==(const Cookie&) const = default;
};

std::int64_t unix_now();

/// Parses Set-Cookie header values. Cookies without Domain belong to `request_host`.
std::vector<Cookie> parse_set_cookies(const std::vector<std::string>& headers, std::string_view request_host,
                                      std::int64_t now);

/// True only for a real music session: NetEase MUSIC_U, or a QQ music key plus an account id.
bool has_music_session(const std::vector<Cookie>& cookies, MusicProvider provider, std::int64_t now);

/// The provider's unexpired cookies when they form a music session, otherwise nullopt.
std::optional<std::vector<Cookie>> accept_session(const std::vector<Cookie>& received, MusicProvider provider,
                                                  std::int64_t now);

/// "name=value; name=value" for unexpired cookies.
std::string cookie_header(const std::vector<Cookie>& cookies, std::int64_t now);

std::string cookies_to_json(const std::vector<Cookie>& cookies);
std::vector<Cookie> cookies_from_json(std::string_view json);

/// Encryption for saved sessions at rest. `open` returns nullopt for data it cannot decrypt or authenticate.
struct SessionCipher {
    std::function<std::string(std::string_view)> seal;
    std::function<std::optional<std::string>(std::string_view)> open;
};

/// DPAPI (CryptProtectData) for the current Windows user, with app-specific entropy. Windows only.
SessionCipher user_session_cipher();

/// One file per provider, <directory>/<provider id>.bin, holding the sealed cookies_to_json of its session.
/// There is no plain-text form and no way to type cookies in: sessions only come from a platform login.
class SessionStore {
public:
    SessionStore(std::filesystem::path directory, SessionCipher cipher);

    std::filesystem::path file(MusicProvider provider) const;
    /// The provider's saved cookies while they still form a session at `now`; nullopt when there is no file,
    /// it cannot be decrypted, or the session has expired. The platform may still end a session early.
    std::optional<std::vector<Cookie>> load(MusicProvider provider, std::int64_t now) const;
    /// Replaces the saved session (written beside it, then swapped in). Throws LyricError on failure.
    void save(MusicProvider provider, const std::vector<Cookie>& cookies) const;
    /// Deletes the saved session; true when no file remains.
    bool remove(MusicProvider provider) const;

private:
    std::filesystem::path directory_;
    SessionCipher cipher_;
};

}  // namespace mu
