// HIGH-RESOLUTION AUDIO (hires.hpp): r8brain-free-src, libsoxr (loaded at run time), dr_wav / dr_flac, libFLAC; dither and word lengths.
#include "hires/hires.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "hires/r8brain/CDSPResampler.h"
#include "hires/soxr/soxr.h"   // types only: the functions are looked up in amix_soxr at run time
#include "FLAC/stream_encoder.h"

#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO_WARNINGS
#include "hires/dr/dr_wav.h"
#define DR_FLAC_IMPLEMENTATION
#include "hires/dr/dr_flac.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#include <climits>
#endif

namespace am::hr {
namespace {

#ifdef _WIN32
std::wstring wide(const std::string& s) { const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0); std::wstring w(n > 0 ? static_cast<std::size_t>(n) : 1u, L'\0'); if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n); w.resize(w.size() - 1); return w; }
#endif

// ---- libsoxr, found next to the program (or AMIX_SOXR_PATH), never required
struct Soxr {
  using Create = soxr_t (*)(double, double, unsigned, soxr_error_t*, const soxr_io_spec_t*, const soxr_quality_spec_t*, const soxr_runtime_spec_t*);
  using Process = soxr_error_t (*)(soxr_t, soxr_in_t, size_t, size_t*, soxr_out_t, size_t, size_t*);
  using Delete = void (*)(soxr_t);
  using Io = soxr_io_spec_t (*)(soxr_datatype_t, soxr_datatype_t);
  using Quality = soxr_quality_spec_t (*)(unsigned long, unsigned long);
  using Version = const char* (*)(void);
  using Delay = double (*)(soxr_t);
  Create create = nullptr; Process process = nullptr; Delete del = nullptr; Io io = nullptr; Quality quality = nullptr; Version version = nullptr; Delay delay = nullptr;
  bool ok = false; std::string where;
};
const Soxr& soxr() {
  static Soxr s; static std::once_flag once;
  std::call_once(once, [] {
    void* h = nullptr; std::string tried;
#ifdef _WIN32
    auto open = [&](const std::wstring& p) { HMODULE m = LoadLibraryExW(p.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH); return static_cast<void*>(m); };
    if (const char* e = std::getenv("AMIX_SOXR_PATH"); e && *e) h = open(wide(e));
    if (!h) { wchar_t buf[MAX_PATH]; const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH); std::wstring d(buf, n); d = d.substr(0, d.find_last_of(L"\\/") + 1); h = open(d + L"amix_soxr.dll"); }
    auto sym = [&](const char* n) { return h ? reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(h), n)) : nullptr; };
#else
    auto open = [&](const std::string& p) { return dlopen(p.c_str(), RTLD_NOW | RTLD_LOCAL); };
    if (const char* e = std::getenv("AMIX_SOXR_PATH"); e && *e) h = open(e);
    if (!h) { char buf[PATH_MAX]; const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1); if (n > 0) { std::string d(buf, static_cast<std::size_t>(n)); d = d.substr(0, d.find_last_of('/') + 1); h = open(d + "libamix_soxr.so"); } }
    if (!h) h = open("libamix_soxr.so");
    auto sym = [&](const char* n) { return h ? dlsym(h, n) : nullptr; };
#endif
    s.create = reinterpret_cast<Soxr::Create>(sym("soxr_create")); s.process = reinterpret_cast<Soxr::Process>(sym("soxr_process")); s.del = reinterpret_cast<Soxr::Delete>(sym("soxr_delete"));
    s.io = reinterpret_cast<Soxr::Io>(sym("soxr_io_spec")); s.quality = reinterpret_cast<Soxr::Quality>(sym("soxr_quality_spec")); s.version = reinterpret_cast<Soxr::Version>(sym("soxr_version")); s.delay = reinterpret_cast<Soxr::Delay>(sym("soxr_delay"));
    s.ok = s.create && s.process && s.del && s.io && s.quality && s.version;
  });
  return s;
}

