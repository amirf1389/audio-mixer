// "Audio Mixer ASIO": an ASIO driver (in-process COM server, a DLL) that appears in every ASIO host (DAWs, OBS, ...) as an audio interface called
// "Audio Mixer" and connects it to the running Audio Mixer program:
//
//   ASIO outputs  (what the host PLAYS)    ->  the mixer reads them as an input source (device "Audio Mixer" in LIVE SOURCES: READ)
//   ASIO inputs   (what the host RECORDS)  <-  what the mixer sends to the device (LIVE SOURCES: WRITE)
//
// 2 in / 2 out, 32-bit integer samples (ASIOSTInt32LSB), buffers of 64..2048 frames (power of two), 44.1 / 48 / 88.2 / 96 kHz.
// The driver has no hardware clock: a high-resolution timer thread paces the buffer switches at the real sample rate.
// The connection to the mixer (drivers/common/am_link.c) is kept by a separate thread that starts in start() and ends in stop(): the audio thread never connects,
// so a mixer that is not running (or stuck) cannot stall the buffer switches. Without the mixer the driver still runs, silent, and the thread tries again every second.
//
// Registration is a deliberate step and nothing the installer does: run  regsvr32 AudioMixerASIO64.dll  as administrator (see README.md).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <olectl.h>
#include <mmsystem.h>
#include <avrt.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asio_iface.h"
extern "C" {
#include "drivers/common/am_link.h"
}

// {C1893F2F-1AD5-4344-9806-DCFD242C0D48}: the class id AND (by ASIO convention) the interface id the hosts ask for
static const GUID CLSID_AudioMixerAsio = { 0xc1893f2f, 0x1ad5, 0x4344, { 0x98, 0x06, 0xdc, 0xfd, 0x24, 0x2c, 0x0d, 0x48 } };
static const char *const kDriverName = "Audio Mixer";
static const wchar_t *const kClsidText = L"{C1893F2F-1AD5-4344-9806-DCFD242C0D48}";
static const long kChannels = 2;
static const long kMinBuffer = 32, kMaxBuffer = 2048, kDefaultBuffer = 256;
static const size_t kRingFrames = 16384;

static HINSTANCE g_module = nullptr;
static volatile LONG g_locks = 0, g_objects = 0;

static bool supportedRate(double r) { return r == 44100.0 || r == 48000.0 || r == 88200.0 || r == 96000.0; }

