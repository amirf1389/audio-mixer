#include "arrange/decode.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <mfapi.h>
#  include <mfidl.h>
#  include <mfreadwrite.h>
#  include <mferror.h>
#else
#  include <sys/wait.h>
#  include <unistd.h>
#  include <cstdlib>
#endif

namespace am::arr {
namespace {

bool slurp(const std::string& path, std::vector<std::uint8_t>& d, std::string& err) {
  std::ifstream f(path, std::ios::binary); if (!f) { err = "cannot open " + path; return false; }
  f.seekg(0, std::ios::end); const auto n = f.tellg(); if (n <= 0) { err = "empty file"; return false; }
  if (static_cast<std::uint64_t>(n) > 2ull * 1024 * 1024 * 1024) { err = "the file is larger than 2 GB"; return false; }
  d.resize(static_cast<std::size_t>(n)); f.seekg(0); f.read(reinterpret_cast<char*>(d.data()), n);
  return static_cast<bool>(f);
}
std::string lower(std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
std::string extOf(const std::string& p) { const auto d = p.find_last_of('.'), s = p.find_last_of("/\\"); return d == std::string::npos || (s != std::string::npos && d < s) ? "" : lower(p.substr(d + 1)); }

// ── FLAC ──
struct Bits {
  const std::uint8_t* p; std::size_t n, pos = 0;   // pos in bits
  bool ok = true;
  std::uint64_t get(int k) {   // k <= 57
    std::uint64_t v = 0;
    for (int i = 0; i < k; ++i) { const std::size_t byte = pos >> 3; if (byte >= n) { ok = false; return 0; } v = (v << 1) | ((p[byte] >> (7 - (pos & 7))) & 1u); ++pos; }
    return v;
  }
  std::int64_t sget(int k) { if (k == 0) return 0; const std::uint64_t v = get(k); return (v >> (k - 1)) & 1u ? static_cast<std::int64_t>(v) - (static_cast<std::int64_t>(1) << k) : static_cast<std::int64_t>(v); }
  std::uint64_t unary() { std::uint64_t z = 0; while (ok && get(1) == 0) { if (++z > (1u << 26)) { ok = false; break; } } return z; }
  void align() { pos = (pos + 7) & ~static_cast<std::size_t>(7); }
  bool utf8(std::uint64_t& v) {   // the frame / sample number
    std::uint64_t x = get(8); int extra = 0;
    if (!(x & 0x80)) { v = x; return ok; }
    if ((x & 0xE0) == 0xC0) { v = x & 0x1F; extra = 1; } else if ((x & 0xF0) == 0xE0) { v = x & 0x0F; extra = 2; } else if ((x & 0xF8) == 0xF0) { v = x & 0x07; extra = 3; }
    else if ((x & 0xFC) == 0xF8) { v = x & 0x03; extra = 4; } else if ((x & 0xFE) == 0xFC) { v = x & 0x01; extra = 5; } else if (x == 0xFE) { v = 0; extra = 6; } else return false;
    for (int i = 0; i < extra; ++i) { const std::uint64_t c = get(8); if ((c & 0xC0) != 0x80) return false; v = (v << 6) | (c & 0x3F); }
    return ok;
  }
};

bool residual(Bits& b, std::vector<std::int64_t>& r, int order, int blocksize) {
  const int method = static_cast<int>(b.get(2)); if (method > 1) return false;
  const int parts = static_cast<int>(b.get(4)), np = 1 << parts, pbits = method == 0 ? 4 : 5, esc = method == 0 ? 15 : 31;
  if ((blocksize >> parts) < order) return false;
  for (int pi = 0; pi < np; ++pi) {
    const int n = (pi == 0 ? (blocksize >> parts) - order : (blocksize >> parts)), start = pi == 0 ? order : pi * (blocksize >> parts);
    const int k = static_cast<int>(b.get(pbits));
    if (k == esc) { const int raw = static_cast<int>(b.get(5)); for (int i = 0; i < n; ++i) r[static_cast<std::size_t>(start + i)] = b.sget(raw); }
    else for (int i = 0; i < n; ++i) { const std::uint64_t q = b.unary(); const std::uint64_t u = (q << k) | (k ? b.get(k) : 0); r[static_cast<std::size_t>(start + i)] = (u & 1u) ? -static_cast<std::int64_t>(u >> 1) - 1 : static_cast<std::int64_t>(u >> 1); }
    if (!b.ok) return false;
  }
  return true;
}

bool subframe(Bits& b, std::vector<std::int64_t>& s, int blocksize, int bps) {
  if (b.get(1) != 0) return false;   // padding bit
  const int type = static_cast<int>(b.get(6)); int wasted = 0;
  if (b.get(1)) { wasted = 1; while (b.ok && b.get(1) == 0) ++wasted; }
  bps -= wasted; if (bps <= 0 || bps > 40) return false;
  s.assign(static_cast<std::size_t>(blocksize), 0);
  if (type == 0) { const std::int64_t v = b.sget(bps); std::fill(s.begin(), s.end(), v); }
  else if (type == 1) { for (int i = 0; i < blocksize; ++i) s[static_cast<std::size_t>(i)] = b.sget(bps); }
  else if (type >= 8 && type <= 12) {   // FIXED, order 0..4
    const int order = type - 8; for (int i = 0; i < order; ++i) s[static_cast<std::size_t>(i)] = b.sget(bps);
    if (!residual(b, s, order, blocksize)) return false;
    for (int i = order; i < blocksize; ++i) { auto& x = s[static_cast<std::size_t>(i)]; const auto* v = s.data() + i;
      switch (order) { case 1: x += v[-1]; break; case 2: x += 2 * v[-1] - v[-2]; break; case 3: x += 3 * v[-1] - 3 * v[-2] + v[-3]; break; case 4: x += 4 * v[-1] - 6 * v[-2] + 4 * v[-3] - v[-4]; break; default: break; } }
  } else if (type >= 32) {   // LPC, order 1..32
    const int order = type - 31; for (int i = 0; i < order; ++i) s[static_cast<std::size_t>(i)] = b.sget(bps);
    const int prec = static_cast<int>(b.get(4)) + 1; if (prec == 16) return false;
    const int shift = static_cast<int>(b.sget(5)); if (shift < 0) return false;
    std::int64_t c[32]; for (int i = 0; i < order; ++i) c[i] = b.sget(prec);
    if (!residual(b, s, order, blocksize)) return false;
    for (int i = order; i < blocksize; ++i) { std::int64_t acc = 0; for (int j = 0; j < order; ++j) acc += c[j] * s[static_cast<std::size_t>(i - 1 - j)]; s[static_cast<std::size_t>(i)] += acc >> shift; }
  } else return false;
  if (wasted) for (auto& x : s) x <<= wasted;
  return b.ok;
}

}  // namespace

bool readFlac(const std::string& path, Audio& out, std::string& err) {
  std::vector<std::uint8_t> d; if (!slurp(path, d, err)) return false;
  if (d.size() < 42 || std::memcmp(d.data(), "fLaC", 4) != 0) { err = "not a FLAC file"; return false; }
  std::size_t p = 4; unsigned rate = 0, channels = 0, bps = 0; std::uint64_t total = 0; bool last = false;
  while (!last) {   // metadata blocks; STREAMINFO is the first
    if (p + 4 > d.size()) { err = "the FLAC header is cut short"; return false; }
    last = d[p] & 0x80; const int type = d[p] & 0x7F; const std::size_t len = (static_cast<std::size_t>(d[p + 1]) << 16) | (d[p + 2] << 8) | d[p + 3]; p += 4;
    if (p + len > d.size()) { err = "the FLAC header is cut short"; return false; }
    if (type == 0 && len >= 34) {
      Bits b{d.data() + p, len}; b.get(16); b.get(16); b.get(24); b.get(24);
      rate = static_cast<unsigned>(b.get(20)); channels = static_cast<unsigned>(b.get(3)) + 1; bps = static_cast<unsigned>(b.get(5)) + 1; total = b.get(36);
    }
    p += len;
  }
  if (!rate || !channels || bps < 4 || bps > 32) { err = "the FLAC stream description is missing or not supported"; return false; }
  out = Audio{}; out.rate = rate; if (total) { out.L.reserve(static_cast<std::size_t>(total)); out.R.reserve(static_cast<std::size_t>(total)); }
  const double scale = 1.0 / static_cast<double>(1ull << (bps - 1));
  std::vector<std::vector<std::int64_t>> ch(channels);
  while (p + 2 <= d.size()) {
    if (!(d[p] == 0xFF && (d[p + 1] & 0xFE) == 0xF8)) { ++p; continue; }   // find the next frame sync
    Bits b{d.data() + p, d.size() - p}; b.get(15); b.get(1);
    const int bsCode = static_cast<int>(b.get(4)), srCode = static_cast<int>(b.get(4)), chCode = static_cast<int>(b.get(4)), szCode = static_cast<int>(b.get(3)); b.get(1);
    std::uint64_t num = 0; if (!b.utf8(num) || bsCode == 0 || chCode > 10 || szCode == 3 || szCode == 7) { ++p; continue; }
    int blocksize = 0;
    if (bsCode == 1) blocksize = 192; else if (bsCode >= 2 && bsCode <= 5) blocksize = 576 << (bsCode - 2);
    else if (bsCode == 6) blocksize = static_cast<int>(b.get(8)) + 1; else if (bsCode == 7) blocksize = static_cast<int>(b.get(16)) + 1; else blocksize = 256 << (bsCode - 8);
    if (srCode == 12) b.get(8); else if (srCode == 13 || srCode == 14) b.get(16); else if (srCode == 15) { ++p; continue; }
    b.get(8);   // header CRC-8 (the frame CRC-16 at the end also protects the samples; a damaged frame shows up as a failed decode)
    static const int kSize[8] = {0, 8, 12, 0, 16, 20, 24, 32}; const int fbps = szCode == 0 ? static_cast<int>(bps) : kSize[szCode];
    const unsigned nch = chCode < 8 ? static_cast<unsigned>(chCode) + 1 : 2;
    if (nch != channels || !b.ok || blocksize <= 0) { ++p; continue; }
    bool good = true;
    for (unsigned c = 0; c < nch && good; ++c) { const int extra = (chCode == 8 && c == 1) || (chCode == 9 && c == 0) || (chCode == 10 && c == 1) ? 1 : 0; good = subframe(b, ch[c], blocksize, fbps + extra); }
    if (!good) { ++p; continue; }
    b.align(); b.get(16);
    if (chCode == 8) for (int i = 0; i < blocksize; ++i) ch[1][static_cast<std::size_t>(i)] = ch[0][static_cast<std::size_t>(i)] - ch[1][static_cast<std::size_t>(i)];        // left / side
    else if (chCode == 9) for (int i = 0; i < blocksize; ++i) ch[0][static_cast<std::size_t>(i)] += ch[1][static_cast<std::size_t>(i)];                                       // side / right
    else if (chCode == 10) for (int i = 0; i < blocksize; ++i) { std::int64_t m = ch[0][static_cast<std::size_t>(i)] << 1, sd = ch[1][static_cast<std::size_t>(i)]; m |= sd & 1; ch[0][static_cast<std::size_t>(i)] = (m + sd) >> 1; ch[1][static_cast<std::size_t>(i)] = (m - sd) >> 1; }   // mid / side
    for (int i = 0; i < blocksize; ++i) { const float l = static_cast<float>(static_cast<double>(ch[0][static_cast<std::size_t>(i)]) * scale); out.L.push_back(l); out.R.push_back(nch > 1 ? static_cast<float>(static_cast<double>(ch[1][static_cast<std::size_t>(i)]) * scale) : l); }
    p += b.pos / 8;
  }
  if (out.L.empty()) { err = "no audio frames could be decoded in this FLAC file"; return false; }
  if (total && out.L.size() > total) { out.L.resize(static_cast<std::size_t>(total)); out.R.resize(static_cast<std::size_t>(total)); }
  return true;
}

// ── AIFF / AIFF-C (big-endian PCM, or 'sowt' little-endian, or fl32 / fl64) ──
bool readAiff(const std::string& path, Audio& out, std::string& err) {
  std::vector<std::uint8_t> d; if (!slurp(path, d, err)) return false;
  if (d.size() < 12 || std::memcmp(d.data(), "FORM", 4) || (std::memcmp(d.data() + 8, "AIFF", 4) && std::memcmp(d.data() + 8, "AIFC", 4))) { err = "not an AIFF file"; return false; }
  const auto be32 = [&](std::size_t i) { return (static_cast<std::uint32_t>(d[i]) << 24) | (d[i + 1] << 16) | (d[i + 2] << 8) | d[i + 3]; };
  unsigned ch = 0, bits = 0; std::uint32_t frames = 0; double rate = 0; std::string comp = "NONE"; std::size_t data = 0, dataLen = 0;
  for (std::size_t p = 12; p + 8 <= d.size();) {
    const std::uint32_t len = be32(p + 4); const std::size_t body = p + 8; if (body + len > d.size()) break;
    if (!std::memcmp(d.data() + p, "COMM", 4) && len >= 18) {
      ch = (d[body] << 8) | d[body + 1]; frames = be32(body + 2); bits = (d[body + 6] << 8) | d[body + 7];
      const int ex = ((d[body + 8] & 0x7F) << 8) | d[body + 9]; std::uint64_t m = 0; for (int i = 0; i < 8; ++i) m = (m << 8) | d[body + 10 + i];
      rate = std::ldexp(static_cast<double>(m), ex - 16383 - 63);
      if (len >= 22) comp = std::string(reinterpret_cast<const char*>(d.data() + body + 18), 4);
    } else if (!std::memcmp(d.data() + p, "SSND", 4) && len >= 8) { const std::uint32_t off = be32(body); data = body + 8 + off; dataLen = len - 8 - off; }
    p = body + len + (len & 1);
  }
  const bool sowt = comp == "sowt", f32 = comp == "fl32" || comp == "FL32", f64 = comp == "fl64" || comp == "FL64";
  if (!ch || !rate || !data || !(comp == "NONE" || comp == "twos" || sowt || f32 || f64)) { err = "this AIFF file uses a compression that is not supported (" + comp + ")"; return false; }
  const unsigned bytes = f32 ? 4 : f64 ? 8 : (bits + 7) / 8; if (!bytes || bytes > 8) { err = "unsupported AIFF sample size"; return false; }
  const std::size_t n = std::min<std::size_t>(frames, dataLen / (bytes * ch));
  out = Audio{}; out.rate = rate; out.L.resize(n); out.R.resize(n);
  for (std::size_t i = 0; i < n; ++i) for (unsigned c = 0; c < std::min(ch, 2u); ++c) {
    const std::uint8_t* s = d.data() + data + (i * ch + c) * bytes; double v;
    if (f32 || f64) { std::uint64_t u = 0; for (unsigned k = 0; k < bytes; ++k) u = (u << 8) | s[k]; if (f32) { const std::uint32_t w = static_cast<std::uint32_t>(u); float f; std::memcpy(&f, &w, 4); v = f; } else { double g; std::memcpy(&g, &u, 8); v = g; } }
    else { std::int64_t x = 0; for (unsigned k = 0; k < bytes; ++k) x = (x << 8) | s[sowt ? bytes - 1 - k : k]; const int sh = 64 - static_cast<int>(bytes) * 8; x = (x << sh) >> sh; v = static_cast<double>(x) / static_cast<double>(1ull << (bytes * 8 - 1)); }
    (c == 0 ? out.L : out.R)[i] = static_cast<float>(v);
  }
  if (ch == 1) out.R = out.L;
  return true;
}

namespace {
#ifdef _WIN32
std::wstring widen(const std::string& s) { const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0); std::wstring w(n > 0 ? n - 1 : 0, L'\0'); if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n); return w; }
// the system's decoders: anything Media Foundation can open (AAC / M4A, MP3, WMA, ALAC, FLAC, and OGG / Opus with the Web Media Extensions), as float stereo
bool readSystem(const std::string& path, Audio& out, std::string& err) {
  const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED); const bool coOk = SUCCEEDED(co);
  if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET))) { if (coOk) CoUninitialize(); err = "the Windows media decoders are not available"; return false; }
  IMFSourceReader* r = nullptr; bool ok = false;
  if (SUCCEEDED(MFCreateSourceReaderFromURL(widen(path).c_str(), nullptr, &r))) {
    r->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE); r->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
    IMFMediaType* want = nullptr; MFCreateMediaType(&want);
    want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio); want->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
    if (SUCCEEDED(r->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, want))) {
      IMFMediaType* got = nullptr; UINT32 ch = 2, rate = 48000;
      if (SUCCEEDED(r->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &got))) { got->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch); got->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate); got->Release(); }
      out = Audio{}; out.rate = rate; ok = ch > 0;
      while (ok) {
        DWORD flags = 0; IMFSample* s = nullptr;
        if (FAILED(r->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &s))) { ok = false; break; }
        if (s) { IMFMediaBuffer* buf = nullptr;
          if (SUCCEEDED(s->ConvertToContiguousBuffer(&buf))) { BYTE* p = nullptr; DWORD len = 0;
            if (SUCCEEDED(buf->Lock(&p, nullptr, &len))) { const float* f = reinterpret_cast<const float*>(p); const std::size_t n = len / (4u * ch);
              for (std::size_t i = 0; i < n; ++i) { out.L.push_back(f[i * ch]); out.R.push_back(ch > 1 ? f[i * ch + 1] : f[i * ch]); } buf->Unlock(); }
            buf->Release(); }
          s->Release(); }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (out.L.size() > static_cast<std::size_t>(rate) * 60 * 60 * 3) { err = "longer than 3 hours"; ok = false; break; }
      }
      if (ok && out.L.empty()) { ok = false; err = "no audio in the file"; }
    } else err = "Windows has no decoder for this file (for OGG / Opus install the Web Media Extensions from the Microsoft Store)";
    want->Release(); r->Release();
  } else err = "Windows cannot open this file as audio";
  MFShutdown(); if (coOk) CoUninitialize();
  return ok;
}
#else
// ffmpeg, when installed: the file is turned into a temporary 32-bit float WAV and read back; arguments go straight to exec, never through a shell
bool readSystem(const std::string& path, Audio& out, std::string& err) {
  char tmpl[] = "/tmp/amfx1-decode-XXXXXX"; const int fd = mkstemp(tmpl); if (fd < 0) { err = "no temporary file"; return false; } close(fd);
  const std::string tmp = std::string(tmpl) + ".wav"; std::rename(tmpl, tmp.c_str());
  std::fflush(nullptr);   // nothing buffered is written twice by the child
  const pid_t pid = fork(); if (pid < 0) { std::remove(tmp.c_str()); err = "cannot start ffmpeg"; return false; }
  if (pid == 0) {
    const char* argv[] = {"ffmpeg", "-nostdin", "-v", "error", "-y", "-i", path.c_str(), "-vn", "-ac", "2", "-c:a", "pcm_f32le", "-f", "wav", tmp.c_str(), nullptr};
    if (!std::freopen("/dev/null", "w", stdout)) _exit(126); execvp("ffmpeg", const_cast<char* const*>(argv)); _exit(127);
  }
  int st = 0; waitpid(pid, &st, 0);
  const bool ran = WIFEXITED(st) && WEXITSTATUS(st) == 0, ok = ran && readWav(tmp, out, err);
  std::remove(tmp.c_str());
  if (!ran) err = WIFEXITED(st) && WEXITSTATUS(st) == 127 ? "this kind of file needs ffmpeg on this system (WAV, FLAC and AIFF are read without it)" : "ffmpeg could not decode this file";
  return ok;
}
#endif
}  // namespace

bool readAudio(const std::string& path, Audio& out, std::string& err) {
  std::ifstream f(path, std::ios::binary); char m[12] = {}; if (!f) { err = "cannot open " + path; return false; } f.read(m, 12);
  if (!std::memcmp(m, "RIFF", 4) || !std::memcmp(m, "RF64", 4)) return readWav(path, out, err);
  if (!std::memcmp(m, "fLaC", 4)) return readFlac(path, out, err);
  if (!std::memcmp(m, "FORM", 4) && (!std::memcmp(m + 8, "AIFF", 4) || !std::memcmp(m + 8, "AIFC", 4))) return readAiff(path, out, err);
  return readSystem(path, out, err);
}

std::vector<std::string> audioExtensions() { return {"wav", "flac", "aif", "aiff", "aifc", "m4a", "aac", "mp4", "mp3", "wma", "ogg", "oga", "opus", "alac", "caf", "mka", "webm"}; }
bool isAudioFile(const std::string& path) { const std::string e = extOf(path); const auto l = audioExtensions(); return std::find(l.begin(), l.end(), e) != l.end(); }

}  // namespace am::arr