// ---- r8brain: one resampler per channel
class R8b : public Resampler {
 public:
  R8b(double src, double dst, int ch, int quality) {
    src_ = src; dst_ = dst; ch_ = ch;
    const double atten = quality <= 0 ? 136.45 : quality == 1 ? 180.15 : 206.91;   // 16-bit, 24-bit / float, the r8brain maximum
    for (int c = 0; c < ch; ++c) r_.push_back(std::make_unique<r8b::CDSPResampler>(src, dst, kBlock, 2.0, atten, r8b::fprLinearPhase));
    buf_.resize(kBlock);
  }
  void process(const double* const* in, int frames, std::vector<std::vector<double>>& out) override {
    out.resize(static_cast<std::size_t>(ch_));
    for (int c = 0; c < ch_; ++c) for (int at = 0; at < frames; at += kBlock) {
      const int n = std::min(kBlock, frames - at); std::copy(in[c] + at, in[c] + at + n, buf_.begin()); double* op = nullptr;
      const int got = r_[static_cast<std::size_t>(c)]->process(buf_.data(), n, op); out[static_cast<std::size_t>(c)].insert(out[static_cast<std::size_t>(c)].end(), op, op + got); }
  }
  void flush(std::vector<std::vector<double>>& out) override {   // zeros until the filter has given back what is still inside it
    out.resize(static_cast<std::size_t>(ch_)); const int tail = r_.empty() ? 0 : r_[0]->getLatency() + 64; std::fill(buf_.begin(), buf_.end(), 0.0);
    for (int c = 0; c < ch_; ++c) { int left = static_cast<int>(std::ceil(tail * src_ / dst_)) + kBlock; while (left > 0) { const int n = std::min(kBlock, left); double* op = nullptr; const int got = r_[static_cast<std::size_t>(c)]->process(buf_.data(), n, op); out[static_cast<std::size_t>(c)].insert(out[static_cast<std::size_t>(c)].end(), op, op + got); left -= n; } }
  }
  const char* engine() const override { return "r8brain"; }
  static constexpr int kBlock = 4096;
 private:
  std::vector<std::unique_ptr<r8b::CDSPResampler>> r_; std::vector<double> buf_;
};

// ---- libsoxr: one multi-channel stream, split (planar) double buffers
class SoxrR : public Resampler {
 public:
  SoxrR(double src, double dst, int ch, int quality, std::string& err) {
    src_ = src; dst_ = dst; ch_ = ch; const Soxr& x = soxr();
    const soxr_io_spec_t io = x.io(SOXR_FLOAT64_S, SOXR_FLOAT64_S); const soxr_quality_spec_t q = x.quality(quality <= 0 ? SOXR_16_BITQ : quality == 1 ? SOXR_VHQ : SOXR_32_BITQ, 0);
    soxr_error_t e = nullptr; h_ = x.create(src, dst, static_cast<unsigned>(ch), &e, &io, &q, nullptr); if (!h_) err = std::string("soxr: ") + (e ? e : "could not create");
  }
  ~SoxrR() override { if (h_) soxr().del(h_); }
  bool ok() const { return h_ != nullptr; }
  void process(const double* const* in, int frames, std::vector<std::vector<double>>& out) override { run(in, static_cast<std::size_t>(frames), out); }
  void flush(std::vector<std::vector<double>>& out) override { run(nullptr, 0, out); }
  const char* engine() const override { return "soxr"; }
 private:
  void run(const double* const* in, std::size_t frames, std::vector<std::vector<double>>& out) {
    out.resize(static_cast<std::size_t>(ch_)); const std::size_t cap = static_cast<std::size_t>(std::ceil(static_cast<double>(std::max<std::size_t>(frames, 1024)) * dst_ / src_)) + 1024;
    std::vector<std::vector<double>> tmp(static_cast<std::size_t>(ch_), std::vector<double>(cap)); std::vector<double*> op(static_cast<std::size_t>(ch_)); std::vector<const double*> ip(static_cast<std::size_t>(ch_));
    std::size_t at = 0;
    for (int guard = 0; guard < 100000; ++guard) {
      for (int c = 0; c < ch_; ++c) { op[static_cast<std::size_t>(c)] = tmp[static_cast<std::size_t>(c)].data(); if (in) ip[static_cast<std::size_t>(c)] = in[c] + at; }
      std::size_t idone = 0, odone = 0;
      soxr().process(h_, in ? static_cast<soxr_in_t>(ip.data()) : nullptr, in ? frames - at : 0, &idone, static_cast<soxr_out_t>(op.data()), cap, &odone);
      for (int c = 0; c < ch_; ++c) out[static_cast<std::size_t>(c)].insert(out[static_cast<std::size_t>(c)].end(), tmp[static_cast<std::size_t>(c)].begin(), tmp[static_cast<std::size_t>(c)].begin() + static_cast<std::ptrdiff_t>(odone));
      at += idone; if (in ? (at >= frames && odone < cap) : odone == 0) break;
    }
  }
  soxr_t h_ = nullptr;
};

