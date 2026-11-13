// FRONT 180° · 10: ten real speakers in a dome in front of the listener, and how much of a sound at a given direction each one gets.
// Ear level across 180 degrees (WIDE L -90, L -45, CENTRE 0, R +45, WIDE R +90), three above (TOP L, TOP C, TOP R) and two below (LOW L,
// LOW R). Each speaker's share follows how close its direction is to the sound's ((1 + cos angle) / 2 raised to a focus power, so the nearest
// speakers carry it and the rest fade smoothly, with no jumps as the sound moves); the shares are power-normalised (total energy 1) and then
// scaled by distance (1 / r beyond 1 m). SPREAD lowers the focus: at 1 the sound fills the whole array.
#pragma once
#include <algorithm>
#include <cmath>

namespace am::fx {

struct Speaker { const char* name; double az, el; };
inline constexpr int kFront10 = 10;
inline constexpr Speaker kFront10Layout[kFront10] = {{"WIDE L", -90, 0}, {"L", -45, 0}, {"CENTRE", 0, 0}, {"R", 45, 0}, {"WIDE R", 90, 0},
                                                     {"TOP L", -60, 40}, {"TOP C", 0, 45}, {"TOP R", 60, 40}, {"LOW L", -45, -30}, {"LOW R", 45, -30}};

inline void front10Gains(double azDeg, double elDeg, double spread, double dist, double out[kFront10]) {
  constexpr double d2r = 3.14159265358979323846 / 180;
  const double a = azDeg * d2r, e = elDeg * d2r, x = std::sin(a) * std::cos(e), y = std::cos(a) * std::cos(e), z = std::sin(e);
  const double p = 2 + 14 * (1 - std::clamp(spread, 0.0, 1.0));   // focus: 16 (tight) .. 2 (everywhere)
  double sum = 0;
  for (int i = 0; i < kFront10; ++i) { const double sa = kFront10Layout[i].az * d2r, se = kFront10Layout[i].el * d2r;
    const double c = x * std::sin(sa) * std::cos(se) + y * std::cos(sa) * std::cos(se) + z * std::sin(se);
    out[i] = std::pow((1 + c) / 2, p); sum += out[i] * out[i]; }
  const double g = (sum > 0 ? 1 / std::sqrt(sum) : 0) * (dist > 1 ? 1 / dist : 1.0);
  for (int i = 0; i < kFront10; ++i) out[i] *= g;
}

}  // namespace am::fx
