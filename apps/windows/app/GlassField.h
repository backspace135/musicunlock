#pragma once

#include <cmath>

namespace glass {

struct Point { double x; double y; };

inline bool animate(bool active, bool motion, bool effects, bool preview, bool high_contrast) {
    return active && motion && effects && !preview && !high_contrast;
}

class FieldClock {
public:
    double advance(double delta, bool running) {
        if (running) time_ += delta;
        return time_;
    }
private:
    double time_ = 0.0;
};

inline Point position(int index, double time) {
    constexpr double rates[] = {0.05, 0.04, 0.06, 0.035, 0.045, 0.055};
    const double angle = 6.283185307179586 * rates[index % 6] * time + index * 0.7;
    return {0.5 + 0.27 * std::sin(angle), 0.5 + 0.30 * std::cos(angle * 0.8)};
}

}  // namespace glass
