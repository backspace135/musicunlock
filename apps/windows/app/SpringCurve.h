#pragma once
// SwiftUI spring(response:dampingFraction:) as a unit-mass damped harmonic oscillator.
// Pure C++20: no WinRT, so the same curves can be tested on the Mac.
//
//   k = (2π / response)²,  c = 4π · dampingFraction / response
//   ω₀ = 2π / response,    ζ = dampingFraction
//
// x(t) is the displacement from 0 to 1 with zero initial velocity. The formulas are the standard
// solutions of x'' + 2ζω₀x' + ω₀²x = ω₀²:
//
//   ζ < 1:  ωd = ω₀·√(1−ζ²),  x(t) = 1 − e^(−ζω₀t) · (cos(ωd·t) + (ζω₀/ωd)·sin(ωd·t))
//   ζ = 1:   x(t) = 1 − e^(−ω₀t) · (1 + ω₀t)
//   ζ > 1:   overdamped solution (see value()).

#include <cmath>
#include <utility>
#include <vector>

namespace spring {

struct Spring {
    double response;  // SwiftUI response, seconds
    double damping;   // SwiftUI dampingFraction (ζ)
};

/// x(t) for a spring that starts at 0 and rests at 1. t is seconds.
double value(Spring s, double t);

/// Earliest time after which |1 − x| stays below epsilon.
double settle_time(Spring s, double epsilon = 0.001);

/// One sample per frame: (normalized time 0…1, x). The last point is (1, 1).
std::vector<std::pair<float, float>> keyframes(Spring s, double fps = 120);

}  // namespace spring
