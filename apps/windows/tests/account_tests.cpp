// Account login tests: the NetEase QR code and its polling loop, run against a scripted transport.
#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "musicunlock/music_api.h"
#include "musicunlock/qr_login.h"
#include "musicunlock/session.h"
#include "test_support.h"

using mu::QrLoginState;

namespace {

/// Answers QR status polls in order with {"code":N}; the last answer repeats.
struct QrScript {
    struct Answer {
        int code;
        std::vector<std::string> cookies;
    };
    std::vector<Answer> answers;
    std::shared_ptr<int> polls = std::make_shared<int>(0);
    bool offline = false;

    mu::MusicApi api() const {
        return mu::MusicApi(
            [answers = answers, polls = polls, offline = offline](const mu::HttpRequest& request) {
                if (offline) throw std::runtime_error("offline");
                if (request.url.find("qrcode/client/login") == std::string::npos) throw std::runtime_error("unexpected");
                const Answer& answer = answers[std::min<std::size_t>(static_cast<std::size_t>((*polls)++), answers.size() - 1)];
                return mu::HttpResponse{200, R"({"code":)" + std::to_string(answer.code) + "}", answer.cookies};
            },
            [] { return std::int64_t{0}; });
    }
};

struct PollResult {
    mu::QrLoginStep final;
    std::vector<QrLoginState> reported;
    int waits = 0;
};

PollResult poll(const QrScript& script, int attempts = 90) {
    PollResult result;
    result.final = mu::poll_qr_login(
        script.api(), "KEY", {}, [&] { ++result.waits; },
        [&](const mu::QrLoginStep& step) { result.reported.push_back(step.state); }, attempts);
    return result;
}

}  // namespace

TEST(qr_matrix_has_finder_patterns) {
    const auto matrix = mu::qr_matrix("https://music.163.com/login?codekey=0f5d2f4e-6a7b-4c3d-9e8f-102938475610");
    CHECK(matrix.size >= 21 && (matrix.size - 17) % 4 == 0);
    CHECK_EQ(matrix.modules.size(), static_cast<std::size_t>(matrix.size * matrix.size), "module count");
    // Finder patterns: dark 7x7 ring, light ring, dark 3x3 centre, in three corners.
    for (const auto [ox, oy] : {std::pair{0, 0}, std::pair{matrix.size - 7, 0}, std::pair{0, matrix.size - 7}}) {
        CHECK(matrix.dark(ox, oy) && matrix.dark(ox + 6, oy) && matrix.dark(ox, oy + 6) && matrix.dark(ox + 6, oy + 6));
        CHECK(!matrix.dark(ox + 1, oy + 1) && !matrix.dark(ox + 5, oy + 5));
        CHECK(matrix.dark(ox + 2, oy + 2) && matrix.dark(ox + 3, oy + 3) && matrix.dark(ox + 4, oy + 4));
    }
}

TEST(qr_path_data_merges_runs) {
    mu::QrMatrix matrix{3, {true, true, false, false, false, false, true, false, true}};
    CHECK_EQ(mu::qr_path_data(matrix), std::string("M0,0h2v1h-2zM0,2h1v1h-1zM2,2h1v1h-1z"), "runs");
    CHECK(mu::qr_path_data(mu::QrMatrix{2, {false, false, false, false}}).empty());

    // Every dark module is covered exactly once.
    const auto real = mu::qr_matrix("https://music.163.com/login?codekey=KEY");
    const std::string data = mu::qr_path_data(real);
    std::size_t covered = 0;
    for (std::size_t at = data.find('h'); at != std::string::npos; at = data.find('h', at + 1)) {
        if (data[at + 1] != '-') covered += std::stoul(data.substr(at + 1));
    }
    std::size_t dark = 0;
    for (const bool module : real.modules) dark += module;
    CHECK_EQ(covered, dark, "covered modules");
}

TEST(qr_status_codes) {
    CHECK(mu::qr_login_state(800) == QrLoginState::expired);
    CHECK(mu::qr_login_state(801) == QrLoginState::waiting);
    CHECK(mu::qr_login_state(802) == QrLoginState::scanned);
    CHECK(mu::qr_login_state(803) == QrLoginState::confirmed);
    CHECK(mu::qr_login_state(200) == QrLoginState::failed);
    CHECK_EQ(mu::qr_login_message(QrLoginState::scanned), std::string_view("已扫码，请在手机上确认登录"), "scanned text");
}

TEST(qr_poll_until_confirmed) {
    QrScript script;
    script.answers = {{801, {}}, {801, {}}, {802, {}}, {801, {}}, {802, {}},
                      {803, {"MUSIC_U=secret; Domain=.music.163.com; Max-Age=86400", "__csrf=c; Domain=.music.163.com",
                             "evil=1; Domain=.example.com"}}};
    const auto result = poll(script);
    CHECK(result.final.state == QrLoginState::confirmed);
    CHECK_EQ(result.waits, 6, "one wait before each poll");
    CHECK((result.reported == std::vector{QrLoginState::scanned, QrLoginState::waiting, QrLoginState::scanned}));
    CHECK(mu::has_music_session(result.final.session, mu::MusicProvider::netease, mu::unix_now()));
    for (const auto& cookie : result.final.session) CHECK(mu::provider_owns(mu::MusicProvider::netease, cookie.domain));
}

TEST(qr_poll_endings) {
    QrScript expired;
    expired.answers = {{801, {}}, {800, {}}};
    auto result = poll(expired);
    CHECK(result.final.state == QrLoginState::expired);
    CHECK_EQ(result.final.message, std::string("二维码已过期，请刷新。"), "expired text");

    QrScript waiting;
    waiting.answers = {{801, {}}};
    result = poll(waiting, 3);
    CHECK(result.final.state == QrLoginState::expired && result.waits == 3);
    CHECK_EQ(result.final.message, std::string("二维码已超时，请刷新。"), "timeout text");

    QrScript no_session;
    no_session.answers = {{803, {"NMTID=x; Domain=.music.163.com"}}};
    result = poll(no_session);
    CHECK(result.final.state == QrLoginState::failed && result.final.session.empty());
    CHECK_EQ(result.final.message, std::string("平台没有返回有效登录，请使用官方登录页重试。"), "no session text");

    QrScript odd;
    odd.answers = {{200, {}}};
    CHECK(poll(odd).final.state == QrLoginState::failed);

    QrScript refused;
    refused.answers = {{8821, {}}};
    result = poll(refused);
    CHECK(result.final.state == QrLoginState::failed && result.final.message.find("8821") != std::string::npos);

    QrScript offline;
    offline.answers = {{801, {}}};
    offline.offline = true;
    CHECK(poll(offline).final.state == QrLoginState::failed);
}

TEST(qr_poll_cancels) {
    QrScript script;
    script.answers = {{801, {}}};
    const mu::CancelToken cancel;
    int waits = 0;
    bool cancelled = false;
    try {
        mu::poll_qr_login(script.api(), "KEY", cancel, [&] {
            if (++waits == 3) cancel.cancel();
        }, {});
    } catch (const mu::Cancelled&) {
        cancelled = true;
    }
    CHECK(cancelled);
    CHECK_EQ(*script.polls, 2, "no poll after cancelling");
}
