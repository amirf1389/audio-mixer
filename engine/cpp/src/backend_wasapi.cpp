// WASAPI backend (Windows, event driven): render with an event, capture polled once per block. Shared mode (default): float32 at the requested rate and channel count, the Windows audio
// engine converts (AUTOCONVERTPCM). Exclusive mode (DeviceConfig::exclusive, "take control"): the device is opened AUDCLNT_SHAREMODE_EXCLUSIVE in the first format it accepts (float32, int32, int24, int16) at the
// exact rate and channel count, with the device-aligned period; no other program can use it meanwhile. The default render / capture endpoints are used; a device is chosen by its endpoint id string ("" = default).
// Compiled with mingw-w64 in the tests (scripts/build-engine.js --io); it has not been run on a Windows PC.
#ifdef _WIN32
#include "audiomixer/sample_format.hpp"
#include <vector>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <propsys.h>
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <avrt.h>
#include <thread>

#include "audiomixer/sysopt.hpp"
#include "audiomixer/app_sound.hpp"
#include "audiomixer/device_watch.hpp"
#include "audiomixer/endpoint_type.hpp"
#include "backends.hpp"

namespace am {
namespace {
const GUID kClsidEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
const GUID kIidEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
const GUID kIidAudioClient = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
const GUID kIidRenderClient = {0xF294ACFC, 0x3146, 0x4483, {0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2}};
const GUID kIidCaptureClient = {0xC8ADBD64, 0xE71E, 0x48A0, {0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17}};
const GUID kPcmSubFormat = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
const GUID kFloatSubFormat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

// per-channel FIFO with its storage allocated once: nothing is allocated on the audio thread (the former per-sample deque allocated on push)
struct Fifo {
  std::vector<float> d; std::size_t head = 0, n = 0;
  void init(std::size_t cap) { d.assign(std::max<std::size_t>(cap, 16), 0.0f); head = 0; n = 0; }
  void push(float v) { if (n == d.size()) { head = (head + 1) % d.size(); --n; } d[(head + n) % d.size()] = v; ++n; }      // full: the oldest sample goes
  float pop() { const float v = d[head]; head = (head + 1) % d.size(); --n; return v; }
  std::size_t size() const { return n; }
  void trim(std::size_t keep) { while (n > keep) { head = (head + 1) % d.size(); --n; } }
};

// one sample format of a stream: what the device takes in exclusive mode (shared mode is always float32)
struct Sample { bool isFloat = true; WORD bits = 32, valid = 32; std::size_t bytes() const { return bits / 8u; } };
float loadSample(const BYTE* p, const Sample& s) {   // am::fmt: one scale both ways, rounding, clamping (sample_format.hpp)
  if (s.isFloat) { float v; std::memcpy(&v, p, 4); return v; }
  if (s.bits == 32) { std::int32_t v; std::memcpy(&v, p, 4); return am::fmt::fromI32(v); }
  if (s.bits == 24) return am::fmt::fromI24(am::fmt::unpack24(p));
  std::int16_t v; std::memcpy(&v, p, 2); return am::fmt::fromI16(v);
}
void storeSample(BYTE* p, float x, const Sample& s) {
  if (s.isFloat) { std::memcpy(p, &x, 4); return; }
  if (s.bits == 32) { const std::int32_t v = am::fmt::toI32(x); std::memcpy(p, &v, 4); }
  else if (s.bits == 24) am::fmt::pack24(p, am::fmt::toI24(x));
  else { const std::int16_t v = am::fmt::toI16(x); std::memcpy(p, &v, 2); }
}

template <class T> void release(T*& p) { if (p) { p->Release(); p = nullptr; } }
std::string hr(const char* what, HRESULT r) { char b[96]; std::snprintf(b, sizeof b, "%s failed (0x%08lx)", what, static_cast<unsigned long>(r)); return b; }

// USB AUDIO (endpoint_type.hpp): the bus of an endpoint and its container (one physical device), read from its property store
const PROPERTYKEY kPkEnumerator = {{0xA45C254E, 0xDF1C, 0x4EFD, {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}}, 24};
const PROPERTYKEY kPkFriendly = {{0xA45C254E, 0xDF1C, 0x4EFD, {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}}, 14};
const PROPERTYKEY kPkInterface = {{0x026E516E, 0xB814, 0x414B, {0x83, 0xCD, 0x85, 0x6D, 0x6F, 0xEF, 0x48, 0x22}}, 2};
const PROPERTYKEY kPkContainer = {{0x8C7ED206, 0x3F8A, 0x4827, {0xB3, 0xAB, 0xAE, 0x9E, 0x1F, 0xAE, 0xFC, 0x6C}}, 2};
std::string narrow(const wchar_t* w) {
  if (!w) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  if (n <= 1) return {};
  std::string o(static_cast<std::size_t>(n - 1), '\0'); WideCharToMultiByte(CP_UTF8, 0, w, -1, &o[0], n, nullptr, nullptr); return o;
}
struct EpFacts { am::ep::Class cls; GUID container{}; bool hasContainer = false; };
EpFacts endpointFacts(IMMDevice* d, bool capture) {
  EpFacts f; if (!d) return f; IPropertyStore* ps = nullptr; if (FAILED(d->OpenPropertyStore(STGM_READ, &ps)) || !ps) return f;
  am::ep::Props p; p.capture = capture; PROPVARIANT v;
  PropVariantInit(&v); if (SUCCEEDED(ps->GetValue(kPkEnumerator, &v)) && v.vt == VT_LPWSTR) p.enumerator = narrow(v.pwszVal); PropVariantClear(&v);
  PropVariantInit(&v); if (SUCCEEDED(ps->GetValue(kPkFriendly, &v)) && v.vt == VT_LPWSTR) p.name = narrow(v.pwszVal); PropVariantClear(&v);
  PropVariantInit(&v); if (SUCCEEDED(ps->GetValue(kPkInterface, &v)) && v.vt == VT_LPWSTR) p.interfaceName = narrow(v.pwszVal); PropVariantClear(&v);
  PropVariantInit(&v); if (SUCCEEDED(ps->GetValue(kPkContainer, &v)) && v.vt == VT_CLSID && v.puuid) { f.container = *v.puuid; f.hasContainer = true; } PropVariantClear(&v);
  ps->Release(); f.cls = am::ep::classifyEndpoint(p); return f;
}
std::string endpointIdOf(IMMDevice* d) { LPWSTR w = nullptr; std::string id; if (d && SUCCEEDED(d->GetId(&w))) { id = narrow(w); CoTaskMemFree(w); } return id; }
// the capture endpoint of the same USB device as the render endpoint `outId` ("" when it has none): a USB headset, mixer or interface in + out together
std::string usbCaptureSibling(IMMDeviceEnumerator* en, const std::string& outId) {
  if (outId.empty() || outId == "-") return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, outId.c_str(), -1, nullptr, 0); if (n <= 0) return {};
  std::vector<wchar_t> w(static_cast<std::size_t>(n), L'\0'); MultiByteToWideChar(CP_UTF8, 0, outId.c_str(), -1, w.data(), n);
  IMMDevice* od = nullptr; if (FAILED(en->GetDevice(w.data(), &od)) || !od) return {};
  const EpFacts of = endpointFacts(od, false); od->Release();
  if (!of.cls.usb || !of.hasContainer) return {};
  IMMDeviceCollection* col = nullptr; std::string found;
  if (FAILED(en->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &col)) || !col) return {};
  UINT c = 0; col->GetCount(&c);
  for (UINT i = 0; i < c && found.empty(); ++i) { IMMDevice* d = nullptr; if (FAILED(col->Item(i, &d)) || !d) continue;
    const EpFacts f = endpointFacts(d, true); if (f.cls.usb && f.hasContainer && IsEqualGUID(f.container, of.container)) found = endpointIdOf(d); d->Release(); }
  col->Release(); return found;
}

