#include "audiomixer/sample_format.hpp"
#include "arrange.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace am::arr {

// ---------------------------------------------------------------- WAV
namespace {
std::uint32_t le32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<std::uint32_t>(p[3]) << 24); }
std::uint16_t le16(const unsigned char* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
constexpr double kPi = 3.14159265358979323846;
}  // namespace

bool readWav(const std::string& path, Audio& out, std::string& err) {
  FILE* f = std::fopen(path.c_str(), "rb"); if (!f) { err = "cannot open " + path; return false; }
  std::vector<unsigned char> d; unsigned char buf[65536]; std::size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) { d.insert(d.end(), buf, buf + n); if (d.size() > 2000u * 1024 * 1024) { std::fclose(f); err = "file larger than 2 GB"; return false; } }
  std::fclose(f);
  if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) || std::memcmp(d.data() + 8, "WAVE", 4)) { err = "not a WAV file (RIFF/WAVE header missing)"; return false; }
  std::size_t i = 12; int fmt = 0, ch = 0, bits = 0; double rate = 0; const unsigned char* data = nullptr; std::size_t dataLen = 0;
  while (i + 8 <= d.size()) {
    const std::uint32_t len = le32(d.data() + i + 4); const unsigned char* body = d.data() + i + 8; const std::size_t avail = std::min<std::size_t>(len, d.size() - i - 8);
    if (!std::memcmp(d.data() + i, "fmt ", 4) && avail >= 16) { fmt = le16(body); ch = le16(body + 2); rate = le32(body + 4); bits = le16(body + 14); if (fmt == 0xFFFE && avail >= 26) fmt = le16(body + 24); }   // WAVE_FORMAT_EXTENSIBLE: the sub-format's first two bytes
    else if (!std::memcmp(d.data() + i, "data", 4)) { data = body; dataLen = avail; }
    i += 8 + len + (len & 1);
  }
  if (!data || ch <= 0 || rate <= 0 || bits <= 0) { err = "WAV without fmt / data chunks"; return false; }
  const bool isFloat = fmt == 3; if (!(fmt == 1 || fmt == 3)) { err = "compressed WAV (format " + std::to_string(fmt) + ") is not supported: export it as PCM"; return false; }
  if (!(bits == 8 || bits == 16 || bits == 24 || bits == 32 || (isFloat && bits == 64))) { err = std::to_string(bits) + "-bit WAV is not supported"; return false; }
  const std::size_t bps = static_cast<std::size_t>(bits / 8), frameBytes = bps * static_cast<std::size_t>(ch), frames = dataLen / frameBytes;
  out.rate = rate; out.L.assign(frames, 0.f); out.R.assign(frames, 0.f);
  auto sample = [&](const unsigned char* p) -> float {
    switch (bits) {
      case 8: return (static_cast<int>(p[0]) - 128) / 128.f;
      case 16: return static_cast<std::int16_t>(le16(p)) / 32768.f;
      case 24: { std::int32_t v = (p[0] << 8) | (p[1] << 16) | (static_cast<std::int32_t>(p[2]) << 24); return static_cast<float>(v >> 8) / 8388608.f; }
      case 32: if (isFloat) { float v; std::memcpy(&v, p, 4); return v; } else return static_cast<float>(static_cast<std::int32_t>(le32(p)) / 2147483648.0);
      case 64: { double v; std::memcpy(&v, p, 8); return static_cast<float>(v); }
      default: return 0.f;
    }
  };
  for (std::size_t k = 0; k < frames; ++k) { const unsigned char* p = data + k * frameBytes; out.L[k] = sample(p); out.R[k] = ch > 1 ? sample(p + bps) : out.L[k]; }
  return true;
}

bool writeWav24(const std::string& path, const std::vector<float>& L, const std::vector<float>& R, double rate, std::string& err) {
  const std::size_t n = std::min(L.size(), R.size()); const std::uint32_t dataLen = static_cast<std::uint32_t>(n * 6), sr = static_cast<std::uint32_t>(rate);
  std::vector<unsigned char> o(44 + dataLen); unsigned char* p = o.data();
  auto w32 = [&](std::size_t at, std::uint32_t v) { p[at] = v & 255; p[at + 1] = (v >> 8) & 255; p[at + 2] = (v >> 16) & 255; p[at + 3] = (v >> 24) & 255; };
  auto w16 = [&](std::size_t at, std::uint16_t v) { p[at] = v & 255; p[at + 1] = (v >> 8) & 255; };
  std::memcpy(p, "RIFF", 4); w32(4, 36 + dataLen); std::memcpy(p + 8, "WAVEfmt ", 8); w32(16, 16); w16(20, 1); w16(22, 2); w32(24, sr); w32(28, sr * 6); w16(32, 6); w16(34, 24); std::memcpy(p + 36, "data", 4); w32(40, dataLen);
  std::size_t at = 44;
  for (std::size_t i = 0; i < n; ++i) for (const float v : {L[i], R[i]}) { const std::int32_t s = am::fmt::toI24(v); p[at++] = s & 255; p[at++] = (s >> 8) & 255; p[at++] = (s >> 16) & 255; }
  FILE* f = std::fopen(path.c_str(), "wb"); if (!f) { err = "cannot write " + path; return false; }
  const bool ok = std::fwrite(o.data(), 1, o.size(), f) == o.size(); std::fclose(f); if (!ok) err = "short write to " + path; return ok;
}

