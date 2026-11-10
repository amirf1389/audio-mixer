// ARRANGE: WAV read / write round trips (16-bit mono 44.1 k, 24-bit stereo, float), resampling, snap / automation / clip math, offline render
// with pan, solo and automation, export, and the realtime Player through a Runtime on the null device.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "arrange/arrange.hpp"

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
using namespace am;
static float peak(const std::vector<float>& v, std::size_t a = 0, std::size_t b = SIZE_MAX) { float p = 0; for (std::size_t i = a; i < v.size() && i < b; ++i) p = std::max(p, std::fabs(v[i])); return p; }

int main(int argc, char** argv) {
  const std::string dir = std::string(argc > 1 ? argv[1] : "/tmp");
  // a 16-bit mono 44.1 k WAV of 0.5 s, 1 kHz at -6 dB, written by hand
  const std::string mono = dir + "/arr-mono.wav"; { const std::uint32_t rate = 44100, n = rate / 2; std::vector<unsigned char> o(44 + n * 2); unsigned char* p = o.data(); auto w32 = [&](int at, std::uint32_t v) { p[at] = v & 255; p[at + 1] = (v >> 8) & 255; p[at + 2] = (v >> 16) & 255; p[at + 3] = v >> 24; }; auto w16 = [&](int at, std::uint16_t v) { p[at] = v & 255; p[at + 1] = v >> 8; };
    std::memcpy(p, "RIFF", 4); w32(4, 36 + n * 2); std::memcpy(p + 8, "WAVEfmt ", 8); w32(16, 16); w16(20, 1); w16(22, 1); w32(24, rate); w32(28, rate * 2); w16(32, 2); w16(34, 16); std::memcpy(p + 36, "data", 4); w32(40, n * 2);
    for (std::uint32_t i = 0; i < n; ++i) { const std::int16_t s = static_cast<std::int16_t>(std::lrint(0.5 * std::sin(2 * 3.14159265358979 * 1000 * i / rate) * 32767)); p[44 + 2 * i] = s & 255; p[45 + 2 * i] = (s >> 8) & 255; }
    FILE* f = std::fopen(mono.c_str(), "wb"); CHECK(f); std::fwrite(o.data(), 1, o.size(), f); std::fclose(f); }
  arr::Audio a; std::string err; CHECK(arr::readWav(mono, a, err)); CHECK(a.rate == 44100 && a.L.size() == 22050 && a.R.size() == 22050 && std::fabs(peak(a.L) - 0.5f) < 0.002f && a.L[100] == a.R[100]);
  // 24-bit stereo write -> read: left only
  const std::string st = dir + "/arr-stereo.wav"; { std::vector<float> L(4800), R(4800); for (std::size_t i = 0; i < L.size(); ++i) { L[i] = 0.25f * std::sin(static_cast<float>(i) * 0.1f); R[i] = 0.5f * L[i]; } CHECK(arr::writeWav24(st, L, R, 48000, err)); }
  arr::Audio b; CHECK(arr::readWav(st, b, err) && b.rate == 48000 && b.L.size() == 4800 && std::fabs(peak(b.L) - 0.25f) < 1e-4f && std::fabs(peak(b.R) - 0.125f) < 1e-4f);
  { auto y = arr::resampleLinear(a.L, 44100, 48000); CHECK(y.size() == 24000 && std::fabs(peak(y) - 0.5f) < 0.01f); }
  CHECK(!arr::readWav(dir + "/nope.wav", a, err) && !err.empty());
  // math (the web console's AR block)
  CHECK(std::fabs(arr::beatSec(120) - 0.5) < 1e-12 && std::fabs(arr::snapTime(1.3, 120, 4) - 1.5) < 1e-9 && std::fabs(arr::snapTime(1.3, 120, 1) - 2.0) < 1e-9 && arr::snapTime(-1, 120, 0) == 0 && arr::barOf(4.1, 120) == 3);
  std::vector<arr::AutoPoint> pts = arr::addPoint({}, 2, -12); pts = arr::addPoint(pts, 0, 0); pts = arr::addPoint(pts, 2, -6);   // replaces the point at 2
  CHECK(pts.size() == 2 && pts[0].t == 0 && pts[1].v == -6 && std::fabs(arr::autoAt(pts, 1) + 3) < 1e-9 && arr::autoAt(pts, 5) == -6 && arr::autoAt({}, 1) == 0);
  CHECK(arr::dbToGain(-60) == 0 && std::fabs(arr::dbToGain(0) - 1) < 1e-12 && std::fabs(arr::dbToGain(-6) - 0.5012) < 1e-3);
  arr::Project p; p.tracks.push_back({1, "A", 0, -1, false, false, 0, {}}); p.tracks.push_back({2, "B", 0, 1, false, false, 1, {}});
  p.clips.push_back({10, 1, mono, 0, 0, 0.5, 0}); p.clips.push_back({11, 1, mono, 1.0, 0, 0.5, 0}); p.clips.push_back({12, 2, st, 0.25, 0, 0.1, 0});
  CHECK(std::fabs(arr::clipEnd(p.clips[0]) - 0.5) < 1e-12 && std::fabs(arr::projectEnd(p) - 1.5) < 1e-12 && arr::nextId(p) == 13);
  CHECK(arr::placeClip(p, 11, 0.4) && std::fabs(p.clips[1].start - 0.5) < 1e-9);   // dropped onto clip 10: pushed to its end
  CHECK(arr::placeClip(p, 11, 3.0) && p.clips[1].start == 3.0);
  CHECK(arr::splitClip(p, 10, 0.2, 13) && p.clips.size() == 4 && std::fabs(p.clips[0].duration - 0.2) < 1e-9 && p.clips[3].id == 13 && std::fabs(p.clips[3].start - 0.2) < 1e-9 && std::fabs(p.clips[3].offset - 0.2) < 1e-9 && std::fabs(arr::clipEnd(p.clips[3]) - 0.5) < 1e-9);
  CHECK(!arr::splitClip(p, 10, 0.0, 14));
  // JSON round trip, clamped
  { const arr::Project q = arr::fromJson(arr::toJson(p)); CHECK(q.tracks.size() == 2 && q.clips.size() == 4 && q.clips[3].offset == p.clips[3].offset && q.tracks[1].pan == 1);
    bool ok = false; const arr::Project bad = arr::fromJson(json::Value::parse("{\"bpm\":9999,\"snap\":3,\"tracks\":[{\"id\":1,\"db\":99}],\"clips\":[{\"id\":5,\"track\":7,\"duration\":1}]}", ok)); CHECK(ok && bad.bpm == 300 && bad.snap == 4 && bad.tracks[0].db == 6 && bad.clips.empty()); }
  // render: track A hard left plays the 1 kHz tone, track B hard right the 0.25 tone; solo B silences A; automation -60 dB at 0.3 s
  arr::Buffers bufs; { auto pa = std::make_shared<arr::Audio>(); CHECK(arr::readWav(mono, *pa, err)); bufs[mono] = pa; auto pb = std::make_shared<arr::Audio>(); CHECK(arr::readWav(st, *pb, err)); bufs[st] = pb; }
  { std::vector<float> L, R; arr::render(p, bufs, 0, 0.5, 48000, L, R); CHECK(L.size() == 24000 && peak(L, 0, 9000) > 0.4f && peak(R, 0, 9000) < 0.3f && peak(R, 12000, 16800) > 0.1f && peak(L, 24000 - 1, 24000) < 1.f);   // A hard left, B (its right channel) hard right
    CHECK(peak(R, 12000, 16800) < 0.2f);   // B's left channel does not leak right
    p.tracks[1].solo = true; L.clear(); R.clear(); arr::render(p, bufs, 0, 0.5, 48000, L, R); CHECK(peak(L) < 1e-6f && peak(R) > 0.1f); p.tracks[1].solo = false;
    p.tracks[0].autoPts = arr::addPoint(arr::addPoint({}, 0.3, 0), 0.31, -60); L.clear(); R.clear(); arr::render(p, bufs, 0, 0.5, 48000, L, R); CHECK(peak(L, 0, 14000) > 0.4f && peak(L, 16000, 24000) < 1e-3f); p.tracks[0].autoPts.clear(); }
  // export and read back
  const std::string out = dir + "/arr-export.wav"; double secs = 0; CHECK(arr::exportWav(p, bufs, 48000, out, err, &secs) && std::fabs(secs - 3.5) < 1e-9);
  { arr::Audio e; CHECK(arr::readWav(out, e, err) && e.rate == 48000 && e.L.size() == 168000 && peak(e.L, 0, 9000) > 0.4f && peak(e.L, 150000, 168000) > 0.4f); }
  { arr::Project empty; CHECK(!arr::exportWav(empty, bufs, 48000, out, err) && err.find("import") != std::string::npos); }
  // the player through a Runtime: the master meter sees the arrangement, the strips do not; stop silences; loop restarts
  { Runtime rt(2); DeviceConfig c; c.backend = "null"; c.frames = 64; CHECK(rt.open(c, err));
    arr::Player pl; pl.setProject(p, bufs); rt.setExtraSource(&pl);
    const std::size_t n = 64; std::vector<float> z(n, 0.f), o0(n), o1(n); const float* ins[2] = {z.data(), z.data()}; float* outs[2] = {o0.data(), o1.data()};
    for (int k = 0; k < 20; ++k) rt.onBlock(ins, outs, n); CHECK(rt.snapshot().master[0].pk < 1e-6);   // not playing yet
    pl.play(0); for (int k = 0; k < 60; ++k) rt.onBlock(ins, outs, n);   // 80 ms
    const Snapshot s = rt.snapshot(); CHECK(s.master[0].pk > 0.3 && s.strips[0].pk < 1e-6 && std::fabs(pl.position() - 0.08) < 0.002 && pl.playing());
    pl.stop(); for (int k = 0; k < 800; ++k) rt.onBlock(ins, outs, n); CHECK(rt.snapshot().master[0].pk < 1e-3 && std::fabs(pl.position() - 0.08) < 0.002);
    pl.setLoop(true); pl.play(3.49); for (int k = 0; k < 100; ++k) rt.onBlock(ins, outs, n); CHECK(pl.playing() && pl.position() < 1.0);   // ran past the end and wrapped
    pl.setLoop(false); pl.play(3.49); for (int k = 0; k < 100; ++k) rt.onBlock(ins, outs, n); CHECK(!pl.playing());
    rt.setExtraSource(nullptr); }
  std::remove(mono.c_str()); std::remove(st.c_str()); std::remove(out.c_str());
  std::printf("arrange_test: WAV 16/24-bit round trips, resample, snap / automation / clip math, render (pan, solo, automation), export, Player through the Runtime OK\n");
  return 0;
}