// PROCESS LOOPBACK ("loopback:system"): everything Windows plays except this program's own process tree. The parameter block of
// audioclientactivationparams.h (Windows SDK 10.0.20348; not in mingw-w64), declared here exactly as documented.
enum AmActivationType { kActivationDefault = 0, kActivationProcessLoopback = 1 };
enum AmLoopbackMode { kIncludeTree = 0, kExcludeTree = 1 };
struct AmLoopbackParams { DWORD TargetProcessId; AmLoopbackMode ProcessLoopbackMode; };
struct AmActivationParams { AmActivationType ActivationType; AmLoopbackParams ProcessLoopbackParams; };
const GUID kIidUnknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const GUID kIidAgile = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};
const GUID kIidActivateHandler = {0x41D949AB, 0x9862, 0x444A, {0x80, 0xF6, 0xC2, 0x61, 0x33, 0x4D, 0xA5, 0xEB}};
// the completion handler: agile (called on any MTA thread), signals an event; the result is read from the operation afterwards
class ActivateDone final : public IActivateAudioInterfaceCompletionHandler {
 public:
  HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  ~ActivateDone() { if (done) CloseHandle(done); }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** p) override {
    if (IsEqualGUID(id, kIidUnknown) || IsEqualGUID(id, kIidAgile) || IsEqualGUID(id, kIidActivateHandler)) { *p = static_cast<IActivateAudioInterfaceCompletionHandler*>(this); AddRef(); return S_OK; }
    *p = nullptr; return E_NOINTERFACE; }
  ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(++refs_); }
  ULONG STDMETHODCALLTYPE Release() override { const long n = --refs_; if (n == 0) delete this; return static_cast<ULONG>(n); }
  HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation*) override { SetEvent(done); return S_OK; }
 private:
  std::atomic<long> refs_{1};
};
// an IAudioClient on the process-loopback virtual device: this process tree excluded (COMPUTER SOUND), or only `pid`'s tree included (APP SOUND);
// loaded at run time (older Windows: a clear error)
HRESULT activateProcessLoopback(IAudioClient** ac, std::string& err, DWORD pid = 0) {
  using ActivateFn = HRESULT(WINAPI*)(LPCWSTR, REFIID, PROPVARIANT*, IActivateAudioInterfaceCompletionHandler*, IActivateAudioInterfaceAsyncOperation**);
  static const HMODULE mm = LoadLibraryExW(L"mmdevapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);   /* Windows' own copy only (never one beside the program), loaded once */ const auto fn = mm ? reinterpret_cast<ActivateFn>(reinterpret_cast<void*>(GetProcAddress(mm, "ActivateAudioInterfaceAsync"))) : nullptr;
  if (!fn) { err = "computer sound capture needs Windows 10 version 2004 or later"; return E_NOTIMPL; }
  AmActivationParams prm{}; prm.ActivationType = kActivationProcessLoopback; prm.ProcessLoopbackParams.TargetProcessId = pid ? pid : GetCurrentProcessId(); prm.ProcessLoopbackParams.ProcessLoopbackMode = pid ? kIncludeTree : kExcludeTree;
  PROPVARIANT pv; PropVariantInit(&pv); pv.vt = VT_BLOB; pv.blob.cbSize = sizeof prm; pv.blob.pBlobData = reinterpret_cast<BYTE*>(&prm);
  auto* h = new ActivateDone; IActivateAudioInterfaceAsyncOperation* op = nullptr;
  HRESULT r = fn(L"VAD\\Process_Loopback", kIidAudioClient, &pv, h, &op);
  if (SUCCEEDED(r) && WaitForSingleObject(h->done, 5000) != WAIT_OBJECT_0) r = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
  HRESULT ar = E_FAIL; IUnknown* unk = nullptr;
  if (SUCCEEDED(r)) { r = op->GetActivateResult(&ar, &unk); if (SUCCEEDED(r)) r = ar; }
  if (SUCCEEDED(r) && unk) r = unk->QueryInterface(kIidAudioClient, reinterpret_cast<void**>(ac));
  if (unk) unk->Release();
  if (op) op->Release();
  h->Release();
  if (FAILED(r)) err = hr(pid ? "app sound (process loopback)" : "computer sound (process loopback)", r) + (pid && r == E_INVALIDARG ? ": that program has closed" : "");
  return r;
}

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
    note_.clear(); exOut_ = exIn_ = false;
    // USB DUPLEX: the output is a USB device with its own microphone / inputs and no input was chosen: both sides of that one device
    // open together (one clock, one format), instead of Windows' default microphone beside the USB output
    if (wantOut && wantIn && cfg_.inDevice.empty()) { const std::string sib = usbCaptureSibling(en, cfg_.outDevice); if (!sib.empty()) { cfg_.inDevice = sib; note_ = "USB duplex: in + out on one device"; } }
    loopback_ = wantIn && cfg.inDevice.rfind("loopback:", 0) == 0;              // the input is what Windows plays on a render endpoint (app audio: a player, a browser, a streaming app)
    captureEvent_ = wantIn && !wantOut && !loopback_;                          // an input-only stream is clocked by the capture event; a loopback stream signals none, it is polled
    if (wantOut) ok = client(en, eRender, cfg.outChannels, &out_, &render_, nullptr, &sOut_, err);
    if (ok && wantIn) ok = client(en, eCapture, cfg.inChannels, &in_, nullptr, &capture_, &sIn_, err);
    en->Release();
    if (!ok) { closeAll(); return false; }
    ev_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (out_) out_->SetEventHandle(ev_); else if (in_ && captureEvent_) in_->SetEventHandle(ev_);
    const std::size_t cap = static_cast<std::size_t>(cfg.frames) * 16;
    inBuf_.assign(static_cast<std::size_t>(std::max(1, cfg.inChannels)), std::vector<float>(cap, 0.0f)); outBuf_.assign(static_cast<std::size_t>(std::max(1, cfg.outChannels)), std::vector<float>(cap, 0.0f));
    for (auto& v : inBuf_) inp_.push_back(v.data());
    for (auto& v : outBuf_) outp_.push_back(v.data());
    fifo_.assign(static_cast<std::size_t>(std::max(1, cfg.inChannels)), Fifo()); for (auto& q : fifo_) q.init(static_cast<std::size_t>(cfg.frames) * 32);
    return true;
  }
  bool start(std::string& err) override {
    if (out_ && FAILED(out_->Start())) { err = "cannot start the render stream"; return false; }
    if (in_ && FAILED(in_->Start())) { err = "cannot start the capture stream"; return false; }
    stop_ = false; lost_ = false; thread_ = std::thread([this] { loop(); }); return true;
  }
  void stop() override { stop_ = true; if (ev_) SetEvent(ev_); if (thread_.joinable()) thread_.join(); if (out_) out_->Stop(); if (in_) in_->Stop(); }
  std::uint64_t xruns() const override { return xruns_; }
  bool deviceLost() const override { return lost_.load(); }
  // LOW LATENCY: Windows' stream latency plus the stream's buffer, each direction (GetStreamLatency leaves the buffer out)
  void latencyFrames(long& in, long& out) const override {
    in = out = 0; const double rate = cfg_.sampleRate > 0 ? cfg_.sampleRate : 48000;
    auto of = [&](IAudioClient* c) -> long { if (!c) return 0; REFERENCE_TIME lat = 0; UINT32 buf = 0; c->GetStreamLatency(&lat); c->GetBufferSize(&buf); return static_cast<long>(lat * rate / 1e7) + static_cast<long>(buf); };
    out = of(out_); in = of(in_); }
 private:
  static WAVEFORMATEXTENSIBLE makeFormat(int ch, double rate, const Sample& s) {
    WAVEFORMATEXTENSIBLE f; std::memset(&f, 0, sizeof f);
    f.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE; f.Format.nChannels = static_cast<WORD>(ch); f.Format.nSamplesPerSec = static_cast<DWORD>(rate); f.Format.wBitsPerSample = s.bits;
    f.Format.nBlockAlign = static_cast<WORD>(ch * s.bytes()); f.Format.nAvgBytesPerSec = f.Format.nSamplesPerSec * f.Format.nBlockAlign; f.Format.cbSize = 22; f.Samples.wValidBitsPerSample = s.valid;
    f.dwChannelMask = ch == 1 ? SPEAKER_FRONT_CENTER : ch == 2 ? (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT) : 0; f.SubFormat = s.isFloat ? kFloatSubFormat : kPcmSubFormat;
    return f;
  }
  static std::string name(const Sample& f) { return std::to_string(f.valid ? f.valid : f.bits) + (f.isFloat ? "-bit float" : "-bit integer"); }
  double inputSilentSeconds() const override { return capture_ && !loopback_ ? static_cast<double>(silentFrames_.load()) / (cfg_.sampleRate > 0 ? cfg_.sampleRate : 48000.0) : 0.0; }
  std::string sampleFormat() const override {
    const std::string mode = (exOut_ || exIn_ ? " (WASAPI exclusive)" : " (WASAPI shared: Windows converts)") + (note_.empty() ? std::string() : " · " + note_);
    const std::string in = in_ ? name(sIn_) : "", out = out_ ? name(sOut_) : "";
    if (!in.empty() && in == out) return in + mode;
    return (in.empty() ? "" : "in " + in) + (in.empty() || out.empty() ? "" : " · ") + (out.empty() ? "" : "out " + out) + mode;
  }
  bool client(IMMDeviceEnumerator* en, EDataFlow flow, int ch, IAudioClient** ac, IAudioRenderClient** rc, IAudioCaptureClient** cc, Sample* used, std::string& err) {
    // the endpoint: the one named by its id string (from --list-devices), else the default one of this direction
    const bool loop = flow == eCapture && loopback_; std::string want = flow == eRender ? cfg_.outDevice : cfg_.inDevice; if (loop) want = want.substr(9);
    std::uint32_t appPid = 0; const bool app = loop && am::app::parseDeviceId(cfg_.inDevice, appPid);
    if (loop && (want == "system" || app)) {   // COMPUTER SOUND: every program but this one, so the mix can play on the same device without a loop; APP SOUND: one program
      if (FAILED(activateProcessLoopback(ac, err, app ? static_cast<DWORD>(appPid) : 0))) return false;
      WAVEFORMATEXTENSIBLE f = makeFormat(ch, cfg_.sampleRate, Sample()); const REFERENCE_TIME dur = static_cast<REFERENCE_TIME>(cfg_.frames * 3.0 / cfg_.sampleRate * 1e7);
      HRESULT r = (*ac)->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY, dur, 0, &f.Format, nullptr);
      if (FAILED(r)) { err = hr("Initialize (computer sound)", r); return false; }
      r = (*ac)->GetService(kIidCaptureClient, reinterpret_cast<void**>(cc)); if (FAILED(r)) { err = hr("GetService (computer sound)", r); return false; }
      return true; }
    if (loop) flow = eRender;   // a loopback capture opens the RENDER endpoint and reads what is mixed into it
    IMMDevice* dev = nullptr; HRESULT r;
    if (!want.empty() && want != "-") {
      const int n = MultiByteToWideChar(CP_UTF8, 0, want.c_str(), -1, nullptr, 0);
      std::vector<wchar_t> w(static_cast<std::size_t>(n > 0 ? n : 1), L'\0');
      if (n > 0) MultiByteToWideChar(CP_UTF8, 0, want.c_str(), -1, w.data(), n);
      r = en->GetDevice(w.data(), &dev);
    } else {
      r = en->GetDefaultAudioEndpoint(flow, eConsole, &dev);
    }
    if (FAILED(r)) { err = hr(want.empty() || want == "-" ? "default endpoint" : "endpoint", r); return false; }
    r = dev->Activate(kIidAudioClient, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(ac));
    if (FAILED(r)) { dev->Release(); err = hr("audio client", r); return false; }
    const DWORD evt = ((flow == eRender && !loop) || captureEvent_) ? AUDCLNT_STREAMFLAGS_EVENTCALLBACK : 0;
    if (loop) {   // always shared: the system mix of that endpoint, converted to our rate and channel count
      dev->Release(); WAVEFORMATEXTENSIBLE f = makeFormat(ch, cfg_.sampleRate, Sample()); const REFERENCE_TIME dur = static_cast<REFERENCE_TIME>(cfg_.frames * 3.0 / cfg_.sampleRate * 1e7);
      r = (*ac)->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY, dur, 0, &f.Format, nullptr);
      if (FAILED(r)) { err = hr("Initialize (loopback)", r); return false; }
    } else if (!cfg_.exclusive) {
      dev->Release();
      WAVEFORMATEXTENSIBLE f = makeFormat(ch, cfg_.sampleRate, Sample());
      const REFERENCE_TIME dur = static_cast<REFERENCE_TIME>(cfg_.frames * 3.0 / cfg_.sampleRate * 1e7);
      r = (*ac)->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY | evt, dur, 0, &f.Format, nullptr);
      if (FAILED(r)) { err = hr("Initialize", r); return false; }
    } else {
      // take control: the first format the device accepts exclusively at exactly the requested rate and channel count, asked in the order of
      // am::ep::exclusiveCandidates (the user's word length, the device's own mix format, then the ladder). A USB device that refuses
      // (exclusive switched off, held by another program, no format at this rate, another block size) goes on in SHARED mode, said in the status line.
      const EpFacts facts = endpointFacts(dev, flow == eCapture); const bool usbDev = facts.cls.usb;
      am::ep::Fmt native{false, 0, 0};
      { WAVEFORMATEX* mf = nullptr; if (SUCCEEDED((*ac)->GetMixFormat(&mf)) && mf) { const bool ext = mf->wFormatTag == WAVE_FORMAT_EXTENSIBLE && mf->cbSize >= 22;
          const WAVEFORMATEXTENSIBLE* xf = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(mf);
          native.isFloat = mf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT || (ext && IsEqualGUID(xf->SubFormat, kFloatSubFormat)); native.bits = mf->wBitsPerSample; native.valid = ext ? xf->Samples.wValidBitsPerSample : mf->wBitsPerSample; if (native.valid == 0) native.valid = native.bits;
          if (native.bits != 16 && native.bits != 24 && native.bits != 32) native.valid = 0;
          CoTaskMemFree(mf); } }
      const std::vector<am::ep::Fmt> kTry = am::ep::exclusiveCandidates(cfg_.bits, native, cfg_.preferInt32);
      WAVEFORMATEXTENSIBLE f; bool found = false;
      const am::ep::Fmt got = am::ep::negotiate(kTry, [&](const am::ep::Fmt& t) { f = makeFormat(ch, cfg_.sampleRate, Sample{t.isFloat, static_cast<WORD>(t.bits), static_cast<WORD>(t.valid)}); return (*ac)->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &f.Format, nullptr) == S_OK; }, found);
      std::string why;
      if (found) {
        *used = Sample{got.isFloat, static_cast<WORD>(got.bits), static_cast<WORD>(got.valid)}; f = makeFormat(ch, cfg_.sampleRate, *used);
        REFERENCE_TIME defP = 0, minP = 0; (*ac)->GetDevicePeriod(&defP, &minP);
        REFERENCE_TIME dur = static_cast<REFERENCE_TIME>(cfg_.frames / cfg_.sampleRate * 1e7); if (dur < minP) dur = minP;
        r = (*ac)->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, evt, dur, dur, &f.Format, nullptr);
        if (r == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {                              // the device wants whole hardware buffers: ask again with the size it reports
          UINT32 n = 0; (*ac)->GetBufferSize(&n); (*ac)->Release(); *ac = nullptr;
          if (SUCCEEDED(dev->Activate(kIidAudioClient, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(ac)))) {
            dur = static_cast<REFERENCE_TIME>(1e7 * static_cast<double>(n) / cfg_.sampleRate + 0.5);
            r = (*ac)->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, evt, dur, dur, &f.Format, nullptr);
          } else r = E_FAIL;
        }
        if (FAILED(r)) why = hr("Initialize (exclusive)", r) + (r == AUDCLNT_E_DEVICE_IN_USE ? ": another program holds the device" : r == AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED ? ": exclusive mode is switched off for this device in the Windows sound settings" : "");
        // an exclusive, event-driven stream is handed over one whole device buffer at a time: when the device grants another size than the
        // block asked for, the loop below could never fill it (silence, or every write refused). Say so instead of opening a dead stream.
        else { UINT32 gotN = 0; if (SUCCEEDED((*ac)->GetBufferSize(&gotN)) && gotN != cfg_.frames) why = "exclusive mode: this device works in blocks of " + std::to_string(gotN) + " frames at this rate, not " + std::to_string(cfg_.frames) + ". Set the buffer to " + std::to_string(gotN) + " frames, or use shared mode."; }
      } else why = "exclusive mode: the device accepts none of float32 / int32 / int24 / int16 at " + std::to_string(static_cast<long>(cfg_.sampleRate)) + " Hz with " + std::to_string(ch) + " channels (is \"Allow applications to take exclusive control of this device\" on, and no other program holding it?)";
      if (!why.empty() && usbDev) {   // USB: on in shared mode, cleanly (a fresh client; the refused one is let go)
        if (*ac) { (*ac)->Release(); *ac = nullptr; }
        r = dev->Activate(kIidAudioClient, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(ac));
        if (SUCCEEDED(r)) { *used = Sample(); WAVEFORMATEXTENSIBLE sf = makeFormat(ch, cfg_.sampleRate, Sample()); const REFERENCE_TIME sd = static_cast<REFERENCE_TIME>(cfg_.frames * 3.0 / cfg_.sampleRate * 1e7);
          r = (*ac)->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY | evt, sd, 0, &sf.Format, nullptr); }
        dev->Release();
        if (FAILED(r)) { err = why + "; shared mode too: " + hr("Initialize", r); return false; }
        const std::string n = (flow == eRender ? "out: " : "in: ") + am::ep::sharedFallbackNote(why); note_ = note_.empty() ? n : note_ + " · " + n;
      } else {
        dev->Release();
        if (!why.empty()) { err = why; return false; }
        (flow == eRender ? exOut_ : exIn_) = true;
        if (usbDev) { const std::string n = std::string("USB AUDIO ") + (flow == eRender ? "out" : "in") + " exclusive"; note_ = note_.empty() ? n : note_ + " · " + n; }
      }
    }
    r = flow == eRender && !loop ? (*ac)->GetService(kIidRenderClient, reinterpret_cast<void**>(rc)) : (*ac)->GetService(kIidCaptureClient, reinterpret_cast<void**>(cc));   // a loopback reads the render endpoint through a CAPTURE client (asking it for a render client into a null pointer was the "GetService failed (0x80004003)" of APP AUDIO)
    if (FAILED(r)) { err = hr("GetService", r); return false; }
    return true;
  }
  void loop() {
    const int ci = cfg_.inChannels, co = cfg_.outChannels; UINT32 bufFrames = 0; if (out_) out_->GetBufferSize(&bufFrames);
    DWORD task = 0; HANDLE mm = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task);          // the scheduler keeps the audio thread running
    am::sysopt::tuneAudioThread();                                                         // never slowed for power; the fast cores on a hybrid processor
    while (!stop_) {
      if (out_ || captureEvent_) WaitForSingleObject(ev_, 100); else Sleep(static_cast<DWORD>(1000.0 * cfg_.frames / cfg_.sampleRate));
      if (stop_) break;
      if (capture_) {                                                          // everything the capture side has: into per-channel FIFOs
        UINT32 n = 0;
        HRESULT pr = S_OK;
        while (SUCCEEDED(pr = capture_->GetNextPacketSize(&n)) && n > 0) {
          BYTE* data; UINT32 got; DWORD fl;
          if (FAILED(pr = capture_->GetBuffer(&data, &got, &fl, nullptr, nullptr))) break;
          const std::size_t sb = sIn_.bytes();
          { bool zero = (fl & AUDCLNT_BUFFERFLAGS_SILENT) != 0;   // exact silence from the system (flagged, or every byte zero): counted, see inputSilentSeconds
            if (!zero) { zero = true; const std::size_t nb = static_cast<std::size_t>(got) * static_cast<std::size_t>(ci) * sb; for (std::size_t b = 0; b < nb && zero; ++b) zero = data[b] == 0; }
            if (zero) silentFrames_ += got; else silentFrames_ = 0; }
          for (UINT32 i = 0; i < got; ++i) for (int c = 0; c < ci; ++c) fifo_[static_cast<std::size_t>(c)].push((fl & AUDCLNT_BUFFERFLAGS_SILENT) ? 0.0f : loadSample(data + (static_cast<std::size_t>(i) * static_cast<std::size_t>(ci) + static_cast<std::size_t>(c)) * sb, sIn_));
          capture_->ReleaseBuffer(got);
        }
        if (lostBy(pr)) break;
      }
      UINT32 pad = 0, room = bufFrames;
      if (out_ && exOut_) room = bufFrames;   // exclusive: each event wants the whole buffer (the padding has no meaning there)
      else if (out_) { const HRESULT pr = out_->GetCurrentPadding(&pad); if (lostBy(pr)) break; room = SUCCEEDED(pr) && pad <= bufFrames ? bufFrames - pad : 0; }
      else if (!out_) room = ci > 0 ? static_cast<UINT32>(fifo_[0].size() / cfg_.frames * cfg_.frames) : cfg_.frames;   // input only: every whole block the capture side has delivered (this loop used to start with room = 0, the callback never ran)
      while (room >= cfg_.frames && !stop_) {
        for (int c = 0; c < ci; ++c) { auto& q = fifo_[static_cast<std::size_t>(c)]; for (UINT32 i = 0; i < cfg_.frames; ++i) { if (q.size() == 0) { inBuf_[static_cast<std::size_t>(c)][i] = 0.0f; ++xruns_; } else inBuf_[static_cast<std::size_t>(c)][i] = q.pop(); } }
        cb_(inp_.data(), outp_.data(), cfg_.frames);
        if (render_) {
          BYTE* data; const HRESULT gr = render_->GetBuffer(cfg_.frames, &data); if (FAILED(gr)) { lostBy(gr); break; }
          const std::size_t sb = sOut_.bytes();
          for (UINT32 i = 0; i < cfg_.frames; ++i) for (int c = 0; c < co; ++c) storeSample(data + (static_cast<std::size_t>(i) * static_cast<std::size_t>(co) + static_cast<std::size_t>(c)) * sb, outBuf_[static_cast<std::size_t>(c)][i], sOut_);
          render_->ReleaseBuffer(cfg_.frames, 0);
        }
        room -= cfg_.frames;
        if (!out_ && !capture_) break;
      }
      if (lost_) break;
      for (auto& q : fifo_) q.trim(static_cast<std::size_t>(cfg_.frames) * 16);      // never let the capture side run away
    }
    if (mm) AvRevertMmThreadCharacteristics(mm);
  }
  // DEVICE INVALIDATED (unplugged, disabled, format changed, audio service restarted): the stream is dead; said once, the loop ends and
  // the owner sees deviceLost() (it used to spin on the failing calls: silence, a frozen console, the device list never told)
  bool lostBy(HRESULT r) { if (classifyStreamError(r) != StreamFault::Lost) return false; lost_ = true; return true; }
  void closeAll() { release(render_); release(capture_); release(out_); release(in_); if (ev_) { CloseHandle(ev_); ev_ = nullptr; } }
  DeviceConfig cfg_; AudioCallback cb_; IAudioClient *out_ = nullptr, *in_ = nullptr; IAudioRenderClient* render_ = nullptr; IAudioCaptureClient* capture_ = nullptr; HANDLE ev_ = nullptr; Sample sOut_, sIn_;
  std::vector<std::vector<float>> inBuf_, outBuf_; std::vector<const float*> inp_; std::vector<float*> outp_; std::vector<Fifo> fifo_;
  bool captureEvent_ = false, loopback_ = false, exOut_ = false, exIn_ = false; std::string note_; std::thread thread_; std::atomic<bool> stop_{false}, lost_{false}; std::atomic<std::uint64_t> xruns_{0}, silentFrames_{0};
};
}  // namespace
std::unique_ptr<Backend> makeWasapiBackend() { return std::make_unique<WasapiBackend>(); }
namespace {
IMMDevice* captureEndpoint(const std::string& id, std::string& err) {   // the recording device with that id ("" = Windows' default one)
  IMMDeviceEnumerator* en = nullptr; IMMDevice* dev = nullptr;
  HRESULT r = CoCreateInstance(kClsidEnumerator, nullptr, CLSCTX_ALL, kIidEnumerator, reinterpret_cast<void**>(&en)); if (FAILED(r)) { err = hr("device enumerator", r); return nullptr; }
  if (id.empty()) r = en->GetDefaultAudioEndpoint(eCapture, eConsole, &dev);
  else { const int n = MultiByteToWideChar(CP_UTF8, 0, id.c_str(), -1, nullptr, 0); std::vector<wchar_t> w(static_cast<std::size_t>(n > 0 ? n : 1), L'\0'); if (n > 0) MultiByteToWideChar(CP_UTF8, 0, id.c_str(), -1, w.data(), n); r = en->GetDevice(w.data(), &dev); }
  en->Release(); if (FAILED(r)) { err = hr("recording device", r) + ": it is unplugged or switched off in Windows"; return nullptr; }
  return dev;
}
const GUID kIidEndpointVolumeW = {0x5CDF2C82, 0x841E, 0x4546, {0x97, 0x22, 0x0C, 0xF7, 0x40, 0x78, 0x22, 0x9A}};
const PROPERTYKEY kDisableSysFx = {{0x1da5d803, 0xd492, 0x4edd, {0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e}}, 5};   // PKEY_AudioEndpoint_Disable_SysFx
}  // namespace
// for WINDOWS RECORDING DEVICES (src/app_capture.cpp): one recording device of Windows as a float stereo capture stream at `rate`
// (shared mode: Windows converts), not started yet. Any device of the Recording tab, beside the sound card the console runs on.
long wasapiDeviceCaptureClient(const std::string& endpointId, double rate, void** audioClient, void** captureClient, std::string& err) {
  *audioClient = nullptr; *captureClient = nullptr; IMMDevice* dev = captureEndpoint(endpointId, err); if (!dev) return E_FAIL;
  IAudioClient* ac = nullptr; IAudioCaptureClient* cc = nullptr;
  HRESULT r = dev->Activate(kIidAudioClient, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&ac)); dev->Release(); if (FAILED(r)) { err = hr("audio client (recording device)", r); return r; }
  WAVEFORMATEXTENSIBLE f; std::memset(&f, 0, sizeof f);
  f.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE; f.Format.nChannels = 2; f.Format.nSamplesPerSec = static_cast<DWORD>(rate); f.Format.wBitsPerSample = 32; f.Format.nBlockAlign = 8;
  f.Format.nAvgBytesPerSec = f.Format.nSamplesPerSec * 8; f.Format.cbSize = 22; f.Samples.wValidBitsPerSample = 32; f.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT; f.SubFormat = kFloatSubFormat;
  r = ac->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY, 2000000 /* 200 ms */, 0, &f.Format, nullptr);
  if (FAILED(r)) { err = hr("Initialize (recording device)", r) + (r == AUDCLNT_E_DEVICE_IN_USE ? ": another program holds it exclusively" : r == E_ACCESSDENIED ? ": Windows microphone privacy blocks desktop apps" : ""); ac->Release(); return r; }
  r = ac->GetService(kIidCaptureClient, reinterpret_cast<void**>(&cc)); if (FAILED(r)) { err = hr("GetService (recording device)", r); ac->Release(); return r; }
  *audioClient = ac; *captureClient = cc; return 0;
}
// what Windows' own Sound properties say about a recording device: its shared format, level, mute and whether enhancements are on
bool wasapiCaptureProperties(const std::string& endpointId, int& channels, double& rate, int& bits, float& level, bool& muted, int& enhancements, std::string& err) {
  channels = 0; rate = 0; bits = 0; level = -1; muted = false; enhancements = -1;
  const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED); IMMDevice* dev = captureEndpoint(endpointId, err);
  if (!dev) { if (SUCCEEDED(co)) CoUninitialize(); return false; }
  IAudioClient* ac = nullptr;
  if (SUCCEEDED(dev->Activate(kIidAudioClient, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&ac)))) { WAVEFORMATEX* f = nullptr;
    if (SUCCEEDED(ac->GetMixFormat(&f)) && f) { channels = f->nChannels; rate = f->nSamplesPerSec; bits = f->wFormatTag == WAVE_FORMAT_EXTENSIBLE && f->cbSize >= 22 ? reinterpret_cast<WAVEFORMATEXTENSIBLE*>(f)->Samples.wValidBitsPerSample : f->wBitsPerSample; CoTaskMemFree(f); }
    ac->Release(); }
  IAudioEndpointVolume* vol = nullptr;
  if (SUCCEEDED(dev->Activate(kIidEndpointVolumeW, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&vol)))) { float l = 0; BOOL m = FALSE; if (SUCCEEDED(vol->GetMasterVolumeLevelScalar(&l))) level = l; if (SUCCEEDED(vol->GetMute(&m))) muted = m != FALSE; vol->Release(); }
  IPropertyStore* ps = nullptr;
  if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &ps))) { PROPVARIANT v; PropVariantInit(&v); if (SUCCEEDED(ps->GetValue(kDisableSysFx, &v))) enhancements = v.vt == VT_UI4 ? (v.ulVal ? 0 : 1) : 1; PropVariantClear(&v); ps->Release(); }   // the key is absent on a device that never had them switched off: enhancements as the driver ships them
  dev->Release(); if (SUCCEEDED(co)) CoUninitialize(); return true;
}
// for APP CAPTURE (src/app_capture.cpp): one program's sound as a float stereo capture stream at `rate`, not started yet
long wasapiAppCaptureClient(unsigned long pid, double rate, void** audioClient, void** captureClient, std::string& err) {
  IAudioClient* ac = nullptr; IAudioCaptureClient* cc = nullptr; *audioClient = nullptr; *captureClient = nullptr;
  HRESULT r = activateProcessLoopback(&ac, err, static_cast<DWORD>(pid)); if (FAILED(r)) return r;
  WAVEFORMATEXTENSIBLE f; std::memset(&f, 0, sizeof f);
  f.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE; f.Format.nChannels = 2; f.Format.nSamplesPerSec = static_cast<DWORD>(rate); f.Format.wBitsPerSample = 32; f.Format.nBlockAlign = 8;
  f.Format.nAvgBytesPerSec = f.Format.nSamplesPerSec * 8; f.Format.cbSize = 22; f.Samples.wValidBitsPerSample = 32; f.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT; f.SubFormat = kFloatSubFormat;
  r = ac->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY, 2000000 /* 200 ms */, 0, &f.Format, nullptr);
  if (FAILED(r)) { err = hr("Initialize (app capture)", r); ac->Release(); return r; }
  r = ac->GetService(kIidCaptureClient, reinterpret_cast<void**>(&cc)); if (FAILED(r)) { err = hr("GetService (app capture)", r); ac->Release(); return r; }
  *audioClient = ac; *captureClient = cc; return 0;
}
}  // namespace am
#endif
