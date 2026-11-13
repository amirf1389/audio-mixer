#include <cmath>
#include <cstdio>
#include <cstring>
#include "audiomixer/dynamics.hpp"
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("FAIL line %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
int main() {
  am::DynamicsTracker t;
  CHECK(!t.suggest(6).ready, "silence: still listening");
  for (int i = 0; i < 100; ++i) t.push(-70);                     // room noise only
  CHECK(!t.suggest(6).ready, "noise below -60 dBFS is not signal");
  t.clear(); for (int i = 0; i < 100; ++i) t.push(-30 + 12 * std::sin(i * 0.7));   // a speaker who swings about 24 dB
  auto s = t.suggest(6);
  std::printf("dynamic voice: spread %.1f dB, median %.1f, threshold %.1f, ratio %.2f, level %d, %s\n", s.spreadDb, s.medianDb, s.thresholdDb, s.ratio, s.level, s.verdict);
  CHECK(s.ready && s.spreadDb > 18 && s.spreadDb < 25, "spread %.1f", s.spreadDb);
  CHECK(s.ratio > 3 && s.ratio <= 4.2 && std::strcmp(s.verdict, "too dynamic") == 0, "ratio %.2f %s", s.ratio, s.verdict);
  CHECK(s.thresholdDb < s.medianDb && s.thresholdDb > s.medianDb - 4, "threshold under the median");
  CHECK(s.level >= 3 && s.level <= 5, "hybrid level %d", s.level);
  auto m = t.suggest(24); CHECK(m.ratio < 1.2 && m.level == 0 && std::strcmp(m.verdict, "on target") == 0, "a wide target asks for nothing: %.2f %d %s", m.ratio, m.level, m.verdict);
  t.clear(); for (int i = 0; i < 100; ++i) t.push(-14 + (i % 2));  // a brick-walled source
  auto q = t.suggest(6); CHECK(std::strcmp(q.verdict, "already squashed") == 0 && q.level == 0, "squashed: %s level %d", q.verdict, q.level);
  t.clear(); for (int i = 0; i < 100; ++i) t.push(i < 70 ? -100 : -20 - (i % 10)); // 3 s of signal after silence
  CHECK(t.suggest(6).ready, "3 s of signal is enough");
  t.clear(); for (int i = 0; i < 300; ++i) t.push(-30 + 12 * std::sin(i * 0.7)); CHECK(t.size() == 100, "a 10 s window");
  std::printf(fails ? "dynamics_test: %d FAILED\n" : "dynamics_test: all passed\n", fails);
  return fails ? 1 : 0;
}
