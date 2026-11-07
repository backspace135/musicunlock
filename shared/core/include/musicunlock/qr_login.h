#pragma once
// NetEase QR login, as in MusicLoginSheet.pollQRCode on macOS: the QR code as vector path data, what each
// status code means, and the polling loop that ends in an accepted session.

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "musicunlock/music_api.h"
#include "musicunlock/session.h"

namespace mu {

/// Square module grid of a QR code with medium error correction; true is a dark module.
struct QrMatrix {
    int size = 0;
    std::vector<bool> modules;  // row-major, size * size

    bool dark(int x, int y) const { return modules[static_cast<std::size_t>(y) * size + x]; }
};

QrMatrix qr_matrix(std::string_view text);

/// Path mini-language (XAML Geometry / SVG) for the dark modules, one unit per module and no quiet zone:
/// "M0,0h7v1h-7z..." with one rectangle per horizontal run.
std::string qr_path_data(const QrMatrix& matrix);

enum class QrLoginState { loading, waiting, scanned, confirmed, expired, failed };

/// 800 expired, 801 waiting, 802 scanned, 803 confirmed; any other code is failed.
QrLoginState qr_login_state(int code);
/// The default status line for a state (macOS wording).
std::string_view qr_login_message(QrLoginState state);

/// What to show when NetEase refuses a QR login, e.g. its risk control's 8821 "请切换其他登录方式或升级新版本再试":
/// the platform's words alone leave nothing to try, so this points to the official login page.
std::string qr_refusal_message(std::int64_t code, std::string_view platform_message);

struct QrLoginStep {
    QrLoginState state = QrLoginState::loading;
    std::string message;
    std::vector<Cookie> session;  // accepted cookies, only when confirmed
};

/// Polls `ticket` up to `attempts` times. `wait` runs before each poll (2 s in the app) and may throw Cancelled;
/// `report` receives each change between waiting and scanned. Returns the final step: confirmed with the accepted session,
/// expired (code 800 or out of attempts) or failed with a message. Throws Cancelled when `cancel` triggers.
QrLoginStep poll_qr_login(const MusicApi& api, const QrTicket& ticket, const CancelToken& cancel,
                          const std::function<void()>& wait, const std::function<void(const QrLoginStep&)>& report,
                          int attempts = 90);

}  // namespace mu