std::vector<float> resampleLinear(const std::vector<float>& x, double from, double to) {
  if (x.empty() || from <= 0 || to <= 0 || from == to) return x;
  const std::size_t n = static_cast<std::size_t>(std::floor(static_cast<double>(x.size()) * to / from)); std::vector<float> y(n);
  for (std::size_t i = 0; i < n; ++i) { const double s = static_cast<double>(i) * from / to; const std::size_t k = static_cast<std::size_t>(s); const double f = s - static_cast<double>(k); y[i] = k + 1 < x.size() ? static_cast<float>(x[k] * (1 - f) + x[k + 1] * f) : x[std::min(k, x.size() - 1)]; }
  return y;
}

// ---------------------------------------------------------------- math
double clampd(double v, double a, double b) { return v < a ? a : v > b ? b : v; }
double beatSec(double bpm) { return 60.0 / clampd(bpm > 0 ? bpm : 120, 20, 300); }
double snapTime(double t, double bpm, int div) { if (div <= 0) return std::max(0.0, t); const double step = beatSec(bpm) * (div == 1 ? 4.0 : 4.0 / div); return std::max(0.0, std::round(t / step) * step); }
int barOf(double t, double bpm) { return static_cast<int>(std::floor(t / (beatSec(bpm) * 4))) + 1; }
double autoAt(const std::vector<AutoPoint>& pts, double t) {
  if (pts.empty()) return 0;
  if (t <= pts.front().t) return pts.front().v;
  for (std::size_t i = 1; i < pts.size(); ++i) if (t <= pts[i].t) { const AutoPoint& a = pts[i - 1]; const AutoPoint& b = pts[i]; return b.t == a.t ? b.v : a.v + (b.v - a.v) * (t - a.t) / (b.t - a.t); }
  return pts.back().v;
}
std::vector<AutoPoint> addPoint(std::vector<AutoPoint> pts, double t, double v) {
  pts.erase(std::remove_if(pts.begin(), pts.end(), [&](const AutoPoint& x) { return std::fabs(x.t - t) <= 1e-3; }), pts.end());
  pts.push_back({std::max(0.0, t), clampd(v, -60, 6)}); std::sort(pts.begin(), pts.end(), [](const AutoPoint& a, const AutoPoint& b) { return a.t < b.t; }); return pts;
}
double dbToGain(double db) { return db <= -60 ? 0 : std::pow(10.0, clampd(db, -60, 12) / 20); }
double trackGain(const Track& t, bool solo) { return (t.mute || (solo && !t.solo)) ? 0 : dbToGain(t.db); }
void panGains(double pan, double& l, double& r) { const double a = (clampd(pan, -1, 1) + 1) * kPi / 4; l = std::cos(a); r = std::sin(a); }
double clipEnd(const Clip& c) { return c.start + std::max(0.0, c.duration - c.offset); }
bool anySolo(const Project& p) { for (const Track& t : p.tracks) if (t.solo) return true; return false; }
int nextId(const Project& p) { int m = 0; for (const Track& t : p.tracks) m = std::max(m, t.id); for (const Clip& c : p.clips) m = std::max(m, c.id); return m + 1; }
double projectEnd(const Project& p) { double e = 0; for (const Clip& c : p.clips) e = std::max(e, clipEnd(c)); for (const Marker& m : p.markers) e = std::max(e, m.t); return e; }

