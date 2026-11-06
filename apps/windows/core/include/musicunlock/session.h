#pragma once
// Music platform login sessions: cookie parsing, validity rules and the serialized form that the
// app protects with DPAPI. Rules match MusicAccounts.swift on macOS.

#include <cstdint>
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

}  // namespace mu
