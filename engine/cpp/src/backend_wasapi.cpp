// WASAPI backend (Windows, shared mode, event driven): render with an event, capture polled once per block. Float32 at the requested rate and channel count: the Windows audio
// engine converts (AUTOCONVERTPCM). The default render / capture endpoints are used; a device is chosen by its endpoint id string ("" = default).
// Compiled with mingw-w64 in the tests (scripts/build-engine.js --io); it has not been run on a Windows PC.
#ifdef _WIN32
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <windows.h>

#include <atomic>
#include <cstring>
#include <deque>
#include <thread>

#include "backends.hpp"

namespace am {
namespace {
const GUID kClsidEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
const GUID kIidEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
const GUID kIidAudioClient = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
const GUID kIidRenderClient = {0xF294ACFC, 0x3146, 0x4483, {0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2}};
const GUID kIidCaptureClient = {0xC8ADBD64, 0xE71E, 0x48A0, {0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17}};
const GUID kFloatSubFormat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

template <class T> void release(T*& p) { if (p) { p->Release(); p = nullptr; } }
std::string hr(const char* what, HRESULT r) { char b[96]; std::snprintf(b, sizeof b, "%s failed (0x%08lx)", what, static_cast<unsigned long>(r)); return b; }

class WasapiBackend : public Backend {
 public:
  ~WasapiBackend() override { stop(); closeAll(); }
  const char* name() const override { return "wasapi"; }
  bool open(const DeviceConfig& cfg, AudioCallback cb, std::string& err) override {
    cfg_ = cfg; cb_ = std::move(cb);
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) ) { /* already initialised by the host: fine */ }
    IMMDeviceEnumerator* en = nullptr; HRESULT r = CoCreateInstance(kClsidEnumerator, nullptr, CLSCTX_ALL, kIidEnumerator, reinterpret_cast<void**>(&en));
    if (FAILED(r)) { err = hr("device enumerator", r); return false; }
    const bool wantOut = cfg.outChannels > 0 && cfg.outDevice != "-", wantIn = cfg.inChannels > 0 && cfg.inDevice != "-";
    bool ok = true;
    if (wantOut) ok = client(en, eRender, cfg.outChannels, &out_, &render_, nullptr, err);
    if (ok && wantIn) ok = client(en, eCapture, cfg.inChannels, &in_, nullptr, &capture_, err);
    en->Release();
    if (!ok) { closeAll(); return false; }
    ev_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (out_) out_->SetEventHandle(ev_);
    const std::size_t cap = static_cast<std::size_t>(cfg.frames) * 16;
    inBuf_.assign(static_cast<std::size_t>(std::max(1, cfg.inChannels)), std::vector<float>(cap, 0.0f)); outBuf_.assign(static_cast<std::size_t>(std::max(1, cfg.outChannels)), std::vector<float>(cap, 0.0f));
    for (auto& v : inBuf_) inp_.push_back(v.data());
    for (auto& v : outBuf_) outp_.push_back(v.data());
    fifo_.assign(static_cast<std::size_t>(std::max(1, cfg.inChannels)), std::deque<float>());
    return true;
  }
  bool start(std::string& err) override {
    if (out_ && FAILED(out_->Start())) { err = "cannot start the render stream"; return false; }
    if (in_ && FAILED(in_->Start())) { err = "cannot start the capture stream"; return false; }
    stop_ = false; thread_ = std::thread([this] { loop(); }); return true;
  }
  void stop() override { stop_ = true; if (ev_) SetEvent(ev_); if (thread_.joinable()) thread_.join(); if (out_) out_->Stop(); if (in_) in_->Stop(); }
  std::uint64_t xruns() const override { return xruns_; }
 private:
  bool client(IMMDeviceEnumerator* en, EDataFlow flow, int ch, IAudioClient** ac, IAudioRenderClient** rc, IAudioCaptureClient** cc, std::string& err) {
    IMMDevice* dev = nullptr; HRESULT r = en->GetDefaultAudioEndpoint(flow, eConsole, &dev);
    if (FAILED(r)) { err = hr("default endpoint", r); return false; }
    r = dev->Activate(kIidAudioClient, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(ac)); dev->Release();
    if (FAILED(r)) { err = hr("audio client", r); return false; }
    WAVEFORMATEXTENSIBLE f; std::memset(&f, 0, sizeof f);
    f.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE; f.Format.nChannels = static_cast<WORD>(ch); f.Format.nSamplesPerSec = static_cast<DWORD>(cfg_.sampleRate); f.Format.wBitsPerSample = 32;
    f.Format.nBlockAlign = static_cast<WORD>(ch * 4); f.Format.nAvgBytesPerSec = f.Format.nSamplesPerSec * f.Format.nBlockAlign; f.Format.cbSize = 22; f.Samples.wValidBitsPerSample = 32;
    f.dwChannelMask = ch == 1 ? SPEAKER_FRONT_CENTER : ch == 2 ? (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT) : 0; f.SubFormat = kFloatSubFormat;
    const REFERENCE_TIME dur = static_cast<REFERENCE_TIME>(cfg_.frames * 3.0 / cfg_.sampleRate * 1e7);
    DWORD flags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY | (flow == eRender ? AUDCLNT_STREAMFLAGS_EVENTCALLBACK : 0);
    r = (*ac)->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, dur, 0, &f.Format, nullptr);
    if (FAILED(r)) { err = hr("Initialize", r); return false; }
    r = flow == eRender ? (*ac)->GetService(kIidRenderClient, reinterpret_cast<void**>(rc)) : (*ac)->GetService(kIidCaptureClient, reinterpret_cast<void**>(cc));
    if (FAILED(r)) { err = hr("GetService", r); return false; }
    return true;
  }
  void loop() {
    const int ci = cfg_.inChannels, co = cfg_.outChannels; UINT32 bufFrames = 0; if (out_) out_->GetBufferSize(&bufFrames);
    while (!stop_) {
      if (out_) WaitForSingleObject(ev_, 100); else Sleep(static_cast<DWORD>(1000.0 * cfg_.frames / cfg_.sampleRate));
      if (stop_) break;
      if (capture_) {                                                          // everything the capture side has: into per-channel FIFOs
        UINT32 n = 0;
        while (SUCCEEDED(capture_->GetNextPacketSize(&n)) && n > 0) {
          BYTE* data; UINT32 got; DWORD fl;
          if (FAILED(capture_->GetBuffer(&data, &got, &fl, nullptr, nullptr))) break;
          const float* x = reinterpret_cast<const float*>(data);
          for (UINT32 i = 0; i < got; ++i) for (int c = 0; c < ci; ++c) fifo_[static_cast<std::size_t>(c)].push_back((fl & AUDCLNT_BUFFERFLAGS_SILENT) ? 0.0f : x[i * static_cast<UINT32>(ci) + static_cast<UINT32>(c)]);
          capture_->ReleaseBuffer(got);
        }
      }
      UINT32 pad = 0, room = bufFrames;
      if (out_ && SUCCEEDED(out_->GetCurrentPadding(&pad))) room = bufFrames - pad;
      while (room >= cfg_.frames && !stop_) {
        for (int c = 0; c < ci; ++c) { auto& q = fifo_[static_cast<std::size_t>(c)]; for (UINT32 i = 0; i < cfg_.frames; ++i) { if (q.empty()) { inBuf_[static_cast<std::size_t>(c)][i] = 0.0f; ++xruns_; } else { inBuf_[static_cast<std::size_t>(c)][i] = q.front(); q.pop_front(); } } }
        cb_(inp_.data(), outp_.data(), cfg_.frames);
        if (render_) {
          BYTE* data; if (FAILED(render_->GetBuffer(cfg_.frames, &data))) break;
          float* y = reinterpret_cast<float*>(data);
          for (UINT32 i = 0; i < cfg_.frames; ++i) for (int c = 0; c < co; ++c) y[i * static_cast<UINT32>(co) + static_cast<UINT32>(c)] = outBuf_[static_cast<std::size_t>(c)][i];
          render_->ReleaseBuffer(cfg_.frames, 0);
        }
        room -= cfg_.frames;
        if (!out_) break;
      }
      for (auto& q : fifo_) while (q.size() > cfg_.frames * 16) q.pop_front();      // never let the capture side run away
    }
  }
  void closeAll() { release(render_); release(capture_); release(out_); release(in_); if (ev_) { CloseHandle(ev_); ev_ = nullptr; } }
  DeviceConfig cfg_; AudioCallback cb_; IAudioClient *out_ = nullptr, *in_ = nullptr; IAudioRenderClient* render_ = nullptr; IAudioCaptureClient* capture_ = nullptr; HANDLE ev_ = nullptr;
  std::vector<std::vector<float>> inBuf_, outBuf_; std::vector<const float*> inp_; std::vector<float*> outp_; std::vector<std::deque<float>> fifo_;
  std::thread thread_; std::atomic<bool> stop_{false}; std::atomic<std::uint64_t> xruns_{0};
};
}  // namespace
std::unique_ptr<Backend> makeWasapiBackend() { return std::make_unique<WasapiBackend>(); }
}  // namespace am
#endif