bool placeClip(Project& p, int id, double start) {
  Clip* me = nullptr; for (Clip& c : p.clips) if (c.id == id) me = &c;
  if (!me) return false;
  const double len = clipEnd(*me) - me->start; std::vector<const Clip*> others; for (const Clip& c : p.clips) if (c.id != id && c.track == me->track) others.push_back(&c);
  std::sort(others.begin(), others.end(), [](const Clip* a, const Clip* b) { return a->start < b->start; });
  double s = std::max(0.0, start);
  for (const Clip* o : others) { const double oe = clipEnd(*o); if (s < oe && s + len > o->start) s = (s + len / 2 < (o->start + oe) / 2) ? std::max(0.0, o->start - len) : oe; }
  for (const Clip* o : others) if (s < clipEnd(*o) && s + len > o->start) return false;
  me->start = s; return true;
}
bool splitClip(Project& p, int id, double t, int newId) {
  Clip* c = nullptr; for (Clip& x : p.clips) if (x.id == id) c = &x;
  if (!c || t <= c->start + 0.01 || t >= clipEnd(*c) - 0.01) return false;
  const double cut = t - c->start; Clip second = *c; second.id = newId; second.start = t; second.offset = c->offset + cut; c->duration = c->offset + cut; p.clips.push_back(second); return true;
}

json::Value toJson(const Project& p) {
  json::Value o = json::Value::object(); o.set("v", 1); o.set("bpm", p.bpm); o.set("snap", p.snap); o.set("loop", p.loop); o.set("zoom", p.zoom);
  json::Value ts = json::Value::array();
  for (const Track& t : p.tracks) { json::Value x = json::Value::object(); x.set("id", t.id); x.set("name", t.name); x.set("db", t.db); x.set("pan", t.pan); x.set("mute", t.mute); x.set("solo", t.solo); x.set("color", t.color); json::Value a = json::Value::array(); for (const AutoPoint& q : t.autoPts) { json::Value y = json::Value::object(); y.set("t", q.t); y.set("v", q.v); a.push(y); } x.set("auto", a); ts.push(x); }
  o.set("tracks", ts);
  json::Value cs = json::Value::array(); for (const Clip& c : p.clips) { json::Value x = json::Value::object(); x.set("id", c.id); x.set("track", c.track); x.set("file", c.file); x.set("start", c.start); x.set("offset", c.offset); x.set("duration", c.duration); x.set("db", c.db); cs.push(x); } o.set("clips", cs);
  json::Value ms = json::Value::array(); for (const Marker& m : p.markers) { json::Value x = json::Value::object(); x.set("t", m.t); x.set("name", m.name); ms.push(x); } o.set("markers", ms);
  return o;
}
Project fromJson(const json::Value& v) {
  Project p; if (!v.isObject()) return p;
  auto num = [](const json::Value& x, double d, double a, double b) { return clampd(x.type() == json::Value::Number ? x.asNumber() : d, a, b); };
  auto str = [](const json::Value& x, std::size_t n) { std::string s = x.asString(); if (s.size() > n) s.resize(n); return s; };
  p.bpm = num(v["bpm"], 120, 20, 300); const int sn = static_cast<int>(num(v["snap"], 4, 0, 16)); p.snap = (sn == 0 || sn == 1 || sn == 4 || sn == 8 || sn == 16) ? sn : 4; p.loop = v["loop"].asBool(); p.zoom = num(v["zoom"], 40, 5, 400);
  std::size_t i = 0;
  for (const json::Value& t : v["tracks"].items()) { if (p.tracks.size() >= 64) break; Track x; x.id = static_cast<int>(num(t["id"], static_cast<double>(++i), 1, 1e9)); x.name = str(t["name"], 80); if (x.name.empty()) x.name = "Track " + std::to_string(p.tracks.size() + 1); x.db = num(t["db"], 0, -60, 6); x.pan = num(t["pan"], 0, -1, 1); x.mute = t["mute"].asBool(); x.solo = t["solo"].asBool(); x.color = static_cast<int>(num(t["color"], 0, 0, 7));
    for (const json::Value& q : t["auto"].items()) { if (x.autoPts.size() >= 500) break; x.autoPts.push_back({num(q["t"], 0, 0, 36000), num(q["v"], 0, -60, 6)}); } std::sort(x.autoPts.begin(), x.autoPts.end(), [](const AutoPoint& a, const AutoPoint& b) { return a.t < b.t; }); p.tracks.push_back(x); }
  i = 0;
  for (const json::Value& c : v["clips"].items()) { if (p.clips.size() >= 2000) break; Clip x; x.id = static_cast<int>(num(c["id"], static_cast<double>(++i), 1, 1e9)); x.track = static_cast<int>(num(c["track"], 0, 0, 1e9)); x.file = str(c["file"], 1024); x.start = num(c["start"], 0, 0, 36000); x.offset = num(c["offset"], 0, 0, 36000); x.duration = num(c["duration"], 0, 0, 36000); x.db = num(c["db"], 0, -60, 6);
    bool has = false; for (const Track& t : p.tracks) if (t.id == x.track) has = true; if (has && x.duration > x.offset) p.clips.push_back(x); }
  for (const json::Value& m : v["markers"].items()) { if (p.markers.size() >= 200) break; Marker x; x.t = num(m["t"], 0, 0, 36000); x.name = str(m["name"], 80); if (x.name.empty()) x.name = "Marker"; p.markers.push_back(x); }
  std::sort(p.markers.begin(), p.markers.end(), [](const Marker& a, const Marker& b) { return a.t < b.t; });
  return p;
}

