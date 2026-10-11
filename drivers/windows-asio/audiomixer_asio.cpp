// "Audio Mixer ASIO": an ASIO driver (in-process COM server, a DLL) that appears in every ASIO host (DAWs, OBS, ...) as an audio interface called
// "Audio Mixer" and connects it to the running Audio Mixer program:
//
//   ASIO outputs  (what the host PLAYS)    ->  the mixer reads them as an input source (device "Audio Mixer" in LIVE SOURCES: READ)
//   ASIO inputs   (what the host RECORDS)  <-  what the mixer sends to the device (LIVE SOURCES: WRITE)
//
// 2 in / 2 out, 32-bit integer samples (ASIOSTInt32LSB), buffers of 64..2048 frames (power of two), 44.1 / 48 / 88.2 / 96 kHz.
// The driver has no hardware clock: a high-resolution timer thread paces the buffer switches at the real sample rate.
// The connection to the mixer (drivers/common/am_link.c) is opened in start() and closed in stop(); if the mixer is not running the driver still runs, silent,
// and tries again every second.
//
// Registration is a deliberate step and nothing the installer does: run  regsvr32 AudioMixerASIO64.dll  as administrator (see README.md).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <olectl.h>
#include <mmsystem.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asio_iface.h"
extern "C" {
#include "../common/am_link.h"
}

// {C1893F2F-1AD5-4344-9806-DCFD242C0D48}: the class id AND (by ASIO convention) the interface id the hosts ask for
static const GUID CLSID_AudioMixerAsio = { 0xc1893f2f, 0x1ad5, 0x4344, { 0x98, 0x06, 0xdc, 0xfd, 0x24, 0x2c, 0x0d, 0x48 } };
static const char *const kDriverName = "Audio Mixer";
static const wchar_t *const kClsidText = L"{C1893F2F-1AD5-4344-9806-DCFD242C0D48}";
static const long kChannels = 2;
static const long kMinBuffer = 64, kMaxBuffer = 2048, kDefaultBuffer = 256;
static const size_t kRingFrames = 16384;

static HINSTANCE g_module = nullptr;
static volatile LONG g_locks = 0, g_objects = 0;

static bool supportedRate(double r) { return r == 44100.0 || r == 48000.0 || r == 88200.0 || r == 96000.0; }

class AudioMixerAsio final : public IASIO {
 public:
  AudioMixerAsio() : refs_(1) {
    InterlockedIncrement(&g_objects);
    InitializeCriticalSection(&ring_lock_);
    ring_ = new short[kRingFrames * kChannels]();
    strcpy(error_, "");
  }