// the 5-tap error filter of Wannamaker / Lipshitz ("E-weighted"): pushes the dither noise above ~15 kHz where hearing is least sensitive
constexpr double kShape[5] = {2.033, -2.165, 1.959, -1.590, 0.6149};

}  // namespace

bool soxrAvailable() { return soxr().ok; }

std::vector<LibInfo> libraries() {
  std::vector<LibInfo> l;
  l.push_back({"r8brain", "r8brain-free-src", R8B_VERSION, "MIT", true, "sample-rate conversion, 64-bit, built in"});
  l.push_back({"soxr", "libsoxr", soxrAvailable() && soxr().version ? std::string(soxr().version()).substr(std::string(soxr().version()).rfind("libsoxr-", 0) == 0 ? 8 : 0) : std::string("0.1.3"), "LGPL-2.1", soxrAvailable(), soxrAvailable() ? "loaded at run time (replaceable)" : "amix_soxr library not next to the program: r8brain is used"});
  l.push_back({"dr_wav", "dr_wav", "0.14.6", "public domain / MIT-0", true, "WAV, RF64, Wave64: PCM 8-64 bit, float 32/64"});
  l.push_back({"dr_flac", "dr_flac", "0.13.4", "public domain / MIT-0", true, "FLAC reading"});
  l.push_back({"libflac", "libFLAC", FLAC__VERSION_STRING, "BSD-3-Clause (Xiph)", true, "FLAC writing: 16 / 24 / 32-bit"});
  return l;
}
static std::string jsonEsc(const std::string& s) { std::string o; for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; } return o; }
std::string infoJson() {
  std::string j = "{\"libraries\":[";
  bool first = true; for (const LibInfo& i : libraries()) { j += (first ? "" : ",") + std::string("{\"id\":\"") + jsonEsc(i.id) + "\",\"name\":\"" + jsonEsc(i.name) + "\",\"version\":\"" + jsonEsc(i.version) + "\",\"licence\":\"" + jsonEsc(i.licence) + "\",\"available\":" + (i.available ? "true" : "false") + ",\"note\":\"" + jsonEsc(i.note) + "\"}"; first = false; }
  j += "],\"resamplers\":[\"r8brain\""; if (soxrAvailable()) j += ",\"soxr\""; j += "],\"read\":[\"wav\",\"rf64\",\"w64\",\"flac\"],\"write\":[";
  first = true; for (const std::string& f : writeFormats()) { j += (first ? "\"" : ",\"") + f + "\""; first = false; }
  j += "],\"dither\":[\"none\",\"tpdf\",\"shaped\"],\"maxFlacRate\":1048575}";
  return j;
}

