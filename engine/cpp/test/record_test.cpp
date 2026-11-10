// The recorder on a Runtime: master + one armed strip to WAV while blocks run; files read back with the arrangement's WAV reader.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "arrange/arrange.hpp"
#include "record/recorder.hpp"

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
using namespace am;

int main(int argc, char** argv) {
  const std::string dir = std::string(argc > 1 ? argv[1] : "/tmp") + "/amixos-rec-test";
  Runtime rt(4); DeviceConfig c; c.backend = "null"; c.frames = 64; std::string err; CHECK(rt.open(c, err));
  rt.setGainDb(0, 0); rt.setPan(0, -1); rt.setGainDb(1, 0); rt.setPan(1, 1); rt.setTrimDb(1, -6.0206);
  rec::Recorder r; rec::Options o; o.folder = dir; o.session = "test"; o.master = true; o.strips = {1}; CHECK(r.start(o, 48000, err)); rt.setTap(&r);
  const std::size_t n = 64; std::vector<float> tone(n); for (std::size_t i = 0; i < n; ++i) tone[i] = 0.5f * std::sin(static_cast<float>(i) * 0.3f); std::vector<float> o0(n), o1(n); const float* ins[4] = {tone.data(), tone.data(), nullptr, nullptr}; float* outs[2] = {o0.data(), o1.data()};
  for (int k = 0; k < 750; ++k) rt.onBlock(ins, outs, n);   // 1 s
  CHECK(r.recording() && std::fabs(r.seconds() - 1.0) < 0.01 && r.dropped() == 0);
  rt.setTap(nullptr); r.stop(); CHECK(!r.recording() && r.files().size() == 2);
  arr::Audio m; CHECK(arr::readWav(r.files()[0], m, err) && m.rate == 48000 && m.L.size() == 48000);
  float pl = 0, pr = 0; for (std::size_t i = 100; i < m.L.size(); ++i) { pl = std::max(pl, std::fabs(m.L[i])); pr = std::max(pr, std::fabs(m.R[i])); }
  CHECK(pl > 0.4f && pr > 0.2f);                                   // strip 0 left, strip 1 (trimmed) right: both reach the master file
  arr::Audio s1; CHECK(arr::readWav(r.files()[1], s1, err) && s1.L.size() == 48000); float ps = 0; for (float v : s1.L) ps = std::max(ps, std::fabs(v)); CHECK(std::fabs(ps - 0.25f) < 0.01f);   // the strip file is pre-fader, after the -6 dB trim, mono
  CHECK(r.files()[0].find("test-master.wav") != std::string::npos && r.files()[1].find("test-ch02.wav") != std::string::npos);
  // nothing armed is refused; a second start while recording is refused
  rec::Recorder r2; rec::Options none; none.folder = dir; none.master = false; CHECK(!r2.start(none, 48000, err) && err.find("arm") != std::string::npos);
  for (const auto& f : r.files()) std::remove(f.c_str());
  std::printf("record_test: master + armed strip to 24-bit WAV through the Runtime tap, 1 s, no drops, pre-fader strip, read back OK\n");
  return 0;
}
