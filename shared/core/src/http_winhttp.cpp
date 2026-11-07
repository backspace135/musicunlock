// WinHTTP transport: system proxy, TLS from the OS, no cookie storage, no cache. Each call is one
// request; cookies are passed explicitly and Set-Cookie headers are returned to the caller.
#include <windows.h>
#include <winhttp.h>

#include <memory>

#include "musicunlock/music_api.h"
#include "musicunlock/text.h"

namespace mu {

namespace {

constexpr int timeout_ms = 25'000;
// Slightly above the parser's limit so oversized responses are still reported as such.
constexpr std::size_t read_limit = 20'000'001;

struct HandleCloser {
    void operator()(HINTERNET handle) const { WinHttpCloseHandle(handle); }
};
using Handle = std::unique_ptr<void, HandleCloser>;

[[noreturn]] void fail() { throw LyricError("网络连接失败，请检查网络或代理设置后重试。"); }

std::wstring wide(std::string_view text) {
    const std::u16string units = utf8_to_utf16(text);
    return std::wstring(units.begin(), units.end());
}

std::string narrow(const wchar_t* text, std::size_t length) {
    return utf16_to_utf8(std::u16string_view(reinterpret_cast<const char16_t*>(text), length));
}

std::vector<std::string> set_cookie_headers(HINTERNET request) {
    std::vector<std::string> headers;
    for (DWORD index = 0;;) {
        DWORD size = 0;
        const DWORD before = index;
        WinHttpQueryHeaders(request, WINHTTP_QUERY_SET_COOKIE, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size, &index);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0) break;
        std::wstring value(size / sizeof(wchar_t), L'\0');
        index = before;
        if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_SET_COOKIE, WINHTTP_HEADER_NAME_BY_INDEX, value.data(), &size,
                                 &index)) {
            break;
        }
        headers.push_back(narrow(value.data(), size / sizeof(wchar_t)));
    }
    return headers;
}

class Session {
public:
    Session() {
        session_.reset(WinHttpOpen(L"MusicUnlock", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                   WINHTTP_NO_PROXY_BYPASS, 0));
        if (!session_) {
            // Automatic proxy needs Windows 8.1; fall back to the WinHTTP default.
            session_.reset(WinHttpOpen(L"MusicUnlock", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                       WINHTTP_NO_PROXY_BYPASS, 0));
        }
        if (!session_) return;
        WinHttpSetTimeouts(session_.get(), timeout_ms, timeout_ms, timeout_ms, timeout_ms);
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        if (!WinHttpSetOption(session_.get(), WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols)) {
            protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;  // TLS 1.3 is not available before Windows 11
            WinHttpSetOption(session_.get(), WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
        }
    }

    HttpResponse perform(const HttpRequest& request) const {
        if (!session_) fail();
        const std::wstring url = wide(request.url);
        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof parts;
        parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) {
            throw LyricError("只支持 HTTPS 请求。");
        }
        const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        const std::wstring target = std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) +
                                    std::wstring(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        Handle connection(WinHttpConnect(session_.get(), host.c_str(), parts.nPort, 0));
        if (!connection) fail();
        Handle handle(WinHttpOpenRequest(connection.get(), wide(request.method).c_str(), target.c_str(), nullptr,
                                         WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH));
        if (!handle) fail();
        DWORD disable = WINHTTP_DISABLE_COOKIES;
        WinHttpSetOption(handle.get(), WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof disable);
        DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
        WinHttpSetOption(handle.get(), WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof decompression);
        std::wstring headers;
        for (const auto& [name, value] : request.headers) headers += wide(name) + L": " + wide(value) + L"\r\n";
        if (!WinHttpSendRequest(handle.get(), headers.c_str(), static_cast<DWORD>(-1L),
                                request.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(request.body.data()),
                                static_cast<DWORD>(request.body.size()), static_cast<DWORD>(request.body.size()), 0) ||
            !WinHttpReceiveResponse(handle.get(), nullptr)) {
            fail();
        }
        HttpResponse response;
        DWORD status = 0;
        DWORD size = sizeof status;
        WinHttpQueryHeaders(handle.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        response.status = static_cast<int>(status);
        response.set_cookies = set_cookie_headers(handle.get());
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(handle.get(), &available)) fail();
            if (available == 0) break;
            const std::size_t offset = response.body.size();
            response.body.resize(offset + available);
            DWORD read = 0;
            if (!WinHttpReadData(handle.get(), response.body.data() + offset, available, &read)) fail();
            response.body.resize(offset + read);
            if (response.body.size() >= read_limit) break;
        }
        return response;
    }

private:
    Handle session_;
};

}  // namespace

HttpTransport make_system_transport() {
    auto session = std::make_shared<Session>();
    return [session](const HttpRequest& request) { return session->perform(request); };
}

}  // namespace mu
