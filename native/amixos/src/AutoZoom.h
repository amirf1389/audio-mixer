// AUTO ZOOM: the console is laid out for a working area of about 1480 x 800 (Windows units). A 14" laptop with a 1920 x 1080 panel gives
// 1536 x 824 at 125 % scaling (it fits) but only 1280 x 690 at 150 %: the pages do not fit and the window is stuck larger than the screen.
// So, before Qt starts, the console works out the zoom that fits the screen and sets QT_SCALE_FACTOR (on top of the Windows scale): on
// that laptop at 150 % the interface is drawn as at about 130 %. Never zoomed in, never below 67 %; INTERFACE ZOOM in SETTINGS can fix it
// at 100 / 90 / 80 / 75 % instead (the setting is "ui/zoom": 0 = AUTO, else a percentage of the Windows scale). A QT_SCALE_FACTOR the
// user set themselves always wins.
// Since 1.24.0.5 AUTO also zooms IN on a screen of 14 inches or less, and RESPONSIVE (ui/zoom = -1) fills any screen.
#pragma once
#include <algorithm>
#include <cmath>
#include <QString>

namespace autozoom {

constexpr double kDesignW = 1480, kDesignH = 800, kMin = 0.67, kMax = 2.0, kSmallScreenInches = 14.6, kSmallMax = 1.5;
constexpr int kFill = -1;   // "ui/zoom" = -1: RESPONSIVE, the console is drawn as large as the screen allows, on any screen

/** the zoom (1 = as Windows draws it) for a work area of logicalW x logicalH Windows units; userPct > 0: the user's fixed zoom, kFill:
 *  responsive. [diagInches] is the screen's physical diagonal (0 = unknown). AUTO shrinks to fit, as before, and now also ENLARGES on a
 *  screen of 14 inches or less: a 14" laptop with 1920 x 1080 at 100 % has room for 1.29 times the design, and at 1.00 the 8 and 9 pixel
 *  labels were too small to read there. A larger monitor is left as Windows draws it (RESPONSIVE fills that one too). */
inline double factor(double logicalW, double logicalH, int userPct, double diagInches = 0) {
  if (userPct > 0) return std::clamp(userPct / 100.0, kMin, kMax);
  if (logicalW <= 0 || logicalH <= 0) return 1.0;
  const double fit = std::min(logicalW / kDesignW, logicalH / kDesignH);
  if (fit < 1.0) return std::max(kMin, std::floor(fit * 100) / 100);
  const double step = std::floor(fit * 20) / 20;   // enlarged in steps of 5 %: text and lines stay crisp
  if (userPct == kFill) return std::min(kMax, step);
  if (diagInches > 0 && diagInches <= kSmallScreenInches) return std::min(kSmallMax, step);
  return 1.0;
}
/** what the console says about it (the log, SETTINGS) */
inline QString describe(double osScale, double logicalW, double logicalH, double z) {
  return QString("Windows scale %1 %, work area %2 x %3: interface zoom %4 % (drawn as at %5 %)")
      .arg(std::lround(osScale * 100)).arg(std::lround(logicalW)).arg(std::lround(logicalH)).arg(std::lround(z * 100)).arg(std::lround(osScale * z * 100));
}

}  // namespace autozoom