  // ───────── IUnknown ─────────
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == CLSID_AudioMixerAsio) { *ppv = static_cast<IASIO *>(this); AddRef(); return S_OK; }   // ASIO hosts ask with the class id
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&refs_); }
  ULONG STDMETHODCALLTYPE Release() override {
    LONG n = InterlockedDecrement(&refs_);
    if (n == 0) { delete this; }
    return (ULONG)n;
  }

  // ───────── IASIO ─────────
  ASIOBool init(void *sysHandle) override { (void)sysHandle; inited_ = true; return ASIOTrue; }
  void getDriverName(char *name) override { strcpy(name, kDriverName); }
  long getDriverVersion() override { return 1; }
  void getErrorMessage(char *string) override { strncpy(string, error_, 123); string[123] = 0; }

  ASIOError start() override {
    if (!buffers_ready_ || !callbacks_) return ASE_NotPresent;
    if (running_) return ASE_OK;
    sample_pos_ = 0; buffer_index_ = 0;
    running_ = true;
    thread_ = CreateThread(nullptr, 0, &AudioMixerAsio::threadMain, this, 0, nullptr);
    if (!thread_) { running_ = false; strcpy(error_, "cannot start the driver thread"); return ASE_HWMalfunction; }
    SetThreadPriority(thread_, THREAD_PRIORITY_TIME_CRITICAL);
    return ASE_OK;
  }
  ASIOError stop() override {
    if (!running_) return ASE_OK;
    running_ = false;
    if (thread_) { WaitForSingleObject(thread_, 3000); CloseHandle(thread_); thread_ = nullptr; }
    if (link_) { am_link_close(link_); link_ = nullptr; }
    return ASE_OK;
  }

  ASIOError getChannels(long *numInputChannels, long *numOutputChannels) override {
    if (!numInputChannels || !numOutputChannels) return ASE_InvalidParameter;
    *numInputChannels = kChannels; *numOutputChannels = kChannels;
    return ASE_OK;
  }
  ASIOError getLatencies(long *inputLatency, long *outputLatency) override {
    long b = buffer_size_ ? buffer_size_ : kDefaultBuffer;
    if (inputLatency) *inputLatency = b;
    if (outputLatency) *outputLatency = b;
    return ASE_OK;
  }
  ASIOError getBufferSize(long *minSize, long *maxSize, long *preferredSize, long *granularity) override {
    if (minSize) *minSize = kMinBuffer;
    if (maxSize) *maxSize = kMaxBuffer;
    if (preferredSize) *preferredSize = kDefaultBuffer;
    if (granularity) *granularity = -1;                       // powers of two between min and max
    return ASE_OK;
  }
  ASIOError canSampleRate(ASIOSampleRate sampleRate) override { return supportedRate(sampleRate) ? ASE_OK : ASE_NoClock; }
  ASIOError getSampleRate(ASIOSampleRate *sampleRate) override { if (!sampleRate) return ASE_InvalidParameter; *sampleRate = rate_; return ASE_OK; }
  ASIOError setSampleRate(ASIOSampleRate sampleRate) override {
    if (!supportedRate(sampleRate)) return ASE_NoClock;
    if (running_ && sampleRate != rate_) return ASE_InvalidMode;     // the host stops the driver before it changes the rate
    rate_ = sampleRate;
    if (callbacks_ && callbacks_->sampleRateDidChange) callbacks_->sampleRateDidChange(rate_);
    return ASE_OK;
  }
  ASIOError getClockSources(ASIOClockSource *clocks, long *numSources) override {
    if (!clocks || !numSources || *numSources < 1) return ASE_InvalidParameter;
    memset(clocks, 0, sizeof *clocks);
    clocks[0].index = 0; clocks[0].associatedChannel = -1; clocks[0].associatedGroup = -1; clocks[0].isCurrentSource = ASIOTrue;
    strcpy(clocks[0].name, "Internal");
    *numSources = 1;
    return ASE_OK;
  }
  ASIOError setClockSource(long reference) override { return reference == 0 ? ASE_OK : ASE_NotPresent; }
  ASIOError getSamplePosition(ASIOSamples *sPos, ASIOTimeStamp *tStamp) override {
    if (!sPos || !tStamp) return ASE_InvalidParameter;
    unsigned long long s = sample_pos_, t = stamp_ns_;
    sPos->hi = (unsigned long)(s >> 32); sPos->lo = (unsigned long)(s & 0xffffffffu);
    tStamp->hi = (unsigned long)(t >> 32); tStamp->lo = (unsigned long)(t & 0xffffffffu);
    return running_ ? ASE_OK : ASE_SPNotAdvancing;
  }
  ASIOError getChannelInfo(ASIOChannelInfo *info) override {
    if (!info || info->channel < 0 || info->channel >= kChannels) return ASE_InvalidParameter;
    info->type = ASIOSTInt32LSB; info->channelGroup = 0;
    info->isActive = (info->isInput ? in_active_ : out_active_)[info->channel] ? ASIOTrue : ASIOFalse;
    // an ASIO input is what the mixer sends to the device ("Mixer"); an ASIO output is what the host plays into the mixer ("To mixer")
    sprintf(info->name, info->isInput ? "Mixer %s" : "To mixer %s", info->channel == 0 ? "L" : "R");
    return ASE_OK;
  }

  ASIOError createBuffers(ASIOBufferInfo *infos, long numChannels, long bufferSize, ASIOCallbacks *callbacks) override {
    if (!infos || !callbacks || numChannels < 1 || numChannels > 2 * kChannels) return ASE_InvalidParameter;
    if (bufferSize < kMinBuffer || bufferSize > kMaxBuffer || (bufferSize & (bufferSize - 1)) != 0) return ASE_InvalidMode;
    if (running_) return ASE_InvalidMode;
    disposeBuffers();
    for (long i = 0; i < numChannels; i++) {
      if (infos[i].channelNum < 0 || infos[i].channelNum >= kChannels) return ASE_InvalidMode;
    }
    for (long i = 0; i < numChannels; i++) {
      int *b0 = (int *)calloc((size_t)bufferSize, sizeof(int)), *b1 = (int *)calloc((size_t)bufferSize, sizeof(int));
      if (!b0 || !b1) { free(b0); free(b1); disposeBuffers(); return ASE_NoMemory; }
      infos[i].buffers[0] = b0; infos[i].buffers[1] = b1;
      Chan &c = chans_[nchans_++];
      c.input = infos[i].isInput != 0; c.num = infos[i].channelNum; c.buf[0] = b0; c.buf[1] = b1;
      (c.input ? in_active_ : out_active_)[c.num] = true;
    }
    buffer_size_ = bufferSize; callbacks_ = callbacks; buffers_ready_ = true;
    return ASE_OK;
  }
  ASIOError disposeBuffers() override {
    if (running_) stop();
    for (int i = 0; i < nchans_; i++) { free(chans_[i].buf[0]); free(chans_[i].buf[1]); }
    nchans_ = 0; in_active_[0] = in_active_[1] = out_active_[0] = out_active_[1] = false;
    buffers_ready_ = false; callbacks_ = nullptr; buffer_size_ = 0;
    return ASE_OK;
  }
  ASIOError controlPanel() override {
    char text[320];
    bool up = link_ && am_link_alive(link_);
    sprintf(text, "Audio Mixer ASIO driver\n\nMixer connection: %s\nSample rate: %.0f Hz   Buffer: %ld frames\n\n%s",
            up ? "connected" : (running_ ? "NOT connected (start Audio Mixer; the driver keeps trying)" : "not started yet"), rate_, buffer_size_ ? buffer_size_ : kDefaultBuffer,
            "In the mixer open LIVE SOURCES, find the device \"Audio Mixer\" and switch READ (host playback) and WRITE (host recording) on.");
    MessageBoxA(nullptr, text, "Audio Mixer ASIO", MB_OK | MB_ICONINFORMATION);
    return ASE_OK;
  }
  ASIOError future(long selector, void *opt) override { (void)selector; (void)opt; return ASE_InvalidParameter; }
  ASIOError outputReady() override { return ASE_NotPresent; }

 private:
  struct Chan { bool input; long num; int *buf[2]; };

  ~AudioMixerAsio() {
    stop(); disposeBuffers();
    DeleteCriticalSection(&ring_lock_);
    delete[] ring_;
    InterlockedDecrement(&g_objects);
  }

  // PCM from the mixer (link thread) -> ring
  static void onRecord(void *user, const short *f, size_t n) {
    AudioMixerAsio *d = static_cast<AudioMixerAsio *>(user);
    EnterCriticalSection(&d->ring_lock_);
    for (size_t i = 0; i < n; i++) {
      if (d->ring_count_ == kRingFrames) { d->ring_tail_ = (d->ring_tail_ + 1) % kRingFrames; d->ring_count_--; }       // the host reads too slowly: drop the oldest
      memcpy(d->ring_ + d->ring_head_ * kChannels, f + i * kChannels, kChannels * sizeof(short));
      d->ring_head_ = (d->ring_head_ + 1) % kRingFrames; d->ring_count_++;
    }
    LeaveCriticalSection(&d->ring_lock_);
  }

  void tryConnect(ULONGLONG now_ms) {
    if (link_ && am_link_alive(link_)) return;
    if (now_ms - last_try_ms_ < 1000) return;
    last_try_ms_ = now_ms;
    if (link_) { am_link_close(link_); link_ = nullptr; }
    char err[160]; am_link_config c; memset(&c, 0, sizeof c);
    c.name = kDriverName; c.channels = (int)kChannels; c.rate = (int)rate_; c.on_record = &AudioMixerAsio::onRecord; c.user = this;
    EnterCriticalSection(&ring_lock_); ring_head_ = ring_tail_ = ring_count_ = 0; LeaveCriticalSection(&ring_lock_);
    link_ = am_link_open(&c, err, sizeof err);
  }

  void cycle() {
    const long n = buffer_size_, idx = buffer_index_;
    // the mixer -> the host's input buffers (silence while the ring is empty)
    short frame[kChannels];
    for (long i = 0; i < n; i++) {
      bool have = false;
      EnterCriticalSection(&ring_lock_);
      if (ring_count_) { memcpy(frame, ring_ + ring_tail_ * kChannels, sizeof frame); ring_tail_ = (ring_tail_ + 1) % kRingFrames; ring_count_--; have = true; }
      LeaveCriticalSection(&ring_lock_);
      if (!have) memset(frame, 0, sizeof frame);
      for (int c = 0; c < nchans_; c++) if (chans_[c].input) chans_[c].buf[idx][i] = (int)((unsigned)(unsigned short)frame[chans_[c].num] << 16);
    }
    callbacks_->bufferSwitch(idx, ASIOTrue);                  // the host reads the inputs and writes the outputs of buffer `idx`
    // the host's output buffers -> the mixer (interleaved 16-bit)
    if (link_ && am_link_alive(link_)) {
      short *out = (short *)calloc((size_t)n * kChannels, sizeof(short));
      if (out) {
        for (int c = 0; c < nchans_; c++) {
          if (chans_[c].input) continue;
          const int *src = chans_[c].buf[idx];
          for (long i = 0; i < n; i++) out[i * kChannels + chans_[c].num] = (short)(src[i] >> 16);
        }
        am_link_play(link_, out, (size_t)n);
        free(out);
      }
    }
    sample_pos_ += (unsigned long long)n;
    buffer_index_ ^= 1;
  }

  void run() {
    LARGE_INTEGER freq, next, now; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&next);
    HANDLE timer = CreateWaitableTimerW(nullptr, TRUE, nullptr);
    timeBeginPeriod(1);
    const LONGLONG period = (LONGLONG)((double)freq.QuadPart * (double)buffer_size_ / rate_);
    while (running_) {
      next.QuadPart += period;
      QueryPerformanceCounter(&now);
      LONGLONG wait = next.QuadPart - now.QuadPart;
      if (wait > 0 && timer) {
        LARGE_INTEGER due; due.QuadPart = -(LONGLONG)((double)wait * 10000000.0 / (double)freq.QuadPart);
        SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
        WaitForSingleObject(timer, 200);
      } else if (wait < -4 * period) next = now;                // the thread was stalled: do not fire a burst of buffers to catch up
      QueryPerformanceCounter(&now);
      stamp_ns_ = (unsigned long long)((double)now.QuadPart * 1e9 / (double)freq.QuadPart);
      tryConnect(GetTickCount64());
      cycle();
    }
    timeEndPeriod(1);
    if (timer) CloseHandle(timer);
  }

  static DWORD WINAPI threadMain(LPVOID arg) { static_cast<AudioMixerAsio *>(arg)->run(); return 0; }

  volatile LONG refs_;
  bool inited_ = false;
  volatile bool running_ = false, buffers_ready_ = false;
  double rate_ = 48000.0;
  long buffer_size_ = 0, buffer_index_ = 0;
  ASIOCallbacks *callbacks_ = nullptr;
  Chan chans_[2 * kChannels];
  int nchans_ = 0;
  bool in_active_[kChannels] = { false, false }, out_active_[kChannels] = { false, false };
  volatile unsigned long long sample_pos_ = 0, stamp_ns_ = 0;
  HANDLE thread_ = nullptr;
  am_link *link_ = nullptr;
  ULONGLONG last_try_ms_ = 0;
  CRITICAL_SECTION ring_lock_;
  short *ring_ = nullptr;
  size_t ring_head_ = 0, ring_tail_ = 0, ring_count_ = 0;
  char error_[128];
};