std::unique_ptr<Resampler> Resampler::make(double src, double dst, int ch, const std::string& engine, int quality, std::string& err) {
  if (!(src > 0) || !(dst > 0) || ch < 1 || ch > 64) { err = "resampler: rates must be above 0 and channels 1 .. 64"; return nullptr; }
  const bool wantSoxr = engine == "soxr" || (engine == "auto" && soxrAvailable());
  if (engine != "auto" && engine != "soxr" && engine != "r8brain") { err = "resampler: unknown engine \"" + engine + "\" (auto, r8brain, soxr)"; return nullptr; }
  if (wantSoxr) {
    if (!soxrAvailable()) { err = "soxr: the amix_soxr library is not installed next to the program"; return nullptr; }
    auto s = std::make_unique<SoxrR>(src, dst, ch, quality, err); if (s->ok()) return s; if (engine == "soxr") return nullptr; err.clear(); }
  return std::make_unique<R8b>(src, dst, ch, quality);
}

bool resample(const std::vector<double>& x, int ch, double src, double dst, const std::string& engine, int quality, std::vector<double>& out, std::string& err, std::string* used) {
  if (ch < 1 || x.size() % static_cast<std::size_t>(ch)) { err = "resample: the buffer is not a whole number of frames"; return false; }
  const std::size_t frames = x.size() / static_cast<std::size_t>(ch);
  if (src == dst) { out = x; if (used) *used = "none"; return true; }
  auto r = Resampler::make(src, dst, ch, engine, quality, err); if (!r) return false; if (used) *used = r->engine();
  std::vector<std::vector<double>> planar(static_cast<std::size_t>(ch), std::vector<double>(frames)); for (std::size_t i = 0; i < frames; ++i) for (int c = 0; c < ch; ++c) planar[static_cast<std::size_t>(c)][i] = x[i * static_cast<std::size_t>(ch) + static_cast<std::size_t>(c)];
  std::vector<const double*> in(static_cast<std::size_t>(ch)); for (int c = 0; c < ch; ++c) in[static_cast<std::size_t>(c)] = planar[static_cast<std::size_t>(c)].data();
  std::vector<std::vector<double>> got; r->process(in.data(), static_cast<int>(frames), got); r->flush(got);
  const std::size_t want = static_cast<std::size_t>(std::llround(static_cast<double>(frames) * dst / src));
  out.assign(want * static_cast<std::size_t>(ch), 0.0);
  for (int c = 0; c < ch; ++c) { const auto& g = got[static_cast<std::size_t>(c)]; for (std::size_t i = 0; i < want && i < g.size(); ++i) out[i * static_cast<std::size_t>(ch) + static_cast<std::size_t>(c)] = g[i]; }
  return true;
}

std::vector<float> resampleMono(const std::vector<float>& x, double from, double to, int quality) {
  if (from == to || x.empty() || !(from > 0) || !(to > 0)) return x;
  std::vector<double> d(x.begin(), x.end()), o; std::string err;
  if (!resample(d, 1, from, to, "r8brain", quality, o, err)) return x;
  return std::vector<float>(o.begin(), o.end());
}

