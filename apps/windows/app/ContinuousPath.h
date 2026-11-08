#pragma once
// SwiftUI RoundedRectangle(style: .continuous) 的三次贝塞尔控制点。
// 纯 C++20，不依赖 WinRT，macOS 与 Windows 共用测试。
// 坐标来自 #61 的 SwiftUI path(in:) 实测值，左上角为原点，x 向右、y 向下。
// 每个角沿边延伸 1.528665 r；短边只压缩贴着它的那段，中间一段不变。

#include <array>

namespace continuous {

struct Point {
    double x;
    double y;
};

struct Curve {
    Point control1;
    Point control2;
    Point to;
};

/// 从左上角沿顺时针遍历四个角。每个角先直线连接 corner_starts，再绘制三段曲线。
/// 边长为零时返回全零路径；半径为零时返回矩形。
struct Path {
    Point start;
    std::array<Point, 4> corner_starts;
    std::array<std::array<Curve, 3>, 4> corners;
};

/// 超过短边一半的半径夹成圆端胶囊；恰好等于一半时保留压缩连续角。
Path path(double width, double height, double radius);

}  // namespace continuous
