#include "ContinuousPath.h"

#include <algorithm>
#include <cmath>

namespace continuous {
namespace {

// 连续角沿边延伸的距离，以半径为单位。
constexpr double kReach = 1.528665;

struct Corner {
    Point from;
    std::array<Curve, 3> curves;
};

/// 半径为 1 的左上角。sx/sy 为宽高的一半除以半径，小于 kReach 的边单独压缩。
Corner unit_corner(double sx, double sy) {
    // 外段控制点在最短边 s=1 与完整连续角之间线性插值。
    const auto compress = [](double s, double full, double short_side) {
        const double t = std::clamp((s - 1.0) / (kReach - 1.0), 0.0, 1.0);
        return short_side + (full - short_side) * t;
    };
    Corner corner;
    corner.from = {0.0, std::min(sy, kReach)};
    corner.curves[0] = {{0.0, compress(sy, 1.088493, 0.96)}, {0.0, compress(sy, 0.868407, 0.82)}, {0.074911, 0.631494}};
    corner.curves[1] = {{0.169060, 0.372824}, {0.372824, 0.169060}, {0.631494, 0.074911}};
    corner.curves[2] = {{compress(sx, 0.868407, 0.82), 0.0}, {compress(sx, 1.088493, 0.96), 0.0},
                        {std::min(sx, kReach), 0.0}};
    return corner;
}

/// 每个四分之一圆拆成三段 30° 曲线，径向误差小于 4e-7 r。
Corner circular_corner() {
    constexpr double pi = 3.14159265358979323846;
    const double step = pi / 6;
    const double k = 4.0 / 3 * std::tan(step / 4);
    Corner corner;
    corner.from = {0.0, 1.0};
    for (int segment = 0; segment < 3; ++segment) {
        const double a = pi + segment * step;
        const double b = a + step;
        corner.curves[static_cast<std::size_t>(segment)] = {
            {1 + std::cos(a) - k * std::sin(a), 1 + std::sin(a) + k * std::cos(a)},
            {1 + std::cos(b) + k * std::sin(b), 1 + std::sin(b) - k * std::cos(b)},
            {1 + std::cos(b), 1 + std::sin(b)}};
    }
    return corner;
}

Point place(Point unit, double radius, int quadrant, double width, double height) {
    // 镜像不交换坐标轴，确保横纵边的压缩互不影响。
    double x = unit.x * radius;
    double y = unit.y * radius;
    if (quadrant == 1 || quadrant == 2) x = width - x;
    if (quadrant == 2 || quadrant == 3) y = height - y;
    return {x, y};
}

}  // namespace

Path path(double width, double height, double radius) {
    Path result{};
    if (!(width > 0.0) || !(height > 0.0)) return result;
    radius = std::max(radius, 0.0);
    const double limit = std::min(width, height) / 2.0;
    // 只有超出短边一半才转成圆端胶囊；等于一半仍使用 #61 的压缩核对点。
    const bool capsule = radius > limit;
    radius = std::min(radius, limit);
    const double sx = radius > 0 ? width / 2.0 / radius : kReach;
    const double sy = radius > 0 ? height / 2.0 / radius : kReach;
    const Corner unit = capsule ? circular_corner() : unit_corner(sx, sy);
    result.start = place(unit.from, radius, 0, width, height);
    for (int quadrant = 0; quadrant < 4; ++quadrant) {
        const bool reverse = quadrant == 1 || quadrant == 3;
        result.corner_starts[static_cast<std::size_t>(quadrant)] =
            place(reverse ? unit.curves[2].to : unit.from, radius, quadrant, width, height);
        for (int segment = 0; segment < 3; ++segment) {
            const int index = reverse ? 2 - segment : segment;
            const Curve& curve = unit.curves[static_cast<std::size_t>(index)];
            const Point end = reverse ? (index == 0 ? unit.from : unit.curves[static_cast<std::size_t>(index - 1)].to)
                                      : curve.to;
            result.corners[static_cast<std::size_t>(quadrant)][static_cast<std::size_t>(segment)] = {
                place(reverse ? curve.control2 : curve.control1, radius, quadrant, width, height),
                place(reverse ? curve.control1 : curve.control2, radius, quadrant, width, height),
                place(end, radius, quadrant, width, height)};
        }
    }
    return result;
}

}  // namespace continuous
