// Spring curves against the SwiftUI formulas in issue #59. No WinRT: the same binary builds on the Mac.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#include "ContinuousPath.h"
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

// The top-left corner of a continuous rounded rectangle, against the points printed from SwiftUI.
void expect_corner(double width, double height, double radius, double start_y, double c1_y, double c2_y, const char* label) {
    const continuous::Path shape = continuous::path(width, height, radius);
    const double tolerance = 1e-4 * radius;
    check_near(shape.start.x, 0.0, tolerance, label);
    check_near(shape.start.y, start_y * radius, tolerance, label);
    const continuous::Curve& first = shape.corners[0][0];
    check_near(first.control1.x, 0.0, tolerance, label);
    check_near(first.control1.y, c1_y * radius, tolerance, label);
    check_near(first.control2.x, 0.0, tolerance, label);
    check_near(first.control2.y, c2_y * radius, tolerance, label);
    check_near(first.to.x, 0.074911 * radius, tolerance, label);
    check_near(first.to.y, 0.631494 * radius, tolerance, label);
    // The middle segment never moves, however short the side is.
    const continuous::Curve& middle = shape.corners[0][1];
    check_near(middle.control1.x, 0.169060 * radius, tolerance, label);
    check_near(middle.to.y, 0.074911 * radius, tolerance, label);
    // The top edge compresses on its own, independently of the left edge checked above.
    const double top = std::min(width / 2.0, 1.528665 * radius);
    check_near(shape.corners[0][2].to.x, top, tolerance, label);
    check_near(shape.corners[0][2].to.y, 0.0, tolerance, label);
}

void continuous_corners_match_swiftui() {
    expect_corner(200, 120, 20, 1.528665, 1.088493, 0.868407, "200x120");
    expect_corner(200, 50, 20, 1.25, 1.02076, 0.84289, "200x50");
    expect_corner(200, 44, 20, 1.1, 0.98430, 0.82916, "200x44");
    expect_corner(60, 40, 20, 1.0, 0.96, 0.82, "60x40");
}

// 宽高互换后，左上角应等于原角反向并交换 x/y；能抓到只压缩左边、遗漏顶部的错误。
void continuous_short_edges_are_independent() {
    for (const double short_side : {40.0, 44.0, 50.0, 60.0}) {
        const auto wide = continuous::path(200, short_side, 20);
        const auto tall = continuous::path(short_side, 200, 20);
        for (std::size_t segment = 0; segment < 3; ++segment) {
            const auto& actual = tall.corners[0][segment];
            const auto& mirrored = wide.corners[0][2 - segment];
            check_near(actual.control1.x, mirrored.control2.y, 1e-9, "transposed control1 x");
            check_near(actual.control1.y, mirrored.control2.x, 1e-9, "transposed control1 y");
            check_near(actual.control2.x, mirrored.control1.y, 1e-9, "transposed control2 x");
            check_near(actual.control2.y, mirrored.control1.x, 1e-9, "transposed control2 y");
        }
    }
}

// 四个角必须沿顺时针连接；错误的轴交换会让右下角逆行、短边压缩落在宽边上。
void continuous_corners_follow_outline() {
    for (const auto size : {continuous::Point{200, 120}, continuous::Point{200, 50},
                            continuous::Point{200, 44}, continuous::Point{60, 40},
                            continuous::Point{40, 60}}) {
        const auto shape = continuous::path(size.x, size.y, 20);
        const double reach_x = std::min(size.x / 2, 30.5733);
        const double reach_y = std::min(size.y / 2, 30.5733);
        const continuous::Point ends[] = {{reach_x, 0}, {size.x, reach_y},
                                          {size.x - reach_x, size.y}, {0, size.y - reach_y}};
        const continuous::Point starts[] = {{0, reach_y}, {size.x - reach_x, 0},
                                            {size.x, size.y - reach_y}, {reach_x, size.y}};
        for (std::size_t corner = 0; corner < 4; ++corner) {
            check_near(shape.corner_starts[corner].x, starts[corner].x, 1e-6, "edge connection x");
            check_near(shape.corner_starts[corner].y, starts[corner].y, 1e-6, "edge connection y");
            check_near(shape.corners[corner][2].to.x, ends[corner].x, 1e-6, "corner end x");
            check_near(shape.corners[corner][2].to.y, ends[corner].y, 1e-6, "corner end y");
        }
        // 右上角是左上角反向后水平镜像，不应把短边压缩旋转到另一条边。
        check_near(shape.corners[1][0].control1.x, size.x - shape.corners[0][2].control2.x, 1e-6,
                   "top-right control x");
        check_near(shape.corners[1][0].control1.y, 0, 1e-6, "top-right horizontal tangent");
        check_near(shape.corners[2][0].control1.x, size.x, 1e-6, "bottom-right vertical tangent");
    }
}

void clamped_capsule_has_circular_ends() {
    const auto shape = continuous::path(100, 40, 1000);
    continuous::Point from = shape.start;
    for (const auto& curve : shape.corners[0]) {
        for (int sample = 0; sample <= 100; ++sample) {
            const double t = sample / 100.0;
            const double u = 1 - t;
            const double x = u*u*u*from.x + 3*u*u*t*curve.control1.x + 3*u*t*t*curve.control2.x + t*t*t*curve.to.x;
            const double y = u*u*u*from.y + 3*u*u*t*curve.control1.y + 3*u*t*t*curve.control2.y + t*t*t*curve.to.y;
            check_near(std::hypot(x - 20, y - 20), 20, 1e-4 * 20, "circular capsule");
        }
        from = curve.to;
    }
}

void zero_radius_keeps_rectangle() {
    const auto shape = continuous::path(200, 120, 0);
    check_near(shape.corners[0][2].to.x, 0, 1e-6, "square top-left");
    check_near(shape.corners[1][2].to.x, 200, 1e-6, "square top-right");
    check_near(shape.corners[2][2].to.y, 120, 1e-6, "square bottom-right");
    check_near(shape.corners[3][2].to.y, 120, 1e-6, "square bottom-left");
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
    continuous_corners_match_swiftui();
    continuous_short_edges_are_independent();
    continuous_corners_follow_outline();
    zero_radius_keeps_rectangle();
    clamped_capsule_has_circular_ends();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "ui math tests passed\n";
    return EXIT_SUCCESS;
}
