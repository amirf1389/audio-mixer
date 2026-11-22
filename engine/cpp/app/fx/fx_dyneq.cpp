// AmF DYNAMIC EQ (see fx.hpp). Our own implementation: RBJ-style bilinear biquads for the bell / shelf / notch / band-pass bands and
// Butterworth cascades (first-order section for odd orders) for the cuts. Processing runs band by band over sub-blocks of 16 samples, the
// inner loops over a channel pair with plain arrays (vectorizable); nothing is allocated or locked in process().
#include "fx.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>

namespace am::fx {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr int kSub = 16;
constexpr int kSlopes[] = {6, 12, 18, 24, 30, 36, 48, 72, 96};
const char* const kKeyId[DynEq::kKeys] = {"on", "type", "freq", "gain", "q", "slope", "stereo", "dyn", "thresh", "ratio", "range", "attack", "release"};
const char* const kKeyTitle[DynEq::kKeys] = {"ON", "TYPE", "FREQ", "GAIN", "Q", "SLOPE", "STEREO", "DYNAMIC", "THRESHOLD", "RATIO", "RANGE", "ATTACK", "RELEASE"};
const char* const kTypeName[] = {"BELL", "LOW SHELF", "HIGH SHELF", "LOW CUT", "HIGH CUT", "NOTCH", "BAND PASS"};
const char* const kChanName[] = {"STEREO", "MID", "SIDE", "LEFT", "RIGHT"};
double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
std::string fmt(const char* f, double v) { char b[40]; std::snprintf(b, sizeof b, f, v); return b; }
double coefMs(double ms, double fs) { return 1 - std::exp(-1.0 / (std::max(ms, 0.01) * 0.001 * fs)); }
double defaultOf(int b, int k) {
  switch (k) {
    case DynEq::Freq: return 0.05 + 0.9 * (b + 0.5) / DynEq::kBands;
    case DynEq::Gain: return 0.5;
    case DynEq::Q: return std::log(10.0) / std::log(300.0);   // Q 1
    case DynEq::Slope: return 3.0 / 8.0;                       // 24 dB/oct
    case DynEq::Thresh: return 0.5;                            // -30 dB
    case DynEq::Ratio: return std::log(4.0) / std::log(20.0);  // 4:1
    case DynEq::Range: return 0.3;                             // -12 dB
    case DynEq::Attack: return std::log(100.0) / std::log(3000.0);   // 10 ms
    case DynEq::Release: return std::log(30.0) / std::log(600.0);    // 150 ms
    default: return 0;
  }
}
// "b12_freq" -> band 11, key Freq; false for anything else
bool parseId(const std::string& id, int& band, int& key) {
  if (id.size() < 4 || id[0] != 'b') return false; std::size_t i = 1; int n = 0;
  while (i < id.size() && id[i] >= '0' && id[i] <= '9') { n = n * 10 + (id[i] - '0'); ++i; if (n > 99) return false; }
  if (i == 1 || i >= id.size() || id[i] != '_' || n < 1 || n > DynEq::kBands) return false;
  const char* rest = id.c_str() + i + 1; for (int k = 0; k < DynEq::kKeys; ++k) if (std::strcmp(rest, kKeyId[k]) == 0) { band = n - 1; key = k; return true; }
  return false;
}
DynEq::Coef rbj(int type, double w, double q, double gdb) {
  const double A = std::pow(10.0, gdb / 40.0), c = std::cos(w), sn = std::sin(w), al = sn / (2 * q);
  double B0, B1, B2, A0, A1, A2;
  switch (type) {
    case DynEq::Bell: B0 = 1 + al * A; B1 = -2 * c; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * c; A2 = 1 - al / A; break;
    case DynEq::LowShelf: { const double r = 2 * std::sqrt(A) * al; B0 = A * ((A + 1) - (A - 1) * c + r); B1 = 2 * A * ((A - 1) - (A + 1) * c); B2 = A * ((A + 1) - (A - 1) * c - r);
      A0 = (A + 1) + (A - 1) * c + r; A1 = -2 * ((A - 1) + (A + 1) * c); A2 = (A + 1) + (A - 1) * c - r; break; }
    case DynEq::HighShelf: { const double r = 2 * std::sqrt(A) * al; B0 = A * ((A + 1) + (A - 1) * c + r); B1 = -2 * A * ((A - 1) + (A + 1) * c); B2 = A * ((A + 1) + (A - 1) * c - r);
      A0 = (A + 1) - (A - 1) * c + r; A1 = 2 * ((A - 1) - (A + 1) * c); A2 = (A + 1) - (A - 1) * c - r; break; }
    case DynEq::Notch: B0 = 1; B1 = -2 * c; B2 = 1; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; break;
    case DynEq::HighCut: B0 = (1 - c) / 2; B1 = 1 - c; B2 = (1 - c) / 2; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; break;
    case DynEq::LowCut: B0 = (1 + c) / 2; B1 = -(1 + c); B2 = (1 + c) / 2; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; break;
    default: B0 = al; B1 = 0; B2 = -al; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; break;   // band pass, 0 dB at the centre
  }
  DynEq::Coef k; k.b0 = B0 / A0; k.b1 = B1 / A0; k.b2 = B2 / A0; k.a1 = A1 / A0; k.a2 = A2 / A0; return k;
}
bool dynamicType(int t) { return t == DynEq::Bell || t == DynEq::LowShelf || t == DynEq::HighShelf; }
}  // namespace

