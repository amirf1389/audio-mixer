// AUTO ZOOM: the console is laid out for a working area of about 1480 x 800 (Windows units). A 14" laptop with a 1920 x 1080 panel gives
// 1536 x 824 at 125 % scaling (it fits) but only 1280 x 690 at 150 %: the pages do not fit and the window is stuck larger than the screen.
// So, before Qt starts, the console works out the zoom that fits the screen and sets QT_SCALE_FACTOR (on top of the Windows scale): on
// that laptop at 150 % the interface is drawn as at about 130 %. Never zoomed in, never below 67 %; INTERFACE ZOOM in SETTINGS can fix it
// at 100 / 90 / 80 / 75 % instead (the setting is "ui/zoom": 0 = AUTO, else a percentage of the Windows scale). A QT_SCALE_FACTOR the
// user set themselves always wins.
#pragma once
#include <algorithm>
#include <cmath>
#include <QString>

namespace autozoom {

constexpr double kDesignW = 1480, kDesignH = 800, kMin = 0.67;

/** the zoom (1 = as Windows draws it) for a work area of logicalW x logicalH Windows units; userPct > 0: the user's fixed zoom */
inline double factor(double logicalW, double logicalH, int userPct) {
  if (userPct > 0) return std::clamp(userPct / 100.0, kMin, 1.0);
  if (logicalW <= 0 || logicalH <= 0) return 1.0;
  const double fit = std::min(logicalW / kDesignW, logicalH / kDesignH);
  return fit >= 1.0 ? 1.0 : std::max(kMin, std::floor(fit * 100) / 100);
}
/** what the console says about it (the log, SETTINGS) */
inline QString describe(double osScale, double logicalW, double logicalH, double z) {
  return QString("Windows scale %1 %, work area %2 x %3: interface zoom %4 % (drawn as at %5 %)")
      .arg(std::lround(osScale * 100)).arg(std::lround(logicalW)).arg(std::lround(logicalH)).arg(std::lround(z * 100)).arg(std::lround(osScale * z * 100));
}

}  // namespace autozoom
