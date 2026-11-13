#include "measure/score.hpp"
#include <cmath>
#include <cstdio>
using namespace am::measure;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); ++fails; } } while (0)
static ScoreInput good() { ScoreInput in; in.shortTerm = -14; in.target = -14; in.truePeak = -1.5; in.peakDb = -1.5; in.rmsDb = -12; in.correlation = 0.7;
  for (int i = 0; i < 31; ++i) { const double f = 20 * std::pow(1000.0, i / 30.0); in.spectrum.push_back(-20 - 3 * std::log2(f / 1000)); } return in; }
int main() {
  { const Score s = soundScore(good()); CHECK(s.valid && s.total >= 98 && s.grade == "A", "good mix %f %s", s.total, s.grade.c_str()); CHECK(std::fabs(s.tiltDbPerOct + 3) < 0.05, "tilt %f", s.tiltDbPerOct); }
  { ScoreInput in = good(); in.shortTerm = -90; CHECK(!soundScore(in).valid, "silence must give no score"); }
  { ScoreInput in = good(); in.masterClip = true; const Score s = soundScore(in); CHECK(s.parts[1].points == 0 && s.total <= 80, "clip %f", s.total); }
  { ScoreInput in = good(); in.rmsDb = -4; const Score s = soundScore(in); CHECK(s.parts[2].points < 8, "squashed dynamics %f", s.parts[2].points); }
  { ScoreInput in = good(); in.spectrum[15] += 12; in.spectrum[16] += 12; const Score s = soundScore(in); CHECK(s.parts[3].points < 18 && s.parts[3].tip.find("bump") != std::string::npos, "bump: %f %s", s.parts[3].points, s.parts[3].tip.c_str()); }
  { ScoreInput in = good(); in.correlation = -0.4; const Score s = soundScore(in); CHECK(s.parts[4].points <= 5, "phase %f", s.parts[4].points); }
  { ScoreInput in = good(); in.shortTerm = -8; const Score s = soundScore(in); CHECK(s.parts[0].points <= 0.01 && s.parts[0].tip.find("too loud") != std::string::npos, "loud %f", s.parts[0].points); }
  std::printf(fails ? "score: %d failed\n" : "score: all passed\n", fails); return fails ? 1 : 0;
}