int DynEq::slopeDb(double v) { int i = static_cast<int>(v * 8 + 0.5); i = i < 0 ? 0 : i > 8 ? 8 : i; return kSlopes[i]; }

int DynEq::design(int type, double fs, double hz, double q, double g, int slope, Coef* out) {
  const double f = std::clamp(hz, 5.0, fs * 0.49), w = 2 * kPi * f / fs; q = std::clamp(q, 0.05, 50.0);
  if (type == LowShelf || type == HighShelf) q = std::min(q, 2.0);   // a steeper shelf only overshoots
  if (type != LowCut && type != HighCut) { out[0] = rbj(type, w, q, type == Bell || type == LowShelf || type == HighShelf ? g : 0); return 1; }
  const int N = std::clamp(slope / 6, 1, 2 * kSections - 1 + 1); int used = 0;
  if (N % 2) {   // the first-order section of an odd order
    const double K = std::tan(w / 2); Coef k; k.a1 = (K - 1) / (K + 1);
    if (type == HighCut) { k.b0 = k.b1 = K / (1 + K); } else { k.b0 = 1 / (1 + K); k.b1 = -k.b0; }
    out[used++] = k; }
  for (int s = 0; s < N / 2 && used < kSections; ++s) {   // the Butterworth pole pairs: angles (2s + 1) pi / 2N (even order) or (s + 1) pi / N (odd)
    const double th = N % 2 ? kPi * (s + 1) / N : kPi * (2 * s + 1) / (2.0 * N); out[used++] = rbj(type, w, 1.0 / (2 * std::cos(th)), 0); }
  return used;
}
double DynEq::magnitudeDb(const Coef* c, int n, double fs, double hz) {
  const double w = 2 * kPi * hz / fs; const std::complex<double> z1 = std::polar(1.0, -w), z2 = z1 * z1; double db = 0;
  for (int i = 0; i < n; ++i) { const std::complex<double> num = c[i].b0 + c[i].b1 * z1 + c[i].b2 * z2, den = 1.0 + c[i].a1 * z1 + c[i].a2 * z2;
    db += 20 * std::log10(std::max(std::abs(num) / std::max(std::abs(den), 1e-30), 1e-15)); }
  return db;
}

