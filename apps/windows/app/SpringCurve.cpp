#include "SpringCurve.h"

#include <algorithm>

namespace spring {
namespace {

constexpr double kPi = 3.14159265358979323846;

struct Oscillator {
    double omega0;
    double zeta;
};

Oscillator oscillator(Spring s) { return {2.0 * kPi / s.response, s.damping}; }

/// Overdamped displacement, ζ > 1: x(t) = 1 − e^(−ζω₀t) · (cosh(ωh·t) + (ζω₀/ωh)·sinh(ωh·t)),
/// with ωh = ω₀·√(ζ²−1).
double overdamped(Oscillator o, double t) {
    const double omega_h = o.omega0 * std::sqrt(o.zeta * o.zeta - 1.0);
    const double decay = std::exp(-o.zeta * o.omega0 * t);
    return 1.0 - decay * (std::cosh(omega_h * t) + (o.zeta * o.omega0 / omega_h) * std::sinh(omega_h * t));
}

}  // namespace

double value(Spring s, double t) {
    if (t <= 0.0) return 0.0;
    const Oscillator o = oscillator(s);
    if (o.zeta > 1.0) return overdamped(o, t);
    if (o.zeta == 1.0) return 1.0 - std::exp(-o.omega0 * t) * (1.0 + o.omega0 * t);
    const double omega_d = o.omega0 * std::sqrt(1.0 - o.zeta * o.zeta);
    const double decay = std::exp(-o.zeta * o.omega0 * t);
    return 1.0 - decay * (std::cos(omega_d * t) + (o.zeta * o.omega0 / omega_d) * std::sin(omega_d * t));
}

double settle_time(Spring s, double epsilon) {
    // Earliest time after which the residual stays inside the band, not the envelope bound: an
    // underdamped spring is still outside the band at the envelope crossing whenever it is near a peak.
    if (!(epsilon > 0.0) || epsilon >= 1.0) return 0.0;
    const Oscillator o = oscillator(s);
    double rate = o.zeta * o.omega0;
    if (o.zeta > 1.0) rate = o.omega0 * (o.zeta - std::sqrt(o.zeta * o.zeta - 1.0));
    if (!(rate > 0.0)) return 0.0;
    // One step finer than a 120 fps frame, out to where even the envelope has died away.
    const double step = 1.0 / 480.0;
    const double horizon = -std::log(std::min(epsilon, 1e-9)) / rate + step;
    double crossed = 0.0;
    for (double t = 0.0; t <= horizon; t += step) {
        if (std::abs(1.0 - value(s, t)) >= epsilon) crossed = t + step;
    }
    return crossed;
}

std::vector<std::pair<float, float>> keyframes(Spring s, double fps) {
    const double duration = settle_time(s);
    const int frames = static_cast<int>(std::ceil(duration * fps));
    std::vector<std::pair<float, float>> frames_out;
    frames_out.reserve(static_cast<std::size_t>(frames) + 1);
    for (int i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / fps;
        frames_out.emplace_back(static_cast<float>(t / duration), static_cast<float>(value(s, t)));
    }
    frames_out.emplace_back(1.f, 1.f);
    return frames_out;
}

}  // namespace spring
