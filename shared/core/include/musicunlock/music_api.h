#pragma once
// First-party NetEase / QQ Music requests only: no external API service, proxy or shared cookie jar.
// Ported from apps/macos/Sources/MusicUnlockCore/MusicAPI.swift. The transport is injected so
// request building and response parsing are tested without the network.

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "musicunlock/lyrics.h"
#include "musicunlock/session.h"

namespace mu {

struct HttpRequest {
    std::string url;
    std::string method = "GET";
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
};

struct HttpResponse {
    int status = 0;
    std::string body;
    std::vector<std::string> set_cookies;
};

/// Performs one request; throws LyricError when the network is unreachable.
using HttpTransport = std::function<HttpResponse(const HttpRequest&)>;

/// WinHTTP with system proxy settings, no cookie storage and no cache (Windows only).
HttpTransport make_system_transport();

/// Thrown when a CancelToken is triggered between requests.
struct Cancelled : std::exception {
    const char* what() const noexcept override { return "已取消"; }
};

class CancelToken {
public:
    CancelToken() : flag_(std::make_shared<std::atomic<bool>>(false)) {}
    void cancel() const { flag_->store(true); }
    bool cancelled() const { return flag_->load(); }
    void check() const {
        if (cancelled()) throw Cancelled();
    }

private:
    std::shared_ptr<std::atomic<bool>> flag_;
};

struct SearchPage {
    std::vector<LyricSong> songs;
    std::vector<MusicCollection> collections;
    bool has_more = false;
};

/// A NetEase QR login, made the way the official Windows client does it: eapi requests that identify as
/// the PC client, with the same device id in every request and in the code's chainId. A bare web-style
/// request still gets 801, but NetEase refuses the login after the phone scans it (code 8821).
struct QrTicket {
    std::string key;
    std::string url;     // the QR code's content
    std::string device;  // random per ticket, sent with each status query
};

struct QrStatus {
    int code = 0;  // 800 expired, 801 waiting, 802 scanned, 803 confirmed
    std::vector<Cookie> cookies;
};

class MusicApi {
public:
    explicit MusicApi(HttpTransport transport, std::function<std::int64_t()> clock_ms = {});

    SearchPage search(std::string_view query, MusicProvider provider, SearchKind kind, int page,
                      const std::string& cookie, const CancelToken& cancel = {}) const;
    std::vector<LyricSong> resolve(const MusicLink& link, const std::string& cookie, const CancelToken& cancel = {}) const;
    LyricDocument lyrics(const LyricSong& song, const std::string& cookie, const CancelToken& cancel = {}) const;
    /// Both throw LyricError with a hint towards the official login page when NetEase refuses the login.
    QrTicket create_qr_code() const;
    QrStatus check_qr_code(const QrTicket& ticket) const;
    std::optional<std::string> account_name(MusicProvider provider, const std::string& cookie) const;

    /// QQ's g_tk: DJB hash of the session key over UTF-16 units.
    static std::uint32_t qq_token(std::string_view cookie);
    static std::string decode_entities(std::string_view text);

private:
    HttpTransport transport_;
    std::function<std::int64_t()> clock_ms_;
};

}  // namespace mu