DynEq::DynEq(double fs) : fs_(fs > 0 ? fs : 48000) {
  for (int b = 0; b < kBands; ++b) { for (int k = 0; k < kKeys; ++k) k_[b][k].store(static_cast<float>(defaultOf(b, k))); dynDb_[b].store(0.f); }
  for (auto& r : ring_) for (auto& x : r) x.store(0.f, std::memory_order_relaxed);
  reset();
}
std::vector<Param> DynEq::params() const {
  std::vector<Param> v; v.reserve(kBands * kKeys + 1);
  for (int b = 0; b < kBands; ++b) for (int k = 0; k < kKeys; ++k) { char id[24], t[40]; std::snprintf(id, sizeof id, "b%d_%s", b + 1, kKeyId[k]); std::snprintf(t, sizeof t, "BAND %d %s", b + 1, kKeyTitle[k]);
    v.push_back({id, t, "", k_[b][k].load(), defaultOf(b, k)}); }
  v.push_back({"output", "OUTPUT", "", out_.load(), 0.5});
  return v;
}
void DynEq::set(const std::string& id, double v) { v = clamp01(v); if (id == "output") { out_.store(static_cast<float>(v)); return; } int b, k; if (parseId(id, b, k)) setBand(b, k, v); }
void DynEq::setBand(int b, int k, double v) { if (b < 0 || b >= kBands || k < 0 || k >= kKeys) return; k_[b][k].store(static_cast<float>(clamp01(v))); }
double DynEq::band(int b, int k) const { return b >= 0 && b < kBands && k >= 0 && k < kKeys ? k_[b][k].load() : 0; }
std::string DynEq::text(const std::string& id, double v) const {
  v = clamp01(v); if (id == "output") return fmt("%+.1f dB", (v - 0.5) * 48); int b, k; if (!parseId(id, b, k)) return "-";
  switch (k) {
    case On: case Dyn: return v > 0.5 ? "ON" : "OFF";
    case Type: return kTypeName[typeOf(v)];
    case Freq: { const double f = freqHz(v); return f >= 1000 ? fmt("%.2f kHz", f / 1000) : fmt("%.0f Hz", f); }
    case Gain: return fmt("%+.1f dB", gainDb(v));
    case Q: return fmt("%.2f", qOf(v));
    case Slope: return fmt("%.0f dB/oct", slopeDb(v));
    case Stereo: return kChanName[chanOf(v)];
    case Thresh: return fmt("%.1f dB", threshDb(v));
    case Ratio: return fmt("%.1f : 1", ratioOf(v));
    case Range: return fmt("%+.1f dB", rangeDb(v));
    case Attack: return fmt("%.1f ms", attackMs(v));
    default: return fmt("%.0f ms", releaseMs(v));
  }
}
void DynEq::setSampleRate(double fs) { fs_ = fs > 0 ? fs : 48000; reset(); }
void DynEq::reset() {
  std::memset(z1_, 0, sizeof z1_); std::memset(z2_, 0, sizeof z2_); std::memset(d1_, 0, sizeof d1_); std::memset(d2_, 0, sizeof d2_);
  for (int b = 0; b < kBands; ++b) { env_[b] = 0; gNow_[b] = 0; used_[b] = 0; was_[b] = false; dynDb_[b].store(0.f); }
}
DynEq::Snap DynEq::snap(int b) const {
  Snap s; const auto L = [&](int k) { return static_cast<double>(k_[b][k].load(std::memory_order_relaxed)); };
  s.on = L(On) > 0.5; s.type = typeOf(L(Type)); s.dyn = L(Dyn) > 0.5 && dynamicType(s.type); s.slope = slopeDb(L(Slope)); s.chan = chanOf(L(Stereo));
  s.f = freqHz(L(Freq)); s.g = gainDb(L(Gain)); s.q = qOf(L(Q)); s.thr = threshDb(L(Thresh)); s.ratio = ratioOf(L(Ratio)); s.range = rangeDb(L(Range)); s.att = attackMs(L(Attack)); s.rel = releaseMs(L(Release));
  return s;
}
void DynEq::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const double outG = std::pow(10.0, (out_.load() - 0.5) * 48 / 20.0);
  std::uint32_t w = w_.load(std::memory_order_relaxed);
  for (std::size_t i0 = 0; i0 < n; i0 += kSub) {
    const int m = static_cast<int>(std::min<std::size_t>(kSub, n - i0)); float* l = L + i0; float* r = R + i0;
    for (int i = 0; i < m; ++i) ring_[0][(w + static_cast<std::uint32_t>(i)) & (kRing - 1)].store(0.5f * (l[i] + r[i]), std::memory_order_relaxed);
    for (int b = 0; b < kBands; ++b) {
      const Snap s = snap(b);
      if (!s.on) { if (was_[b]) { was_[b] = false; dynDb_[b].store(0.f, std::memory_order_relaxed); } continue; }
      if (!was_[b]) { std::memset(z1_[b], 0, sizeof z1_[b]); std::memset(z2_[b], 0, sizeof z2_[b]); d1_[b][0] = d1_[b][1] = d2_[b][0] = d2_[b][1] = 0; env_[b] = 0; was_[b] = true; }
      const bool two = s.chan == StereoMode;
      double a[kSub], c2[kSub];   // the band's channel pair: (L, R), or the one signal it works on in a[]
      for (int i = 0; i < m; ++i) { const double x = l[i], y = r[i];
        switch (s.chan) { case MidMode: a[i] = 0.5 * (x + y); c2[i] = 0.5 * (x - y); break; case SideMode: a[i] = 0.5 * (x - y); c2[i] = 0.5 * (x + y); break;
          case LeftMode: a[i] = x; c2[i] = y; break; case RightMode: a[i] = y; c2[i] = x; break; default: a[i] = x; c2[i] = y; } }
      double gd = 0;
      if (s.dyn) {   // the side chain: this band's own frequencies, followed with ATTACK / RELEASE
        const Coef d = rbj(BandPass, 2 * kPi * std::clamp(s.f, 5.0, fs_ * 0.49) / fs_, std::clamp(s.q, 0.3, 50.0), 0); const double aA = coefMs(s.att, fs_), aR = coefMs(s.rel, fs_);
        double e = env_[b];
        for (int i = 0; i < m; ++i) { double pk = 0;
          for (int ch = 0; ch < (two ? 2 : 1); ++ch) { const double x = ch ? c2[i] : a[i], y = d.b0 * x + d1_[b][ch]; d1_[b][ch] = d.b1 * x - d.a1 * y + d2_[b][ch]; d2_[b][ch] = d.b2 * x - d.a2 * y; pk = std::max(pk, std::fabs(y)); }
          e += (pk > e ? aA : aR) * (pk - e); }
        env_[b] = e < 1e-12 ? 0 : e;
        const double over = 20 * std::log10(std::max(e, 1e-9)) - s.thr;
        if (over > 0) { const double amt = std::min(over * (1 - 1 / s.ratio), std::fabs(s.range)); gd = s.range < 0 ? -amt : amt; }
        for (int ch = 0; ch < 2; ++ch) { if (std::fabs(d1_[b][ch]) < 1e-20) d1_[b][ch] = 0; if (std::fabs(d2_[b][ch]) < 1e-20) d2_[b][ch] = 0; }
      }
      gNow_[b] = gd; dynDb_[b].store(static_cast<float>(gd), std::memory_order_relaxed);
      used_[b] = design(s.type, fs_, s.f, s.q, s.g + gd, s.slope, c_[b]);
      for (int sct = 0; sct < used_[b]; ++sct) { const Coef k = c_[b][sct]; double* z1 = z1_[b][sct]; double* z2 = z2_[b][sct];
        for (int i = 0; i < m; ++i) { double y = k.b0 * a[i] + z1[0]; z1[0] = k.b1 * a[i] - k.a1 * y + z2[0]; z2[0] = k.b2 * a[i] - k.a2 * y; a[i] = y;
          if (two) { y = k.b0 * c2[i] + z1[1]; z1[1] = k.b1 * c2[i] - k.a1 * y + z2[1]; z2[1] = k.b2 * c2[i] - k.a2 * y; c2[i] = y; } }
        for (int ch = 0; ch < 2; ++ch) { if (std::fabs(z1[ch]) < 1e-20) z1[ch] = 0; if (std::fabs(z2[ch]) < 1e-20) z2[ch] = 0; } }   // no denormals in the state
      for (int i = 0; i < m; ++i) { double x, y;
        switch (s.chan) { case MidMode: x = a[i] + c2[i]; y = a[i] - c2[i]; break; case SideMode: x = c2[i] + a[i]; y = c2[i] - a[i]; break;
          case RightMode: x = c2[i]; y = a[i]; break; default: x = a[i]; y = c2[i]; }
        l[i] = static_cast<float>(x); r[i] = static_cast<float>(y); }
    }
    for (int i = 0; i < m; ++i) { double x = l[i] * outG, y = r[i] * outG; x = std::fabs(x) < 1e-30 ? 0 : x; y = std::fabs(y) < 1e-30 ? 0 : y; l[i] = static_cast<float>(x); r[i] = static_cast<float>(y);
      ring_[1][(w + static_cast<std::uint32_t>(i)) & (kRing - 1)].store(0.5f * (l[i] + r[i]), std::memory_order_relaxed); }
    w += static_cast<std::uint32_t>(m); w_.store(w, std::memory_order_release);
  }
}
double DynEq::bandResponseDb(int b, double hz, bool withDyn) const {
  if (b < 0 || b >= kBands) return 0; const Snap s = snap(b); if (!s.on) return 0; Coef c[kSections];
  const int u = design(s.type, fs_, s.f, s.q, s.g + (withDyn && s.dyn ? dynDb_[b].load(std::memory_order_relaxed) : 0.0), s.slope, c); return magnitudeDb(c, u, fs_, hz);
}
double DynEq::responseDb(double hz, bool withDyn) const { double d = 0; for (int b = 0; b < kBands; ++b) d += bandResponseDb(b, hz, withDyn); return d + (out_.load() - 0.5) * 48; }
void DynEq::spectrum(bool post, float* outDb, int bins, double lo, double hi) const {
  constexpr int N = 2048; std::vector<std::complex<double>> x(N); const std::uint32_t w = w_.load(std::memory_order_acquire);
  for (int i = 0; i < N; ++i) { const double hann = 0.5 - 0.5 * std::cos(2 * kPi * i / (N - 1)); x[static_cast<std::size_t>(i)] = ring_[post ? 1 : 0][(w - N + static_cast<std::uint32_t>(i)) & (kRing - 1)].load(std::memory_order_relaxed) * hann; }
  for (int i = 1, j = 0; i < N; ++i) { int bit = N >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(x[static_cast<std::size_t>(i)], x[static_cast<std::size_t>(j)]); }   // iterative radix-2
  for (int len = 2; len <= N; len <<= 1) { const std::complex<double> wl = std::polar(1.0, -2 * kPi / len);
    for (int i = 0; i < N; i += len) { std::complex<double> wn(1); for (int k = 0; k < len / 2; ++k) { const auto u = x[static_cast<std::size_t>(i + k)], v = x[static_cast<std::size_t>(i + k + len / 2)] * wn; x[static_cast<std::size_t>(i + k)] = u + v; x[static_cast<std::size_t>(i + k + len / 2)] = u - v; wn *= wl; } } }
  const double norm = 4.0 / N;   // a full-scale sine reads about 0 dBFS through the Hann window
  for (int j = 0; j < bins; ++j) {
    const double f0 = lo * std::pow(hi / lo, static_cast<double>(j) / bins), f1 = lo * std::pow(hi / lo, static_cast<double>(j + 1) / bins);
    int k0 = static_cast<int>(f0 / fs_ * N), k1 = static_cast<int>(f1 / fs_ * N); k0 = std::clamp(k0, 1, N / 2 - 1); k1 = std::clamp(k1, k0, N / 2 - 1);
    double mx = 0; for (int k = k0; k <= k1; ++k) mx = std::max(mx, std::abs(x[static_cast<std::size_t>(k)]));
    outDb[j] = static_cast<float>(20 * std::log10(std::max(mx * norm, 1e-7)));
  }
}
}  // namespace am::fx