// ───────── COM plumbing ─────────
class Factory : public IClassFactory {
 public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
    if (riid == IID_IUnknown || riid == IID_IClassFactory) { *ppv = static_cast<IClassFactory *>(this); AddRef(); return S_OK; }
    *ppv = nullptr; return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return 2; }          // one static instance
  ULONG STDMETHODCALLTYPE Release() override { return 1; }
  HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown *outer, REFIID riid, void **ppv) override {
    if (outer) return CLASS_E_NOAGGREGATION;
    AudioMixerAsio *d = new (std::nothrow) AudioMixerAsio();
    if (!d) return E_OUTOFMEMORY;
    HRESULT hr = d->QueryInterface(riid, ppv);
    d->Release();
    return hr;
  }
  HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override { if (lock) InterlockedIncrement(&g_locks); else InterlockedDecrement(&g_locks); return S_OK; }
};
static Factory g_factory;

extern "C" BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) { g_module = module; DisableThreadLibraryCalls(module); }
  return TRUE;
}
extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv) {
  if (rclsid != CLSID_AudioMixerAsio) { *ppv = nullptr; return CLASS_E_CLASSNOTAVAILABLE; }
  return g_factory.QueryInterface(riid, ppv);
}
extern "C" HRESULT __stdcall DllCanUnloadNow() { return (g_locks == 0 && g_objects == 0) ? S_OK : S_FALSE; }

