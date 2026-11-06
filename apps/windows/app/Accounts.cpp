#include "pch.h"

#include "Accounts.h"

#include <chrono>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>

#include "Settings.h"
#include "musicunlock/text.h"

namespace accounts {

namespace {

std::mutex mutex;
std::map<mu::MusicProvider, std::vector<mu::Cookie>> sessions;  // guarded by mutex
bool loaded = false;

mu::SessionStore store() { return mu::SessionStore(settings::data_directory() / L"sessions", mu::user_session_cipher()); }

std::string title(mu::MusicProvider provider) { return std::string(mu::provider_title(provider)); }

std::filesystem::path purge_marker(mu::MusicProvider provider) {
    auto marker = webview_folder(provider);
    marker += L".purge";
    return marker;
}

bool remove_webview_data(mu::MusicProvider provider) {
    std::error_code error;
    std::filesystem::remove_all(webview_folder(provider), error);
    if (std::filesystem::exists(webview_folder(provider), error)) return false;
    std::filesystem::remove(purge_marker(provider), error);
    return true;
}

/// Reads the saved sessions once. Caller holds `mutex`.
void load_saved() {
    if (loaded) return;
    loaded = true;
    // Layout renders must not touch the user's credentials.
    if (GetEnvironmentVariableW(L"MUSICUNLOCK_RENDER_DIR", nullptr, 0)) return;
    const auto saved = store();
    for (auto provider : mu::all_providers) {
        if (auto cookies = saved.load(provider, mu::unix_now())) sessions[provider] = std::move(*cookies);
        clear_pending_webview_data(provider);
    }
}

/// The provider's cookies when they still form a session. Caller holds `mutex`.
const std::vector<mu::Cookie>* current(mu::MusicProvider provider, std::int64_t now) {
    load_saved();
    const auto it = sessions.find(provider);
    return it != sessions.end() && mu::has_music_session(it->second, provider, now) ? &it->second : nullptr;
}

}  // namespace

std::string cookie_header(mu::MusicProvider provider) {
    std::lock_guard lock(mutex);
    const auto now = mu::unix_now();
    const auto* cookies = current(provider, now);
    return cookies ? mu::cookie_header(*cookies, now) : std::string();
}

bool signed_in(mu::MusicProvider provider) {
    std::lock_guard lock(mutex);
    return current(provider, mu::unix_now()) != nullptr;
}

std::optional<std::string> accept(mu::MusicProvider provider, const std::vector<mu::Cookie>& received) {
    auto session = mu::accept_session(received, provider, mu::unix_now());
    if (!session) return std::nullopt;
    std::lock_guard lock(mutex);
    load_saved();
    sessions[provider] = *session;
    try {
        store().save(provider, *session);
    } catch (const mu::LyricError& error) {
        return "已登录" + title(provider) + "，本次可用；保存登录失败：" + error.what();
    } catch (const std::exception&) {
        return "已登录" + title(provider) + "，本次可用；保存登录失败。";
    }
    return title(provider) + "登录成功，会话已加密保存在这台电脑上。";
}

std::string logout(mu::MusicProvider provider) {
    bool removed = false;
    std::filesystem::path file;
    {
        std::lock_guard lock(mutex);
        load_saved();
        sessions.erase(provider);
        const auto saved = store();
        removed = saved.remove(provider);
        file = saved.file(provider);
    }
    if (!remove_webview_data(provider)) {
        // The login page's browser process can take a moment to exit. Keep trying for a while, and remember
        // the folder so it is cleared before the next login page or at the next start.
        std::ofstream(purge_marker(provider)) << "logout\n";
        std::thread([provider] {
            using namespace std::chrono_literals;
            for (int attempt = 0; attempt < 20 && !remove_webview_data(provider); ++attempt) {
                std::this_thread::sleep_for(500ms);
            }
        }).detach();
    }
    if (!removed) return "已退出本次会话；会话文件删除失败，可手动删除 " + mu::path_to_utf8(file) + "。";
    return "已退出" + title(provider) + "。";
}

std::filesystem::path webview_folder(mu::MusicProvider provider) {
    return settings::data_directory() / L"WebView2" / mu::utf8_to_path(mu::provider_id(provider));
}

bool clear_pending_webview_data(mu::MusicProvider provider) {
    std::error_code error;
    return !std::filesystem::exists(purge_marker(provider), error) || remove_webview_data(provider);
}

}  // namespace accounts
