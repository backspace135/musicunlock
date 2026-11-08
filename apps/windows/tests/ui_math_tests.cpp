// Spring curves against the SwiftUI formulas in issue #59. No WinRT: the same binary builds on the Mac.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#include "SpringCurve.h"
#include "GlassField.h"

namespace {

int failures = 0;

void report(const char* file, int line, const std::string& message) {
    ++failures;
    std::cerr << file << ":" << line << ": " << message << "\n";
}

#define CHECK(expr) \
    do { if (!(expr)) report(__FILE__, __LINE__, "CHECK(" #expr ") failed"); } while (0)

void check_near(double actual, double expected, double tolerance, const char* label) {
    if (!(std::abs(actual - expected) <= tolerance)) {
        report(__FILE__, __LINE__, std::string(label) + ": got " + std::to_string(actual) + ", expected " +
                                       std::to_string(expected));
    }
}

// Menu opening spring (response 0.32, damping 0.78). Underdamped peak overshoot is e^(−ζπ/√(1−ζ²)).
void menu_open_overshoot() {
    constexpr spring::Spring open{0.32, 0.78};
    const double zeta = open.damping;
    const double expected = std::exp(-zeta * 3.14159265358979323846 / std::sqrt(1.0 - zeta * zeta));
    check_near(expected, 0.0199, 0.0005, "analytic overshoot");

    const double omega0 = 2.0 * 3.14159265358979323846 / open.response;
    const double omega_d = omega0 * std::sqrt(1.0 - zeta * zeta);
    const double peak_time = 3.14159265358979323846 / omega_d;
    check_near(spring::value(open, peak_time) - 1.0, expected, 1e-9, "sampled overshoot");
    CHECK(spring::value(open, 0.0) == 0.0);
}

// Menu closing spring (response 0.26, damping 1): critically damped, never past 1.
void menu_close_settle() {
    constexpr spring::Spring close{0.26, 1.0};
    double peak = 0.0;
    for (int i = 0; i <= 4000; ++i) peak = std::max(peak, spring::value(close, i * 0.0002));
    CHECK(peak <= 1.0);
    check_near(spring::value(close, 0.196), 0.95, 0.005, "95 percent at 0.196 s");
    check_near(spring::settle_time(close, 0.001), 0.382, 0.002, "settle_time");
    CHECK(std::abs(spring::value(close, spring::settle_time(close, 0.001)) - 1.0) < 0.001);
}

void keyframe_shape() {
    constexpr spring::Spring open{0.32, 0.78};
    constexpr double fps = 120.0;
    const auto frames = spring::keyframes(open, fps);
    const int expected = static_cast<int>(std::ceil(spring::settle_time(open) * fps)) + 1;
    CHECK(static_cast<int>(frames.size()) == expected);
    CHECK(frames.front().first == 0.f);
    CHECK(frames.front().second == 0.f);
    CHECK(frames.back().first == 1.f);
    CHECK(frames.back().second == 1.f);
    for (std::size_t i = 1; i < frames.size(); ++i) CHECK(frames[i].first > frames[i - 1].first);
}

// Overdamped springs have to be supported too; the residual decays and never crosses 1.
void overdamped_stays_below_one() {
    constexpr spring::Spring heavy{0.4, 1.5};
    double peak = 0.0;
    for (int i = 0; i <= 4000; ++i) peak = std::max(peak, spring::value(heavy, i * 0.0005));
    CHECK(peak <= 1.0);
    CHECK(spring::value(heavy, spring::settle_time(heavy)) > 1.0 - 0.001);
}

}  // namespace

int main() {
    // 任一暂停条件都必须冻结相位，恢复后从原相位继续，而不是追赶墙上时钟。
    glass::FieldClock clock;
    check_near(clock.advance(1.0, true), 1.0, 1e-9, "field running");
    check_near(clock.advance(8.0, false), 1.0, 1e-9, "field paused");
    check_near(clock.advance(0.5, true), 1.5, 1e-9, "field resumed");
    CHECK(glass::animate(true, true, true, false, false));
    CHECK(!glass::animate(false, true, true, false, false));
    CHECK(!glass::animate(true, false, true, false, false));
    CHECK(!glass::animate(true, true, false, false, false));
    CHECK(!glass::animate(true, true, true, true, false));
    CHECK(!glass::animate(true, true, true, false, true));
    const auto start = glass::position(0, 0.0);
    const auto later = glass::position(0, 3.0);
    CHECK(std::abs(start.x - later.x) + std::abs(start.y - later.y) > 0.01);
    for (int i = 0; i < 6; ++i) {
        for (int t = 0; t < 120; ++t) {
            const auto point = glass::position(i, t);
            CHECK(point.x >= 0.0 && point.x <= 1.0);
            CHECK(point.y >= 0.0 && point.y <= 1.0);
        }
    }
    menu_open_overshoot();
    menu_close_settle();
    keyframe_shape();
    overdamped_stays_below_one();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "ui math tests passed\n";
    return EXIT_SUCCESS;
}
