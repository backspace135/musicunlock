#include "musicunlock/qr_login.h"

#include <qrcodegen/qrcodegen.hpp>

namespace mu {

QrMatrix qr_matrix(std::string_view text) {
    const auto code = qrcodegen::QrCode::encodeText(std::string(text).c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
    QrMatrix matrix;
    matrix.size = code.getSize();
    matrix.modules.reserve(static_cast<std::size_t>(matrix.size) * matrix.size);
    for (int y = 0; y < matrix.size; ++y) {
        for (int x = 0; x < matrix.size; ++x) matrix.modules.push_back(code.getModule(x, y));
    }
    return matrix;
}

std::string qr_path_data(const QrMatrix& matrix) {
    std::string data;
    for (int y = 0; y < matrix.size; ++y) {
        for (int x = 0; x < matrix.size;) {
            if (!matrix.dark(x, y)) {
                ++x;
                continue;
            }
            const int start = x;
            while (x < matrix.size && matrix.dark(x, y)) ++x;
            const std::string run = std::to_string(x - start);
            data += "M" + std::to_string(start) + "," + std::to_string(y) + "h" + run + "v1h-" + run + "z";
        }
    }
    return data;
}

QrLoginState qr_login_state(int code) {
    switch (code) {
        case 800: return QrLoginState::expired;
        case 801: return QrLoginState::waiting;
        case 802: return QrLoginState::scanned;
        case 803: return QrLoginState::confirmed;
        default: return QrLoginState::failed;
    }
}

std::string_view qr_login_message(QrLoginState state) {
    switch (state) {
        case QrLoginState::loading: return "正在生成二维码…";
        case QrLoginState::waiting: return "请使用网易云音乐扫码";
        case QrLoginState::scanned: return "已扫码，请在手机上确认登录";
        case QrLoginState::confirmed: return "已确认登录，正在保存会话…";
        case QrLoginState::expired: return "二维码已过期，请刷新。";
        case QrLoginState::failed: break;
    }
    return "平台登录状态异常，请刷新或使用官方登录页。";
}

QrLoginStep poll_qr_login(const MusicApi& api, const std::string& key, const CancelToken& cancel,
                          const std::function<void()>& wait, const std::function<void(const QrLoginStep&)>& report,
                          int attempts) {
    auto step = [](QrLoginState state, std::string_view message = {}) {
        return QrLoginStep{state, std::string(message.empty() ? qr_login_message(state) : message), {}};
    };
    // The dialog shows "waiting" as soon as the code is on screen.
    QrLoginState shown = QrLoginState::waiting;
    for (int attempt = 0; attempt < attempts; ++attempt) {
        if (wait) wait();
        cancel.check();
        QrStatus status;
        try {
            status = api.check_qr_code(key);
        } catch (const Cancelled&) {
            throw;
        } catch (const LyricError& error) {
            return step(QrLoginState::failed, error.what());
        } catch (const std::exception&) {
            return step(QrLoginState::failed, "无法连接网易云音乐，请检查网络后刷新二维码。");
        }
        cancel.check();
        const QrLoginState state = qr_login_state(status.code);
        switch (state) {
            case QrLoginState::confirmed: {
                auto session = accept_session(status.cookies, MusicProvider::netease, unix_now());
                if (!session) return step(QrLoginState::failed, "平台没有返回有效登录，请使用官方登录页重试。");
                QrLoginStep done = step(state);
                done.session = std::move(*session);
                return done;
            }
            case QrLoginState::expired:
            case QrLoginState::failed: return step(state);
            default:
                if (state != shown && report) report(step(state));
                shown = state;
        }
    }
    return step(QrLoginState::expired, "二维码已超时，请刷新。");
}

}  // namespace mu
