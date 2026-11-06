// Account login tests: the NetEase QR code and its polling loop (scripted transport), and saved sessions.
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "musicunlock/music_api.h"
#include "musicunlock/qr_login.h"
#include "musicunlock/session.h"
#include "musicunlock/text.h"
#include "test_support.h"

namespace fs = std::filesystem;
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

namespace {

/// Stand-in for DPAPI on the Mac: a tag plus XOR, so tests can see that nothing is stored in plain text.
mu::SessionCipher test_cipher() {
    auto flip = [](std::string text) {
        for (char& c : text) c = static_cast<char>(c ^ 0x5A);
        return text;
    };
    return {[flip](std::string_view plain) { return "SEAL" + flip(std::string(plain)); },
            [flip](std::string_view sealed) -> std::optional<std::string> {
                if (!sealed.starts_with("SEAL")) return std::nullopt;
                return flip(std::string(sealed.substr(4)));
            }};
}

fs::path fresh(const char* name) {
    const fs::path folder = mu::utf8_to_path(MU_TEST_SCRATCH) / name;
    fs::remove_all(folder);
    return folder;
}

std::string read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

mu::Cookie cookie(std::string name, std::string value, std::string domain, std::optional<std::int64_t> expires = {}) {
    mu::Cookie cookie;
    cookie.name = std::move(name);
    cookie.value = std::move(value);
    cookie.domain = std::move(domain);
    cookie.expires = expires;
    return cookie;
}

void check_store(const mu::SessionCipher& cipher, const char* folder) {
    const std::int64_t now = 1'800'000'000;
    const fs::path directory = fresh(folder) / mu::utf8_to_path("会话");
    const mu::SessionStore store(directory, cipher);
    CHECK(store.file(mu::MusicProvider::netease) == directory / "netease.bin");
    CHECK(store.file(mu::MusicProvider::qq) == directory / "qq.bin");
    CHECK(!store.load(mu::MusicProvider::netease, now));

    const std::vector<mu::Cookie> netease = {cookie("MUSIC_U", "secret-value", ".music.163.com", now + 3600),
                                             cookie("__csrf", "c", ".music.163.com")};
    store.save(mu::MusicProvider::netease, netease);
    CHECK(read(store.file(mu::MusicProvider::netease)).find("secret-value") == std::string::npos);
    CHECK(!fs::exists(directory / "netease.bin.tmp"));
    CHECK(store.load(mu::MusicProvider::netease, now) == netease);
    CHECK(!store.load(mu::MusicProvider::qq, now));
    // Expiry is decided when loading: an expired MUSIC_U is no session, though the file stays until logout.
    CHECK(!store.load(mu::MusicProvider::netease, now + 3600));
    CHECK(fs::exists(store.file(mu::MusicProvider::netease)));

    // Saving again replaces the session; cookies of another platform are dropped when loading.
    const std::vector<mu::Cookie> qq = {cookie("qm_keyst", "k", ".y.qq.com"), cookie("uin", "1", ".qq.com"),
                                        cookie("MUSIC_U", "x", ".music.163.com")};
    store.save(mu::MusicProvider::qq, {qq[0]});
    CHECK(!store.load(mu::MusicProvider::qq, now));
    store.save(mu::MusicProvider::qq, qq);
    const auto loaded = store.load(mu::MusicProvider::qq, now);
    CHECK(loaded && loaded->size() == 2);

    // A damaged file is no session.
    std::ofstream(store.file(mu::MusicProvider::qq), std::ios::binary | std::ios::trunc) << "garbage";
    CHECK(!store.load(mu::MusicProvider::qq, now));

    CHECK(store.remove(mu::MusicProvider::netease));
    CHECK(!fs::exists(store.file(mu::MusicProvider::netease)));
    CHECK(!store.load(mu::MusicProvider::netease, now));
    CHECK(store.remove(mu::MusicProvider::netease));
}

}  // namespace

TEST(session_store_round_trip) { check_store(test_cipher(), "sessions"); }

TEST(session_store_reports_write_failures) {
    const fs::path blocked = fresh("sessions-blocked");
    fs::create_directories(blocked);
    std::ofstream(blocked / "sessions") << "a file where the folder should be";
    const mu::SessionStore store(blocked / "sessions", test_cipher());
    CHECK_THROWS_AS(store.save(mu::MusicProvider::netease, {cookie("MUSIC_U", "v", ".music.163.com")}), mu::LyricError,
                    "folder is a file");
}

#ifdef _WIN32
TEST(dpapi_session_cipher) {
    const auto cipher = mu::user_session_cipher();
    const std::string plain = R"([{"name":"MUSIC_U","value":"秘密"}])";
    const std::string sealed = cipher.seal(plain);
    CHECK(sealed.find("MUSIC_U") == std::string::npos);
    CHECK_EQ(cipher.open(sealed), std::optional<std::string>(plain), "round trip");
    std::string tampered = sealed;
    tampered[tampered.size() / 2] = static_cast<char>(tampered[tampered.size() / 2] ^ 1);
    CHECK(!cipher.open(tampered));
    CHECK(!cipher.open("not a dpapi blob"));
    CHECK(!cipher.open(""));
    check_store(cipher, "sessions-dpapi");
}
#endif