class AudioMixerAsio final : public IASIO {
 public:
  AudioMixerAsio() : refs_(1) {
    InterlockedIncrement(&g_objects);
    InitializeCriticalSection(&ring_lock_); InitializeCriticalSection(&link_lock_);
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
  ASIOBool ASIO_CALL init(void *sysHandle) override { (void)sysHandle; inited_ = true; return ASIOTrue; }
  void ASIO_CALL getDriverName(char *name) override { strcpy(name, kDriverName); }
  long ASIO_CALL getDriverVersion() override { return 118; }
  void ASIO_CALL getErrorMessage(char *string) override { strncpy(string, error_, 123); string[123] = 0; }

  ASIOError ASIO_CALL start() override {
    if (!buffers_ready_ || !callbacks_) return ASE_NotPresent;
    if (running_) return ASE_OK;
    InterlockedExchange64((volatile LONG64 *)&sample_pos_, 0); buffer_index_ = 0;
    running_ = true;
    stop_evt_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    connector_ = stop_evt_ ? CreateThread(nullptr, 0, &AudioMixerAsio::connectorMain, this, 0, nullptr) : nullptr;   // without it the driver still runs, silent
    thread_ = CreateThread(nullptr, 0, &AudioMixerAsio::threadMain, this, 0, &thread_id_);
    if (!thread_) { running_ = false; stopConnector(); strcpy(error_, "cannot start the driver thread"); return ASE_HWMalfunction; }
    SetThreadPriority(thread_, THREAD_PRIORITY_TIME_CRITICAL);
    return ASE_OK;
  }
  ASIOError ASIO_CALL stop() override {
    if (!running_) return ASE_OK;
    running_ = false;
    if (thread_) {
      // a host may call stop() from inside its bufferSwitch callback, that is on the driver thread: it cannot wait for itself (it ends when the callback returns)
      if (GetCurrentThreadId() != thread_id_) WaitForSingleObject(thread_, INFINITE);
      CloseHandle(thread_); thread_ = nullptr;
    }
    stopConnector();
    return ASE_OK;
  }

  ASIOError ASIO_CALL getChannels(long *numInputChannels, long *numOutputChannels) override {
    if (!numInputChannels || !numOutputChannels) return ASE_InvalidParameter;
    *numInputChannels = kChannels; *numOutputChannels = kChannels;
    return ASE_OK;
  }
  ASIOError ASIO_CALL getLatencies(long *inputLatency, long *outputLatency) override {
    long b = buffer_size_ ? buffer_size_ : kDefaultBuffer;
    if (inputLatency) *inputLatency = b;
    if (outputLatency) *outputLatency = b;
    return ASE_OK;
  }
  ASIOError ASIO_CALL getBufferSize(long *minSize, long *maxSize, long *preferredSize, long *granularity) override {
    if (minSize) *minSize = kMinBuffer;
    if (maxSize) *maxSize = kMaxBuffer;
    if (preferredSize) *preferredSize = kDefaultBuffer;
    if (granularity) *granularity = -1;                       // powers of two between min and max
    return ASE_OK;
  }
  ASIOError ASIO_CALL canSampleRate(ASIOSampleRate sampleRate) override { return supportedRate(sampleRate) ? ASE_OK : ASE_NoClock; }
  ASIOError ASIO_CALL getSampleRate(ASIOSampleRate *sampleRate) override { if (!sampleRate) return ASE_InvalidParameter; *sampleRate = rate_; return ASE_OK; }
  ASIOError ASIO_CALL setSampleRate(ASIOSampleRate sampleRate) override {
    if (!supportedRate(sampleRate)) return ASE_NoClock;
    if (running_ && sampleRate != rate_) return ASE_InvalidMode;     // the host stops the driver before it changes the rate
    rate_ = sampleRate;
    if (callbacks_ && callbacks_->sampleRateDidChange) callbacks_->sampleRateDidChange(rate_);
    return ASE_OK;
  }
  ASIOError ASIO_CALL getClockSources(ASIOClockSource *clocks, long *numSources) override {
    if (!clocks || !numSources || *numSources < 1) return ASE_InvalidParameter;
    memset(clocks, 0, sizeof *clocks);
    clocks[0].index = 0; clocks[0].associatedChannel = -1; clocks[0].associatedGroup = -1; clocks[0].isCurrentSource = ASIOTrue;
    strcpy(clocks[0].name, "Internal");
    *numSources = 1;
    return ASE_OK;
  }
  ASIOError ASIO_CALL setClockSource(long reference) override { return reference == 0 ? ASE_OK : ASE_NotPresent; }
  ASIOError ASIO_CALL getSamplePosition(ASIOSamples *sPos, ASIOTimeStamp *tStamp) override {
    if (!sPos || !tStamp) return ASE_InvalidParameter;
    const unsigned long long s = (unsigned long long)InterlockedCompareExchange64((volatile LONG64 *)&sample_pos_, 0, 0), t = (unsigned long long)InterlockedCompareExchange64((volatile LONG64 *)&stamp_ns_, 0, 0);   // a plain 64-bit read tears on 32-bit builds
    sPos->hi = (unsigned long)(s >> 32); sPos->lo = (unsigned long)(s & 0xffffffffu);
    tStamp->hi = (unsigned long)(t >> 32); tStamp->lo = (unsigned long)(t & 0xffffffffu);
    return running_ ? ASE_OK : ASE_SPNotAdvancing;
  }
  ASIOError ASIO_CALL getChannelInfo(ASIOChannelInfo *info) override {
    if (!info || info->channel < 0 || info->channel >= kChannels) return ASE_InvalidParameter;
    info->type = ASIOSTInt32LSB; info->channelGroup = 0;
    info->isActive = (info->isInput ? in_active_ : out_active_)[info->channel] ? ASIOTrue : ASIOFalse;
    // an ASIO input is what the mixer sends to the device ("Mixer"); an ASIO output is what the host plays into the mixer ("To mixer")
    sprintf(info->name, info->isInput ? "Mixer %s" : "To mixer %s", info->channel == 0 ? "L" : "R");
    return ASE_OK;
  }

  ASIOError ASIO_CALL createBuffers(ASIOBufferInfo *infos, long numChannels, long bufferSize, ASIOCallbacks *callbacks) override {
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
    out_ = (short *)calloc((size_t)bufferSize * kChannels, sizeof(short));
    if (!out_) { disposeBuffers(); return ASE_NoMemory; }
    buffer_size_ = bufferSize; callbacks_ = callbacks; buffers_ready_ = true;
    return ASE_OK;
  }
  ASIOError ASIO_CALL disposeBuffers() override {
    if (running_) stop();
    for (int i = 0; i < nchans_; i++) { free(chans_[i].buf[0]); free(chans_[i].buf[1]); }
    free(out_); out_ = nullptr;
    nchans_ = 0; in_active_[0] = in_active_[1] = out_active_[0] = out_active_[1] = false;
    buffers_ready_ = false; callbacks_ = nullptr; buffer_size_ = 0;
    return ASE_OK;
  }
  ASIOError ASIO_CALL controlPanel() override {
    char text[320];
    EnterCriticalSection(&link_lock_); bool up = link_ && am_link_alive(link_); LeaveCriticalSection(&link_lock_);
    sprintf(text, "Audio Mixer ASIO driver\n\nMixer connection: %s\nSample rate: %.0f Hz   Buffer: %ld frames\n\n%s",
            up ? "connected" : (running_ ? "NOT connected (start Audio Mixer; the driver keeps trying)" : "not started yet"), rate_, buffer_size_ ? buffer_size_ : kDefaultBuffer,
            "In the mixer open LIVE SOURCES, find the device \"Audio Mixer\" and switch READ (host playback) and WRITE (host recording) on.");
    MessageBoxA(nullptr, text, "Audio Mixer ASIO", MB_OK | MB_ICONINFORMATION);
    return ASE_OK;
  }
  // ASIO SDK 2.3 capability probes (hosts call these right after init): PCM is the only I/O format; the optional features (time code, transport, monitoring,
  // gain / meters, overload reporting) are answered "not present", anything else is an invalid selector. Answering ASE_InvalidParameter to the can-do probes
  // made some hosts treat the driver as broken.
  ASIOError ASIO_CALL future(long selector, void *opt) override {
    switch (selector) {
      case kAsioGetIoFormat: if (!opt) return ASE_InvalidParameter; ((ASIOIoFormat *)opt)->FormatType = kASIOPCMFormat; return ASE_SUCCESS;
      case kAsioSetIoFormat: case kAsioCanDoIoFormat: if (!opt) return ASE_InvalidParameter; return ((ASIOIoFormat *)opt)->FormatType == kASIOPCMFormat ? ASE_SUCCESS : ASE_NotPresent;
      case kAsioCanInputMonitor: case kAsioCanTimeInfo: case kAsioCanTimeCode: case kAsioCanTransport: case kAsioCanInputGain: case kAsioCanInputMeter:
      case kAsioCanOutputGain: case kAsioCanOutputMeter: case kAsioCanReportOverload: return ASE_NotPresent;
      default: return ASE_InvalidParameter;
    }
  }
  ASIOError ASIO_CALL outputReady() override { return ASE_NotPresent; }

 private:
  struct Chan { bool input; long num; int *buf[2]; };

  ~AudioMixerAsio() {
    stop(); disposeBuffers();
    DeleteCriticalSection(&ring_lock_); DeleteCriticalSection(&link_lock_);
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

  // The connector thread: opens the link when it is down (every second), swaps it in under link_lock_ and closes dead ones. All the slow work (connecting,
  // the handshake, joining the link's reader thread) happens here, never on the audio thread.
  void connectLoop() {
    while (running_ && WaitForSingleObject(stop_evt_, 0) == WAIT_TIMEOUT) {
      EnterCriticalSection(&link_lock_);
      bool up = link_ && am_link_alive(link_);
      am_link *dead = nullptr;
      if (!up) { dead = link_; link_ = nullptr; }
      LeaveCriticalSection(&link_lock_);
      if (dead) am_link_close(dead);
      if (!up) {
        char err[160]; am_link_config cfg; memset(&cfg, 0, sizeof cfg);
        cfg.name = kDriverName; cfg.channels = (int)kChannels; cfg.rate = (int)rate_; cfg.on_record = &AudioMixerAsio::onRecord; cfg.user = this;
        EnterCriticalSection(&ring_lock_); ring_head_ = ring_tail_ = ring_count_ = 0; LeaveCriticalSection(&ring_lock_);
        am_link *fresh = am_link_open(&cfg, err, sizeof err);
        if (fresh) {
          EnterCriticalSection(&link_lock_);
          if (running_) link_ = fresh; else { LeaveCriticalSection(&link_lock_); am_link_close(fresh); break; }
          LeaveCriticalSection(&link_lock_);
        }
      }
      if (WaitForSingleObject(stop_evt_, 1000) != WAIT_TIMEOUT) break;
    }
  }
  static DWORD WINAPI connectorMain(LPVOID arg) { static_cast<AudioMixerAsio *>(arg)->connectLoop(); return 0; }

  void stopConnector() {
    if (stop_evt_) SetEvent(stop_evt_);
    if (connector_) { WaitForSingleObject(connector_, INFINITE); CloseHandle(connector_); connector_ = nullptr; }   // bounded: the link's connect and handshake have time limits
    if (stop_evt_) { CloseHandle(stop_evt_); stop_evt_ = nullptr; }
    EnterCriticalSection(&link_lock_);
    am_link *old = link_; link_ = nullptr;
    LeaveCriticalSection(&link_lock_);
    if (old) am_link_close(old);
  }

  void cycle() {
    const long n = buffer_size_, idx = buffer_index_;
    // the mixer -> the host's input buffers (silence while the ring is empty)
    EnterCriticalSection(&ring_lock_);                        // one lock for the whole buffer (it was one per frame: 48000 lock rounds a second against the link thread)
    for (long i = 0; i < n; i++) {
      short frame[kChannels] = { 0, 0 };
      if (ring_count_) { memcpy(frame, ring_ + ring_tail_ * kChannels, sizeof frame); ring_tail_ = (ring_tail_ + 1) % kRingFrames; ring_count_--; }
      for (int c = 0; c < nchans_; c++) if (chans_[c].input) chans_[c].buf[idx][i] = (int)((unsigned)(unsigned short)frame[chans_[c].num] << 16);
    }
    LeaveCriticalSection(&ring_lock_);
    callbacks_->bufferSwitch(idx, ASIOTrue);                  // the host reads the inputs and writes the outputs of buffer `idx`
    if (!running_) return;                                    // the host stopped the driver (and may have freed the buffers) inside its callback
    // the host's output buffers -> the mixer (interleaved 16-bit; the buffer was allocated by createBuffers, nothing is allocated here)
    EnterCriticalSection(&link_lock_);
    if (link_ && am_link_alive(link_)) {
      memset(out_, 0, (size_t)n * kChannels * sizeof(short));
      for (int c = 0; c < nchans_; c++) {
        if (chans_[c].input) continue;
        const int *src = chans_[c].buf[idx];
        for (long i = 0; i < n; i++) out_[i * kChannels + chans_[c].num] = (short)(src[i] >> 16);
      }
      am_link_play(link_, out_, (size_t)n);                   // sending has a time limit; a link that fails is dead and the connector replaces it
    }
    LeaveCriticalSection(&link_lock_);
    InterlockedExchangeAdd64((volatile LONG64 *)&sample_pos_, (LONG64)n);
    buffer_index_ ^= 1;
  }

  void run() {
    LARGE_INTEGER freq, next, now; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&next);
    // a high-resolution timer (Windows 10 1803+) wakes within ~0.5 ms; the classic one rounds to the 1 ms tick and glitches at 32..64 frame buffers
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer) timer = CreateWaitableTimerW(nullptr, TRUE, nullptr);
    timeBeginPeriod(1);
    DWORD task = 0; HANDLE mm = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task);   // the scheduler keeps the audio thread running (like a real driver's interrupt)
    const LONGLONG period = (LONGLONG)((double)freq.QuadPart * (double)buffer_size_ / rate_);
    while (running_) {
      next.QuadPart += period;
      QueryPerformanceCounter(&now);
      LONGLONG wait = next.QuadPart - now.QuadPart;
      if (wait > 0) {
        const DWORD ms = (DWORD)((double)wait * 1000.0 / (double)freq.QuadPart);
        if (timer) {
          LARGE_INTEGER due; due.QuadPart = -(LONGLONG)((double)wait * 10000000.0 / (double)freq.QuadPart);
          if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(timer, 200); else Sleep(ms);
        } else Sleep(ms);                                        // without a timer: sleep, never spin a core at 100 %
      } else if (wait < -4 * period) next = now;                // the thread was stalled: do not fire a burst of buffers to catch up
      QueryPerformanceCounter(&now);
      InterlockedExchange64((volatile LONG64 *)&stamp_ns_, (LONG64)((double)now.QuadPart * 1e9 / (double)freq.QuadPart));
      cycle();
    }
    if (mm) AvRevertMmThreadCharacteristics(mm);
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
  alignas(8) volatile unsigned long long sample_pos_ = 0;   // 64-bit, aligned: accessed with the Interlocked*64 functions only
  alignas(8) volatile unsigned long long stamp_ns_ = 0;
  HANDLE thread_ = nullptr, connector_ = nullptr, stop_evt_ = nullptr;
  DWORD thread_id_ = 0;
  am_link *link_ = nullptr;          // guarded by link_lock_
  short *out_ = nullptr;             // interleaved output of one buffer, allocated with the buffers
  CRITICAL_SECTION ring_lock_, link_lock_;
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
  ok = ok && setValue(HKEY_LOCAL_MACHINE, key, nullptr, dll) && setValue(HKEY_LOCAL_MACHINE, key, L"ThreadingModel", L"Both");
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
