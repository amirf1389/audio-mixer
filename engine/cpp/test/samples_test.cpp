// The 100 built-in samples: every one renders, is not silent, stays within +-1, has a unique name; the test tones sit at their level.
#include <cmath>
#include <cstdio>
#include <set>
#include <vector>

#include "samples/samples.hpp"

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

int main() {
  const auto& all = am::samples::list(); CHECK(all.size() == 100); std::set<std::string> names; std::vector<float> L, R;
  for (const auto& s : all) { names.insert(s.name); CHECK(am::samples::render(s.id, 48000, L, R)); CHECK(L.size() == static_cast<std::size_t>(s.seconds * 48000)); float pk = 0; for (std::size_t i = 0; i < L.size(); ++i) pk = std::max(pk, std::max(std::fabs(L[i]), std::fabs(R[i]))); CHECK(pk <= 1.f); if (s.name != "Silence 1 s") CHECK(pk > 0.04f); }
  CHECK(names.size() == 100);
  am::samples::render(89, 48000, L, R); float pk = 0; for (float v : L) pk = std::max(pk, std::fabs(v)); CHECK(std::fabs(20 * std::log10(pk) + 18) < 0.2);   // Tone 1 kHz -18 dBFS
  am::samples::render(97, 48000, L, R); pk = 0; for (float v : R) pk = std::max(pk, std::fabs(v)); CHECK(pk < 1e-6f);   // Left Only: right silent
  CHECK(am::samples::fileName(7) == "007-Snare Fat.wav" || am::samples::fileName(7).rfind("007-", 0) == 0);
  std::printf("samples_test: 100 samples render, in range, unique names, tones at level OK\n");
  return 0;
}
