// DirectSound backend (Windows): the oldest Windows path, and the one every Bluetooth headset / speaker, USB-class device and on-board codec
// answers to, with no exclusive lock on the device (Windows mixes it with other programs) and no driver of its own. Render: a streaming
// secondary buffer of 8 periods, 16-bit PCM, written one period at a time ahead of the write cursor from a worker thread. Capture: a
// DirectSoundCapture buffer read the same way. The device is chosen by its GUID string ("{...}", as listDevices reports it) or by a part of
// its name; "" = the Windows default. Latency is 2-3 periods plus the Bluetooth / codec path, so this is the fallback after ASIO and
// WASAPI, and the path to use for Bluetooth. Cross-compiled with mingw-w64 (-Werror); run against real devices only on a Windows PC.
#ifdef _WIN32
#include "audiomixer/sample_format.hpp"
#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "audiomixer/sysopt.hpp"
#include "backends.hpp"

namespace am {
namespace {
const GUID kIidBuffer8 = {0x6825a449, 0x7524, 0x4d82, {0x92, 0x0f, 0x50, 0xe3, 0x6a, 0xb3, 0xab, 0x1e}};          // IID_IDirectSoundBuffer8
const GUID kIidCaptureBuffer8 = {0x00990df4, 0x0dbb, 0x4872, {0x83, 0x3e, 0x6d, 0x30, 0x3e, 0x80, 0xae, 0xb6}};   // IID_IDirectSoundCaptureBuffer8

std::string lower(std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
std::string hr(const char* what, HRESULT r) { char b[96]; std::snprintf(b, sizeof b, "%s failed (0x%08lx)", what, static_cast<unsigned long>(r)); return b; }
template <class T> void release(T*& p) { if (p) { p->Release(); p = nullptr; } }

struct Found { GUID guid{}; bool any = false; bool isDefault = false; std::string name; };
struct Want { std::string key; Found found; };
BOOL CALLBACK onDevice(LPGUID guid, LPCSTR desc, LPCSTR, LPVOID ctx) {
  Want* w = static_cast<Want*>(ctx); if (w->found.any) return TRUE;
  std::string id = "default"; if (guid) { char b[64]; std::snprintf(b, sizeof b, "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", static_cast<unsigned long>(guid->Data1), guid->Data2, guid->Data3, guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3], guid->Data4[4], guid->Data4[5], guid->Data4[6], guid->Data4[7]); id = b; }
  const std::string name = desc ? desc : "";
  if (w->key.empty() || w->key == "default" || lower(id) == lower(w->key) || lower(name).find(lower(w->key)) != std::string::npos) { w->found.any = true; w->found.isDefault = guid == nullptr; if (guid) w->found.guid = *guid; w->found.name = name; }
  return TRUE;
}

class DsoundBackend : public Backend {
 public:
  ~DsoundBackend() override { stop(); closeAll(); }
  const char* name() const override { return "dsound"; }