// ---- reading
// the header only: rate, channels, length and word length without decoding the audio (a file list)
bool probe(const std::string& path, AudioInfo& info, std::string& err) {
  std::string ext = path.substr(path.find_last_of('.') + 1); for (char& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  if (ext == "flac") {
#ifdef _WIN32
    drflac* f = drflac_open_file_w(wide(path).c_str(), nullptr);
#else
    drflac* f = drflac_open_file(path.c_str(), nullptr);
#endif
    if (!f) { err = "not a FLAC file that can be read: " + path; return false; }
    info = {static_cast<double>(f->sampleRate), f->channels, static_cast<std::int64_t>(f->totalPCMFrameCount), f->bitsPerSample, false, "flac"}; drflac_close(f); return true;
  }
  drwav w;
#ifdef _WIN32
  const bool ok = drwav_init_file_w(&w, wide(path).c_str(), nullptr);
#else
  const bool ok = drwav_init_file(&w, path.c_str(), nullptr);
#endif
  if (!ok) { err = "not a WAV / RF64 / Wave64 file that can be read: " + path; return false; }
  info = {static_cast<double>(w.sampleRate), w.channels, static_cast<std::int64_t>(w.totalPCMFrameCount), w.bitsPerSample, w.translatedFormatTag == DR_WAVE_FORMAT_IEEE_FLOAT, w.container == drwav_container_rf64 ? "rf64" : w.container == drwav_container_w64 ? "w64" : "wav"};
  drwav_uninit(&w); return true;
}
bool read(const std::string& path, Audio& out, std::string& err) {
  out = Audio{};
  std::string ext = path.substr(path.find_last_of('.') + 1); for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (ext == "flac") {
#ifdef _WIN32
    drflac* f = drflac_open_file_w(wide(path).c_str(), nullptr);
#else
    drflac* f = drflac_open_file(path.c_str(), nullptr);
#endif
    if (!f) { err = "not a FLAC file that can be read: " + path; return false; }
    out.info = {static_cast<double>(f->sampleRate), f->channels, static_cast<std::int64_t>(f->totalPCMFrameCount), f->bitsPerSample, false, "flac"};
    std::vector<std::int32_t> s(static_cast<std::size_t>(f->totalPCMFrameCount) * f->channels);
    const drflac_uint64 got = drflac_read_pcm_frames_s32(f, f->totalPCMFrameCount, s.data()); drflac_close(f);
    out.samples.resize(static_cast<std::size_t>(got) * static_cast<std::size_t>(out.info.channels)); for (std::size_t i = 0; i < out.samples.size(); ++i) out.samples[i] = s[i] / 2147483648.0;
    out.info.frames = static_cast<std::int64_t>(got); return true;
  }
  drwav w;
#ifdef _WIN32
  const bool ok = drwav_init_file_w(&w, wide(path).c_str(), nullptr);
#else
  const bool ok = drwav_init_file(&w, path.c_str(), nullptr);
#endif
  if (!ok) { err = "not a WAV / RF64 / Wave64 file that can be read: " + path; return false; }
  const bool isFloat = w.translatedFormatTag == DR_WAVE_FORMAT_IEEE_FLOAT;
  out.info = {static_cast<double>(w.sampleRate), w.channels, static_cast<std::int64_t>(w.totalPCMFrameCount), w.bitsPerSample, isFloat, w.container == drwav_container_rf64 ? "rf64" : w.container == drwav_container_w64 ? "w64" : "wav"};
  const std::size_t n = static_cast<std::size_t>(w.totalPCMFrameCount) * w.channels; out.samples.resize(n);
  drwav_uint64 got = 0;
  if (isFloat && w.bitsPerSample == 64) {   // a 64-bit float file keeps every bit: read the raw doubles
    got = drwav_read_pcm_frames(&w, w.totalPCMFrameCount, out.samples.data());
  } else if (isFloat) { std::vector<float> f(n); got = drwav_read_pcm_frames_f32(&w, w.totalPCMFrameCount, f.data()); for (std::size_t i = 0; i < n; ++i) out.samples[i] = f[i]; }
  else { std::vector<std::int32_t> s(n); got = drwav_read_pcm_frames_s32(&w, w.totalPCMFrameCount, s.data()); for (std::size_t i = 0; i < n; ++i) out.samples[i] = s[i] / 2147483648.0; }
  drwav_uninit(&w);
  out.samples.resize(static_cast<std::size_t>(got) * w.channels); out.info.frames = static_cast<std::int64_t>(got);
  if (got == 0 && n) { err = "the file holds no readable audio (unsupported sample format?)"; return false; }
  return true;
}

// ---- dither
Ditherer::Ditherer(int channels, int bits, const std::string& mode, std::uint32_t seed) : ch_(std::max(1, channels)), bits_(std::clamp(bits, 8, 32)), mode_(mode == "shaped" ? 2 : mode == "tpdf" ? 1 : 0), rng_(seed ? seed : 1u), err_(static_cast<std::size_t>(std::max(1, channels)) * 5, 0.0) {
  scale_ = std::ldexp(1.0, bits_ - 1); lo_ = -scale_; hi_ = scale_ - 1;
}
double Ditherer::rnd() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return rng_ / 4294967296.0; }   // xorshift32, 0 .. 1
std::int32_t Ditherer::next(double x, int c) {
  double v = x * scale_; double* e = &err_[static_cast<std::size_t>(c % ch_) * 5];
  if (mode_ == 2) for (int k = 0; k < 5; ++k) v -= kShape[k] * e[k];
  const double d = mode_ ? rnd() - rnd() : 0.0;   // triangular, +-1 LSB
  double y = std::floor(v + d + 0.5); y = std::clamp(y, lo_, hi_);
  if (mode_ == 2) { for (int k = 4; k > 0; --k) e[k] = e[k - 1]; e[0] = y - v; }
  return static_cast<std::int32_t>(y);
}

