#include "musicunlock/session.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <tuple>

#include <nlohmann/json.hpp>

#include "musicunlock/text.h"

namespace mu {

namespace {

std::string_view trim_ascii(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
    return text;
}

std::int64_t days_from_civil(std::int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

/// hms-time: 1*2DIGIT ":" 1*2DIGIT ":" 1*2DIGIT
std::optional<std::tuple<int, int, int>> parse_time(std::string_view token) {
    int parts[3] = {};
    std::size_t at = 0;
    for (int i = 0; i < 3; ++i) {
        std::size_t digits = 0;
        while (at < token.size() && digits < 3 && token[at] >= '0' && token[at] <= '9') {
            parts[i] = parts[i] * 10 + (token[at++] - '0');
            ++digits;
        }
        if (digits == 0 || digits > 2) return std::nullopt;
        if (i < 2 && (at >= token.size() || token[at++] != ':')) return std::nullopt;
    }
    return std::tuple{parts[0], parts[1], parts[2]};
}

/// Cookie dates per RFC 6265 §5.1.1: "Wed, 21 Oct 2026 07:28:00 GMT" and the dashed variants.
std::optional<std::int64_t> parse_cookie_date(std::string_view text) {
    constexpr std::array<std::string_view, 12> months = {"jan", "feb", "mar", "apr", "may", "jun",
                                                         "jul", "aug", "sep", "oct", "nov", "dec"};
    std::optional<int> day, month, year, hour, minute, second;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && (text[i] == ' ' || text[i] == ',' || text[i] == '-' || text[i] == '\t')) ++i;
        std::size_t end = i;
        while (end < text.size() && text[end] != ' ' && text[end] != ',' && text[end] != '-' && text[end] != '\t') ++end;
        const std::string token = ascii_lower(text.substr(i, end - i));
        i = end;
        if (token.empty()) continue;
        if (!hour) {
            if (auto time = parse_time(token)) {
                std::tie(hour, minute, second) = *time;
                continue;
            }
        }
        if (!month) {
            const auto found = std::find(months.begin(), months.end(), std::string_view(token).substr(0, 3));
            if (found != months.end() && token.size() >= 3) {
                month = static_cast<int>(found - months.begin()) + 1;
                continue;
            }
        }
        if (token.size() <= 4 && std::all_of(token.begin(), token.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            const int value = std::stoi(token);
            if (!day && token.size() <= 2) day = value;
            else if (!year && (token.size() == 2 || token.size() == 4)) year = token.size() == 2 ? value + (value < 70 ? 2000 : 1900) : value;
        }
    }
    if (!day || !month || !year || !hour || *day < 1 || *day > 31 || *hour > 23 || *minute > 59 || *second > 59) {
        return std::nullopt;
    }
    return days_from_civil(*year, static_cast<unsigned>(*month), static_cast<unsigned>(*day)) * 86400 +
           *hour * 3600 + *minute * 60 + *second;
}

}  // namespace

std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::vector<Cookie> parse_set_cookies(const std::vector<std::string>& headers, std::string_view request_host,
                                      std::int64_t now) {
    std::vector<Cookie> cookies;
    for (const std::string& header : headers) {
        std::string_view rest(header);
        const std::size_t first = rest.find(';');
        const std::string_view pair = trim_ascii(rest.substr(0, first));
        const std::size_t equals = pair.find('=');
        if (equals == std::string_view::npos || equals == 0) continue;
        Cookie cookie;
        cookie.name = std::string(trim_ascii(pair.substr(0, equals)));
        cookie.value = std::string(trim_ascii(pair.substr(equals + 1)));
        cookie.domain = ascii_lower(request_host);
        std::optional<std::int64_t> max_age;
        rest = first == std::string_view::npos ? std::string_view() : rest.substr(first + 1);
        while (!rest.empty()) {
            const std::size_t next = rest.find(';');
            const std::string_view attribute = trim_ascii(rest.substr(0, next));
            rest = next == std::string_view::npos ? std::string_view() : rest.substr(next + 1);
            const std::size_t eq = attribute.find('=');
            const std::string key = ascii_lower(trim_ascii(attribute.substr(0, eq)));
            const std::string_view value = eq == std::string_view::npos ? std::string_view() : trim_ascii(attribute.substr(eq + 1));
            if (key == "domain" && !value.empty()) {
                cookie.domain = ascii_lower(value);
                if (cookie.domain.front() != '.') cookie.domain.insert(cookie.domain.begin(), '.');
            } else if (key == "path" && !value.empty()) {
                cookie.path = std::string(value);
            } else if (key == "secure") {
                cookie.secure = true;
            } else if (key == "expires") {
                if (auto date = parse_cookie_date(value)) cookie.expires = date;
            } else if (key == "max-age") {
                try {
                    max_age = std::stoll(std::string(value));
                } catch (...) {
                }
            }
        }
        if (max_age) cookie.expires = *max_age <= 0 ? now - 1 : now + *max_age;
        cookies.push_back(std::move(cookie));
    }
    return cookies;
}

bool has_music_session(const std::vector<Cookie>& cookies, MusicProvider provider, std::int64_t now) {
    std::vector<std::string_view> names;
    for (const auto& cookie : cookies) {
        if (provider_owns(provider, cookie.domain) && !cookie.value.empty() && !cookie.expired(now)) {
            names.push_back(cookie.name);
        }
    }
    auto has = [&](std::initializer_list<std::string_view> wanted) {
        return std::any_of(names.begin(), names.end(),
                           [&](std::string_view name) { return std::find(wanted.begin(), wanted.end(), name) != wanted.end(); });
    };
    if (provider == MusicProvider::netease) return has({"MUSIC_U"});
    return has({"qqmusic_key", "qm_keyst", "qqmusic_session"}) && has({"uin", "qqmusic_uin", "wxuin"});
}

std::optional<std::vector<Cookie>> accept_session(const std::vector<Cookie>& received, MusicProvider provider,
                                                  std::int64_t now) {
    std::vector<Cookie> valid;
    for (const auto& cookie : received) {
        if (provider_owns(provider, cookie.domain) && !cookie.expired(now)) valid.push_back(cookie);
    }
    if (!has_music_session(valid, provider, now)) return std::nullopt;
    return valid;
}

std::string cookie_header(const std::vector<Cookie>& cookies, std::int64_t now) {
    std::string header;
    for (const auto& cookie : cookies) {
        if (cookie.expired(now)) continue;
        if (!header.empty()) header += "; ";
        header += cookie.name + "=" + cookie.value;
    }
    return header;
}

std::string cookies_to_json(const std::vector<Cookie>& cookies) {
    auto array = nlohmann::json::array();
    for (const auto& cookie : cookies) {
        array.push_back({{"name", cookie.name},
                         {"value", cookie.value},
                         {"domain", cookie.domain},
                         {"path", cookie.path},
                         {"expires", cookie.expires ? nlohmann::json(*cookie.expires) : nlohmann::json(nullptr)},
                         {"secure", cookie.secure}});
    }
    return array.dump();
}

std::vector<Cookie> cookies_from_json(std::string_view text) {
    std::vector<Cookie> cookies;
    const auto json = nlohmann::json::parse(text, nullptr, false);
    if (!json.is_array()) return cookies;
    for (const auto& item : json) {
        if (!item.is_object()) continue;
        auto text_field = [&](const char* key) {
            auto it = item.find(key);
            return it != item.end() && it->is_string() ? it->get<std::string>() : std::string();
        };
        Cookie cookie{text_field("name"), text_field("value"), text_field("domain"), text_field("path"), std::nullopt, false};
        if (cookie.name.empty() || cookie.domain.empty()) continue;
        if (cookie.path.empty()) cookie.path = "/";
        if (auto it = item.find("expires"); it != item.end() && it->is_number_integer()) cookie.expires = it->get<std::int64_t>();
        if (auto it = item.find("secure"); it != item.end() && it->is_boolean()) cookie.secure = it->get<bool>();
        cookies.push_back(std::move(cookie));
    }
    return cookies;
}

}  // namespace mu