static bool setValue(HKEY root, const wchar_t *path, const wchar_t *name, const wchar_t *value) {
  HKEY k;
  if (RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
  LONG r = RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)value, (DWORD)((wcslen(value) + 1) * sizeof(wchar_t)));
  RegCloseKey(k);
  return r == ERROR_SUCCESS;
}

// regsvr32 AudioMixerASIO64.dll (as administrator): the class in HKLM\SOFTWARE\Classes\CLSID, the driver in HKLM\SOFTWARE\ASIO\Audio Mixer where ASIO hosts look
extern "C" HRESULT __stdcall DllRegisterServer() {
  wchar_t dll[MAX_PATH], key[160];
  if (!GetModuleFileNameW(g_module, dll, MAX_PATH)) return SELFREG_E_CLASS;
  swprintf(key, 160, L"SOFTWARE\\Classes\\CLSID\\%ls", kClsidText);
  bool ok = setValue(HKEY_LOCAL_MACHINE, key, nullptr, L"Audio Mixer ASIO");
  swprintf(key, 160, L"SOFTWARE\\Classes\\CLSID\\%ls\\InprocServer32", kClsidText);
  ok = ok && setValue(HKEY_LOCAL_MACHINE, key, nullptr, dll) && setValue(HKEY_LOCAL_MACHINE, key, L"ThreadingModel", L"Apartment");
  ok = ok && setValue(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO\\Audio Mixer", L"CLSID", kClsidText) && setValue(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO\\Audio Mixer", L"Description", L"Audio Mixer");
  return ok ? S_OK : SELFREG_E_CLASS;
}
extern "C" HRESULT __stdcall DllUnregisterServer() {
  wchar_t key[160];
  RegDeleteTreeW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO\\Audio Mixer");
  swprintf(key, 160, L"SOFTWARE\\Classes\\CLSID\\%ls", kClsidText);
  RegDeleteTreeW(HKEY_LOCAL_MACHINE, key);
  return S_OK;
}
