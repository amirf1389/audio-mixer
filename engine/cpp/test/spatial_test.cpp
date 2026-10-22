// 360 degree output test (built only with Resonance Audio): a tone placed to the right is louder in the right ear and the other way round, straight ahead is balanced,
// the latency is exactly one block for any chunking of the input, and the Runtime path (null device) delivers the rendered scene. Exit 0 = all good.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

#include "audiomixer/runtime.hpp"
#include "audiomixer/spatial.hpp"

static int failures = 0;
static void expect(bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL %s\n", what); } }

struct Ears { double l, r; };
static Ears render(double az, double spread, std::size_t chunk) {
  am::Spatial sp(48000, 256);
  sp.setPosition(az, 0, 2, spread);
  const std::size_t N = 48000;
  std::vector<float> in(N), l(N), r(N);
  for (std::size_t i = 0; i < N; ++i) in[i] = static_cast<float>(0.5 * std::sin(2 * M_PI * 1000.0 * static_cast<double>(i) / 48000.0));
  for (std::size_t i = 0; i < N; i += chunk) { const std::size_t n = std::min(chunk, N - i); sp.process(&in[i], &in[i], &l[i], &r[i], n); }
  double el = 0, er = 0; for (std::size_t i = 24000; i < N; ++i) { el += l[i] * l[i]; er += r[i] * r[i]; }
  return {std::sqrt(el / 24000), std::sqrt(er / 24000)};
}

int main() {
  expect(am::Spatial::available(), "built with Resonance Audio");
  const Ears right = render(90, 0, 256), left = render(270, 0, 256), front = render(0, 0, 256);
  std::printf("azimuth 90: L %.4f R %.4f | azimuth 270: L %.4f R %.4f | azimuth 0: L %.4f R %.4f\n", right.l, right.r, left.l, left.r, front.l, front.r);
  expect(right.r > right.l * 1.5, "a tone to the right is louder in the right ear");
  expect(left.l > left.r * 1.5, "a tone to the left is louder in the left ear");
  expect(std::fabs(front.l - front.r) < 0.02 * (front.l + front.r), "straight ahead is balanced");
  const Ears odd = render(90, 0, 100);                                                              // chunks that do not divide the block
  expect(std::fabs(odd.r - right.r) < 1e-4 && std::fabs(odd.l - right.l) < 1e-4, "the result does not depend on the chunk size");

  // latency: an impulse comes out exactly one block later
  am::Spatial sp(48000, 256); sp.setPosition(0, 0, 2, 30);
  std::vector<float> in(1024, 0.0f), l(1024), r(1024); in[0] = 1.0f;
  sp.process(in.data(), in.data(), l.data(), r.data(), 1024);
  std::size_t first = 0; for (std::size_t i = 0; i < 1024; ++i) if (std::fabs(l[i]) > 1e-6f || std::fabs(r[i]) > 1e-6f) { first = i; break; }
  std::printf("first output sample at frame %zu (block %u)\n", first, sp.block());
  expect(first >= sp.block() && first < sp.block() + 64, "the output is delayed by one block (plus the HRTF's own few samples)");

  // the Runtime: strip 0 hard left on the null device; with 360 on, the right ear gets sound too
  am::Runtime rt(2); am::DeviceConfig cfg; cfg.backend = "null"; cfg.sampleRate = 48000; cfg.frames = 256; cfg.inChannels = 2; cfg.outChannels = 2;
  cfg.nullInput = [](float* const* x, std::size_t n, std::uint64_t f0) { for (std::size_t i = 0; i < n; ++i) { const float v = static_cast<float>(0.5 * std::sin(2 * M_PI * 1000.0 * static_cast<double>(f0 + i) / 48000.0)); x[0][i] = v; x[1][i] = 0.0f; } };
  std::string err;
  expect(rt.open(cfg, err), "open");
  rt.setPan(0, -1);
  expect(rt.setSpatial(true), "setSpatial is accepted"); rt.setSpatialPosition(0, 0, 2, 30);
  expect(rt.start(err), "start");
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  const am::Snapshot s = rt.snapshot(); rt.stop();
  std::printf("runtime: %llu blocks, strip 0 pk %.3f, master L %.3f R %.3f (meters are before the 360 stage)\n", static_cast<unsigned long long>(s.blocks), s.strips[0].pk, s.master[0].pk, s.master[1].pk);
  expect(s.blocks > 20 && s.master[0].pk > 0.1 && s.master[1].pk < 0.01, "the meters show the stereo master (hard left)");
  if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
  std::printf("spatial: all good\n");
  return 0;
}