// ---------------------------------------------------------------- render
void render(const Project& p, const Buffers& b, double from, double to, double rate, std::vector<float>& L, std::vector<float>& R) {
  if (to <= from || rate <= 0) return;
  const std::size_t n = static_cast<std::size_t>(std::llround((to - from) * rate)); const std::size_t base = L.size(); L.resize(base + n, 0.f); R.resize(base + n, 0.f);
  const bool solo = anySolo(p);
  for (const Track& t : p.tracks) {
    const double tg = trackGain(t, solo); if (tg <= 0) continue; double pl, pr; panGains(t.pan, pl, pr);
    for (const Clip& c : p.clips) {
      if (c.track != t.id) continue;
      const auto it = b.find(c.file);
      if (it == b.end() || !it->second) continue;
      const Audio& a = *it->second;
      const double ce = clipEnd(c);
      if (ce <= from || c.start >= to) continue;
      const double cg = dbToGain(c.db);
      const long long s0 = std::llround((c.start - from) * rate), s1 = std::llround((ce - from) * rate); const std::size_t i0 = static_cast<std::size_t>(std::max(0LL, s0)), i1 = std::min(n, static_cast<std::size_t>(std::max(0LL, s1)));
      for (std::size_t i = i0; i < i1; ++i) {
        const double tAbs = from + static_cast<double>(i) / rate, src = (tAbs - c.start + c.offset) * a.rate; const std::size_t k = static_cast<std::size_t>(src); if (k + 1 >= a.L.size()) break;
        const float f = static_cast<float>(src - static_cast<double>(k)); const float sl = a.L[k] * (1 - f) + a.L[k + 1] * f, sr = a.R[k] * (1 - f) + a.R[k + 1] * f;
        const double g = tg * cg * (t.autoPts.empty() ? 1.0 : dbToGain(autoAt(t.autoPts, tAbs)));
        L[base + i] += static_cast<float>(sl * g * pl); R[base + i] += static_cast<float>(sr * g * pr);
      }
    }
  }
}
bool exportWav(const Project& p, const Buffers& b, double rate, const std::string& path, std::string& err, double* seconds) {
  const double end = projectEnd(p); bool any = false; for (const Clip& c : p.clips) if (b.count(c.file)) any = true;
  if (end <= 0 || !any) { err = "nothing to export: import audio files first"; return false; }
  std::vector<float> L, R; render(p, b, 0, end, rate, L, R); if (seconds) *seconds = end;
  return writeWav24(path, L, R, rate, err);
}

// ---------------------------------------------------------------- player
void Player::setProject(const Project& p, const Buffers& b) { auto s = std::make_shared<Snap>(); s->p = p; s->b = b; s->end = projectEnd(p) + 0.05; std::lock_guard<std::mutex> g(m_); snap_ = s; }
void Player::play(double from) { pos_.store(std::max(0.0, from)); playing_.store(true); }
void Player::stop() { playing_.store(false); }
void Player::seek(double s) { pos_.store(std::max(0.0, s)); }
void Player::render(float* L, float* R, std::size_t frames, double rate) {
  std::fill(L, L + frames, 0.f); std::fill(R, R + frames, 0.f);
  if (!playing_.load()) return;
  std::shared_ptr<const Snap> s; { std::lock_guard<std::mutex> g(m_); s = snap_; }
  if (!s) return;
  double pos = pos_.load(); const double len = static_cast<double>(frames) / rate;
  if (pos >= s->end) {
    if (loop_.load() && s->end > 0.1) pos = 0;
    else { playing_.store(false); return; }
  }
  tl_.clear(); tr_.clear(); arr::render(s->p, s->b, pos, pos + len, rate, tl_, tr_);
  const std::size_t n = std::min(frames, tl_.size()); std::copy(tl_.begin(), tl_.begin() + static_cast<std::ptrdiff_t>(n), L); std::copy(tr_.begin(), tr_.begin() + static_cast<std::ptrdiff_t>(n), R);
  pos_.store(pos + len);
}

}  // namespace am::arr