  bool open(const DeviceConfig& cfg, AudioCallback cb, std::string& err) override {
    cfg_ = cfg; cb_ = std::move(cb); CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    frames_ = std::max<unsigned>(32, cfg.frames); nOut_ = std::clamp(cfg.outChannels, 0, 8); nIn_ = std::clamp(cfg.inChannels, 0, 8);
    const bool wantOut = cfg.outDevice != "-" && nOut_ > 0, wantIn = cfg.inDevice != "-" && nIn_ > 0;
    if (!wantOut && !wantIn) { err = "nothing to open: no input and no output asked for"; return false; }
    if (wantOut) {
      Want w; w.key = cfg.outDevice; DirectSoundEnumerateA(&onDevice, &w);
      if (!w.found.any) { err = "no DirectSound output named \"" + cfg.outDevice + "\""; return false; }
      HRESULT r = DirectSoundCreate8(w.found.isDefault ? nullptr : &w.found.guid, &ds_, nullptr); if (FAILED(r)) { err = hr("DirectSoundCreate8", r); closeAll(); return false; }
      r = ds_->SetCooperativeLevel(GetDesktopWindow(), DSSCL_PRIORITY); if (FAILED(r)) { err = hr("SetCooperativeLevel", r); closeAll(); return false; }
      WAVEFORMATEX f = format(nOut_); DSBUFFERDESC d; std::memset(&d, 0, sizeof d); d.dwSize = sizeof d; d.dwFlags = DSBCAPS_GLOBALFOCUS | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_CTRLPOSITIONNOTIFY; d.dwBufferBytes = outBytes(); d.lpwfxFormat = &f;
      IDirectSoundBuffer* b = nullptr; r = ds_->CreateSoundBuffer(&d, &b, nullptr); if (FAILED(r)) { err = hr("CreateSoundBuffer", r); closeAll(); return false; }
      r = b->QueryInterface(kIidBuffer8, reinterpret_cast<void**>(&out_)); b->Release(); if (FAILED(r) || !out_) { err = hr("IDirectSoundBuffer8", r); closeAll(); return false; }
      outName_ = w.found.name;
    }
    if (wantIn) {
      Want w; w.key = cfg.inDevice; DirectSoundCaptureEnumerateA(&onDevice, &w);
      if (!w.found.any) { err = "no DirectSound input named \"" + cfg.inDevice + "\""; closeAll(); return false; }
      HRESULT r = DirectSoundCaptureCreate8(w.found.isDefault ? nullptr : &w.found.guid, &dsc_, nullptr); if (FAILED(r)) { err = hr("DirectSoundCaptureCreate8", r); closeAll(); return false; }
      WAVEFORMATEX f = format(nIn_); DSCBUFFERDESC d; std::memset(&d, 0, sizeof d); d.dwSize = sizeof d; d.dwBufferBytes = inBytes(); d.lpwfxFormat = &f;
      IDirectSoundCaptureBuffer* b = nullptr; r = dsc_->CreateCaptureBuffer(&d, &b, nullptr); if (FAILED(r)) { err = hr("CreateCaptureBuffer", r); closeAll(); return false; }
      r = b->QueryInterface(kIidCaptureBuffer8, reinterpret_cast<void**>(&in_)); b->Release(); if (FAILED(r) || !in_) { err = hr("IDirectSoundCaptureBuffer8", r); closeAll(); return false; }
      inName_ = w.found.name;
    }
    inF_.assign(static_cast<std::size_t>(std::max(1, nIn_)), std::vector<float>(frames_, 0.0f)); outF_.assign(static_cast<std::size_t>(std::max(1, nOut_)), std::vector<float>(frames_, 0.0f));
    zeros_.assign(frames_, 0.0f); sink_.assign(frames_, 0.0f); inp_.clear(); outp_.clear();
    for (int i = 0; i < nIn_; ++i) inp_.push_back(inF_[static_cast<std::size_t>(i)].data());
    while (static_cast<int>(inp_.size()) < cfg.inChannels) inp_.push_back(zeros_.data());
    for (int i = 0; i < nOut_; ++i) outp_.push_back(outF_[static_cast<std::size_t>(i)].data());
    while (static_cast<int>(outp_.size()) < cfg.outChannels) outp_.push_back(sink_.data());
    scratch_.assign(static_cast<std::size_t>(frames_) * 8 * 2, 0);
    return true;
  }
  bool start(std::string& err) override {
    if (!out_ && !in_) { err = "open() first"; return false; }
    if (out_) { // two periods of silence ahead of the cursor, then play
      DWORD play = 0, write = 0; out_->GetCurrentPosition(&play, &write); writePos_ = (write + periodBytes(nOut_)) % outBytes(); clearOut(); HRESULT r = out_->Play(0, 0, DSBPLAY_LOOPING); if (FAILED(r)) { err = hr("Play", r); return false; } }
    if (in_) { HRESULT r = in_->Start(DSCBSTART_LOOPING); if (FAILED(r)) { err = hr("capture Start", r); if (out_) out_->Stop(); return false; } DWORD cap = 0, rd = 0; in_->GetCurrentPosition(&cap, &rd); readPos_ = rd; }
    running_ = true; worker_ = std::thread([this] { loop(); });
    return true;
  }
  void stop() override { if (!running_.exchange(false)) return; if (worker_.joinable()) worker_.join(); if (out_) out_->Stop(); if (in_) in_->Stop(); }
  std::uint64_t xruns() const override { return xruns_; }
  void latencyFrames(long& in, long& out) const override { const long q = static_cast<long>(frames_) * kPeriods; in = nIn_ > 0 ? q : 0; out = nOut_ > 0 ? q : 0; }   // its queued periods (Windows' own engine adds more)
  const std::string& outputName() const { return outName_; }
  const std::string& inputName() const { return inName_; }

