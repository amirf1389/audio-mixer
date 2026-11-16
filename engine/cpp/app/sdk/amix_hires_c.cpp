// The hi-res part of the C SDK (sdk/c/include/amix_hires.h) over am::hr (app/hires).
#define AMIX_BUILD_DLL
#include "amix_hires.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "hires/hires.hpp"

namespace {
void fail(char* err, std::size_t len, const std::string& m) { if (err && len) { std::strncpy(err, m.c_str(), len - 1); err[len - 1] = '\0'; } }
char* dupStr(const std::string& s) { char* p = static_cast<char*>(std::malloc(s.size() + 1)); if (p) std::memcpy(p, s.c_str(), s.size() + 1); return p; }
}  // namespace

struct amix_hr_resampler {
  std::unique_ptr<am::hr::Resampler> r; std::vector<std::deque<double>> ready; bool flushed = false;
  void take(std::vector<std::vector<double>>& got) { for (std::size_t c = 0; c < ready.size() && c < got.size(); ++c) ready[c].insert(ready[c].end(), got[c].begin(), got[c].end()); }
};

struct amix_hr_writer { std::unique_ptr<am::hr::Writer> w; std::vector<double> tmp; };

extern "C" {

char* amix_hr_info_json(void) { return dupStr(am::hr::infoJson()); }

amix_hr_resampler* amix_hr_resampler_create(double src, double dst, int ch, const char* engine, int quality, char* err, size_t len) {
  std::string e; auto r = am::hr::Resampler::make(src, dst, ch, engine && *engine ? engine : "auto", quality, e);
  if (!r) { fail(err, len, e); return nullptr; }
  auto* h = new amix_hr_resampler; h->r = std::move(r); h->ready.resize(static_cast<std::size_t>(ch)); return h;
}
void amix_hr_resampler_destroy(amix_hr_resampler* h) { delete h; }
const char* amix_hr_resampler_engine(const amix_hr_resampler* h) { return h ? h->r->engine() : ""; }
int amix_hr_resampler_pending(const amix_hr_resampler* h) { return h && !h->ready.empty() ? static_cast<int>(h->ready[0].size()) : 0; }
int amix_hr_resampler_process(amix_hr_resampler* h, const double* const* in, int frames, double* const* out, int cap) {
  if (!h || frames < 0 || cap < 0) return -1;
  std::vector<std::vector<double>> got;
  if (in) { if (frames > 0) { h->r->process(in, frames, got); h->take(got); } }
  else if (!h->flushed) { h->r->flush(got); h->take(got); h->flushed = true; }
  const int n = std::min(cap, amix_hr_resampler_pending(h));
  for (std::size_t c = 0; c < h->ready.size(); ++c) { for (int i = 0; i < n; ++i) { if (out && out[c]) out[c][i] = h->ready[c].front(); h->ready[c].pop_front(); } }
  return n;
}

int amix_hr_resample(const double* x, int64_t frames, int ch, double src, double dst, const char* engine, int quality, double** out, int64_t* outFrames, char* err, size_t len) {
  if (!x || !out || !outFrames || ch < 1 || frames < 0) { fail(err, len, "amix_hr_resample: bad arguments"); return -1; }
  std::vector<double> in(x, x + frames * ch), o; std::string e;
  if (!am::hr::resample(in, ch, src, dst, engine && *engine ? engine : "auto", quality, o, e)) { fail(err, len, e); return -2; }
  *out = static_cast<double*>(std::malloc(std::max<std::size_t>(1, o.size()) * sizeof(double))); if (!*out) { fail(err, len, "out of memory"); return -3; }
  std::copy(o.begin(), o.end(), *out); *outFrames = static_cast<int64_t>(o.size() / static_cast<std::size_t>(ch)); return 0;
}

double* amix_hr_read(const char* path, amix_hr_info* info, char* err, size_t len) {
  if (!path) { fail(err, len, "amix_hr_read: no path"); return nullptr; }
  am::hr::Audio a; std::string e; if (!am::hr::read(path, a, e)) { fail(err, len, e); return nullptr; }
  if (info) { info->rate = a.info.rate; info->channels = a.info.channels; info->frames = a.info.frames; info->bits = a.info.bits; info->is_float = a.info.isFloat ? 1 : 0; std::memset(info->container, 0, sizeof info->container); std::strncpy(info->container, a.info.container.c_str(), sizeof info->container - 1); }
  double* p = static_cast<double*>(std::malloc(std::max<std::size_t>(1, a.samples.size()) * sizeof(double))); if (!p) { fail(err, len, "out of memory"); return nullptr; }
  std::copy(a.samples.begin(), a.samples.end(), p); return p;
}

int amix_hr_write(const char* path, const double* x, int64_t frames, int ch, double rate, const char* format, const char* dither, char* err, size_t len) {
  if (!path || (!x && frames > 0) || ch < 1 || frames < 0) { fail(err, len, "amix_hr_write: bad arguments"); return -1; }
  std::vector<double> in(x, x + frames * ch); std::string e;
  if (!am::hr::write(path, in, ch, rate, format ? format : "wav24", dither ? dither : "tpdf", e)) { fail(err, len, e); return -2; }
  return 0;
}

amix_hr_writer* amix_hr_writer_open(const char* path, int ch, double rate, const char* format, const char* dither, char* err, size_t len) {
  if (!path) { fail(err, len, "amix_hr_writer_open: no path"); return nullptr; }
  std::string e; auto w = am::hr::Writer::make(path, ch, rate, format ? format : "wav24", dither ? dither : "tpdf", e); if (!w) { fail(err, len, e); return nullptr; }
  auto* h = new amix_hr_writer; h->w = std::move(w); return h;
}
int amix_hr_writer_write(amix_hr_writer* h, const double* x, int64_t frames) { if (!h || !x || frames < 0) return -1; return h->w->write(x, static_cast<std::size_t>(frames)) ? 0 : -2; }
int amix_hr_writer_write_f32(amix_hr_writer* h, const float* x, int64_t frames, int channels) {
  if (!h || !x || frames < 0 || channels < 1) return -1; h->tmp.assign(x, x + frames * channels); return h->w->write(h->tmp.data(), static_cast<std::size_t>(frames)) ? 0 : -2;
}
int64_t amix_hr_writer_frames(const amix_hr_writer* h) { return h ? static_cast<int64_t>(h->w->frames()) : 0; }
int amix_hr_writer_close(amix_hr_writer* h, char* err, size_t len) { if (!h) return -1; std::string e; const bool ok = h->w->close(&e); if (!ok) fail(err, len, e); delete h; return ok ? 0 : -2; }

int amix_hr_quantize(const double* in, int64_t n, int ch, int bits, const char* dither, uint32_t seed, int32_t* out) {
  if (!in || !out || ch < 1 || n < 0 || bits < 8 || bits > 32) return -1;
  am::hr::Ditherer d(ch, bits, dither ? dither : "tpdf", seed); for (int64_t i = 0; i < n; ++i) out[i] = d.next(in[i], static_cast<int>(i % ch)); return 0;
}

}  // extern "C"