// ---- writing
std::vector<std::string> writeFormats() { return {"wav16", "wav24", "wav32", "wavf32", "wavf64", "flac16", "flac24", "flac32"}; }

// the streaming writer: one file, blocks of interleaved frames, the header finished on close()
struct Writer::Impl {
  int ch = 0; bool flac = false, isFloat = false, open = false; int bits = 0; std::string err;
  FLAC__StreamEncoder* enc = nullptr; drwav w{}; std::unique_ptr<Ditherer> d; std::vector<FLAC__int32> ib; std::vector<unsigned char> bb; std::uint64_t frames = 0;
};
Writer::Writer() : i_(new Impl) {}
Writer::~Writer() { close(); }
std::uint64_t Writer::frames() const { return i_->frames; }
std::unique_ptr<Writer> Writer::make(const std::string& path, int ch, double rate, const std::string& format, const std::string& dither, std::string& err, std::uint64_t expectedFrames, int flacLevel) {
  if (ch < 1 || ch > 64) { err = "write: 1 .. 64 channels"; return nullptr; }
  if (!(rate >= 1)) { err = "write: the sample rate must be at least 1 Hz"; return nullptr; }
  const auto& fm = writeFormats(); if (std::find(fm.begin(), fm.end(), format) == fm.end()) { err = "write: unknown format \"" + format + "\""; return nullptr; }
  if (dither != "none" && dither != "tpdf" && dither != "shaped") { err = "write: dither is none, tpdf or shaped"; return nullptr; }
  std::unique_ptr<Writer> wr(new Writer); Impl& I = *wr->i_; I.ch = ch;
  if (format.rfind("flac", 0) == 0) {
    I.flac = true; I.bits = std::stoi(format.substr(4));
    if (rate > 1048575 || ch > 8) { err = "FLAC takes up to 8 channels and 1,048,575 Hz"; return nullptr; }
    I.enc = FLAC__stream_encoder_new(); if (!I.enc) { err = "FLAC: out of memory"; return nullptr; }
    FLAC__stream_encoder_set_channels(I.enc, static_cast<unsigned>(ch)); FLAC__stream_encoder_set_bits_per_sample(I.enc, static_cast<unsigned>(I.bits)); FLAC__stream_encoder_set_sample_rate(I.enc, static_cast<unsigned>(std::lround(rate)));
    FLAC__stream_encoder_set_compression_level(I.enc, static_cast<unsigned>(std::clamp(flacLevel, 0, 8))); if (expectedFrames) FLAC__stream_encoder_set_total_samples_estimate(I.enc, expectedFrames); FLAC__stream_encoder_set_verify(I.enc, true);
    const FLAC__StreamEncoderInitStatus st = FLAC__stream_encoder_init_file(I.enc, path.c_str(), nullptr, nullptr);
    if (st != FLAC__STREAM_ENCODER_INIT_STATUS_OK) { err = std::string("FLAC: ") + FLAC__StreamEncoderInitStatusString[st]; FLAC__stream_encoder_delete(I.enc); I.enc = nullptr; return nullptr; }
  } else {
    I.isFloat = format.rfind("wavf", 0) == 0; I.bits = std::stoi(format.substr(I.isFloat ? 4 : 3));
    const std::uint64_t bytes = expectedFrames * static_cast<std::uint64_t>(ch) * static_cast<std::uint64_t>(I.bits / 8);
    drwav_data_format f{}; f.container = bytes > 0xFFFFFF00ull - 4096 ? drwav_container_rf64 : drwav_container_riff;   // known to pass 4 GB: RF64 (EBU Tech 3306)
    f.format = I.isFloat ? DR_WAVE_FORMAT_IEEE_FLOAT : DR_WAVE_FORMAT_PCM; f.channels = static_cast<drwav_uint32>(ch); f.sampleRate = static_cast<drwav_uint32>(std::lround(rate)); f.bitsPerSample = static_cast<drwav_uint32>(I.bits);
#ifdef _WIN32
    if (!drwav_init_file_write_w(&I.w, wide(path).c_str(), &f, nullptr)) { err = "cannot create " + path; return nullptr; }
#else
    if (!drwav_init_file_write(&I.w, path.c_str(), &f, nullptr)) { err = "cannot create " + path; return nullptr; }
#endif
  }
  I.d = std::make_unique<Ditherer>(ch, I.bits, dither); I.open = true; return wr;
}
bool Writer::write(const double* x, std::size_t frames) {
  Impl& I = *i_; if (!I.open) return false; const std::size_t ch = static_cast<std::size_t>(I.ch);
  for (std::size_t at = 0; at < frames; at += 4096) {
    const std::size_t n = std::min<std::size_t>(4096, frames - at), m = n * ch; const double* src = x + at * ch;
    if (I.flac) {
      I.ib.resize(m); for (std::size_t k = 0; k < m; ++k) I.ib[k] = I.d->next(src[k], static_cast<int>(k % ch));
      if (!FLAC__stream_encoder_process_interleaved(I.enc, I.ib.data(), static_cast<unsigned>(n))) { I.err = std::string("FLAC: ") + FLAC__stream_encoder_get_resolved_state_string(I.enc); return false; }
    } else {
      const std::size_t bps = static_cast<std::size_t>(I.bits / 8); I.bb.resize(m * bps); unsigned char* p = I.bb.data();
      for (std::size_t k = 0; k < m; ++k, p += bps) { const double v = src[k];
        if (I.isFloat && I.bits == 64) std::memcpy(p, &v, 8);
        else if (I.isFloat) { const float s = static_cast<float>(v); std::memcpy(p, &s, 4); }
        else { const std::int32_t s = I.d->next(v, static_cast<int>(k % ch)); for (std::size_t b = 0; b < bps; ++b) p[b] = static_cast<unsigned char>((static_cast<std::uint32_t>(s) >> (8 * b)) & 0xFF); } }
      if (drwav_write_pcm_frames(&I.w, n, I.bb.data()) != n) { I.err = "write failed (the disk is full?)"; return false; }
    }
    I.frames += n;
  }
  return true;
}
bool Writer::close(std::string* err) {
  Impl& I = *i_; if (!I.open) return I.err.empty(); I.open = false; bool ok = I.err.empty();
  if (I.flac) { if (!FLAC__stream_encoder_finish(I.enc) && ok) { ok = false; I.err = "FLAC: the encoder could not finish (verify failed or the disk is full)"; } FLAC__stream_encoder_delete(I.enc); I.enc = nullptr; }
  else drwav_uninit(&I.w);
  if (err && !ok) *err = I.err; return ok;
}

bool write(const std::string& path, const std::vector<double>& x, int ch, double rate, const std::string& format, const std::string& dither, std::string& err, int flacLevel) {
  if (ch < 1 || x.size() % static_cast<std::size_t>(ch)) { err = "write: 1 .. 64 channels and whole frames"; return false; }
  const std::size_t frames = x.size() / static_cast<std::size_t>(ch);
  auto w = Writer::make(path, ch, rate, format, dither, err, frames, flacLevel); if (!w) return false;
  const bool wrote = w->write(x.data(), frames); std::string e; const bool closed = w->close(&e);
  if (!wrote || !closed) { if (err.empty()) err = e.empty() ? "write failed (the disk is full?)" : e; return false; }
  return true;
}

}  // namespace am::hr