 private:
  std::string sampleFormat() const override { return "16-bit integer (DirectSound)"; }
  WAVEFORMATEX format(int ch) const { WAVEFORMATEX f; std::memset(&f, 0, sizeof f); f.wFormatTag = WAVE_FORMAT_PCM; f.nChannels = static_cast<WORD>(std::max(1, ch)); f.nSamplesPerSec = static_cast<DWORD>(cfg_.sampleRate); f.wBitsPerSample = 16; f.nBlockAlign = static_cast<WORD>(f.nChannels * 2); f.nAvgBytesPerSec = f.nSamplesPerSec * f.nBlockAlign; return f; }
  DWORD periodBytes(int ch) const { return static_cast<DWORD>(frames_) * static_cast<DWORD>(std::max(1, ch)) * 2; }
  DWORD outBytes() const { return periodBytes(nOut_) * kPeriods; }
  DWORD inBytes() const { return periodBytes(nIn_) * kPeriods; }
  void clearOut() { void* p1 = nullptr; void* p2 = nullptr; DWORD n1 = 0, n2 = 0; if (SUCCEEDED(out_->Lock(0, 0, &p1, &n1, &p2, &n2, DSBLOCK_ENTIREBUFFER))) { if (p1) std::memset(p1, 0, n1); if (p2) std::memset(p2, 0, n2); out_->Unlock(p1, n1, p2, n2); } }
  // one period: read the capture ring (zeros when it has not filled a period yet), run the mixer, write the render ring
  void period() {
    if (in_) {
      DWORD cap = 0, rd = 0; const DWORD need = periodBytes(nIn_), total = inBytes();
      if (SUCCEEDED(in_->GetCurrentPosition(&cap, &rd)) && ((rd + total - readPos_) % total) >= need) {
        void* p1 = nullptr; void* p2 = nullptr; DWORD n1 = 0, n2 = 0;
        if (SUCCEEDED(in_->Lock(readPos_, need, &p1, &n1, &p2, &n2, 0))) { if (p1) std::memcpy(scratch_.data(), p1, n1); if (p2) std::memcpy(scratch_.data() + n1, p2, n2); in_->Unlock(p1, n1, p2, n2); readPos_ = (readPos_ + need) % total;
          const std::int16_t* s = reinterpret_cast<const std::int16_t*>(scratch_.data()); for (unsigned i = 0; i < frames_; ++i) for (int c = 0; c < nIn_; ++c) inF_[static_cast<std::size_t>(c)][i] = am::fmt::fromI16(s[i * nIn_ + c]); }
      } else for (auto& v : inF_) std::fill(v.begin(), v.end(), 0.0f);
    }
    for (auto& v : outF_) std::fill(v.begin(), v.end(), 0.0f);
    cb_(inp_.data(), outp_.data(), frames_);
    if (out_) {
      const DWORD need = periodBytes(nOut_); std::int16_t* d = reinterpret_cast<std::int16_t*>(scratch_.data());
      for (unsigned i = 0; i < frames_; ++i) for (int c = 0; c < nOut_; ++c) { const float x = outF_[static_cast<std::size_t>(c)][i]; d[i * nOut_ + c] = am::fmt::toI16(x); }   // rounded, clamped (sample_format.hpp)
      void* p1 = nullptr; void* p2 = nullptr; DWORD n1 = 0, n2 = 0;
      if (SUCCEEDED(out_->Lock(writePos_, need, &p1, &n1, &p2, &n2, 0))) { if (p1) std::memcpy(p1, scratch_.data(), n1); if (p2) std::memcpy(p2, scratch_.data() + n1, n2); out_->Unlock(p1, n1, p2, n2); }
      writePos_ = (writePos_ + need) % outBytes();
    }
  }
  void loop() {
    am::sysopt::tuneAudioThread();   // never slowed for power; the fast cores on a hybrid processor
    timeBeginPeriod(1); const DWORD total = outBytes(), need = out_ ? periodBytes(nOut_) : 0;
    while (running_.load()) {
      if (out_) {
        DWORD play = 0, write = 0; if (FAILED(out_->GetCurrentPosition(&play, &write))) { Sleep(1); continue; }
        DWORD ahead = (writePos_ + total - write) % total;                     // how far our data is ahead of the write cursor
        if (ahead > total / 2) { ++xruns_; writePos_ = (write + need) % total; ahead = need; }   // the cursor overtook us: restart ahead of it
        int guard = kPeriods; while (ahead < 2 * need && guard-- > 0) { period(); ahead += need; }
      } else if (in_) {   // input only: one block per period the capture ring really delivered (it used to run twice per period, every other block silent)
        DWORD cap = 0, rd = 0; const DWORD needIn = periodBytes(nIn_), totalIn = inBytes(); int guard = kPeriods;
        while (guard-- > 0 && SUCCEEDED(in_->GetCurrentPosition(&cap, &rd)) && ((rd + totalIn - readPos_) % totalIn) >= needIn) period();
      } else period();
      const double ms = 1000.0 * frames_ / cfg_.sampleRate; Sleep(static_cast<DWORD>(std::max(1.0, ms / 2)));
    }
    timeEndPeriod(1);
  }
  void closeAll() { release(out_); release(in_); release(ds_); release(dsc_); }

  static constexpr DWORD kPeriods = 8;
  DeviceConfig cfg_; AudioCallback cb_; unsigned frames_ = 256; int nIn_ = 0, nOut_ = 0;
  IDirectSound8* ds_ = nullptr; IDirectSoundBuffer8* out_ = nullptr; IDirectSoundCapture8* dsc_ = nullptr; IDirectSoundCaptureBuffer8* in_ = nullptr;
  DWORD writePos_ = 0, readPos_ = 0; std::string outName_, inName_;
  std::vector<std::vector<float>> inF_, outF_; std::vector<float> zeros_, sink_; std::vector<const float*> inp_; std::vector<float*> outp_; std::vector<unsigned char> scratch_;
  std::atomic<bool> running_{false}; std::atomic<std::uint64_t> xruns_{0}; std::thread worker_;
};
}  // namespace
std::unique_ptr<Backend> makeDsoundBackend() { return std::make_unique<DsoundBackend>(); }
}  // namespace am
#endif
