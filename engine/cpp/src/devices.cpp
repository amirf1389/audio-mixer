// listDevices(): every device the backends of this binary can open, for `audiomixer-native --list-devices` (the bridge's device list).
//   WASAPI   active render and capture endpoints: id = the endpoint id string, channels and rate from the shared-mode mix format; every render endpoint
//            also as "loopback:<id>", an input that captures what Windows plays on it (app audio: players, browsers, streaming apps)
//   DirectSound  every render and capture device DirectSound enumerates (Bluetooth headsets / speakers included); id = the device GUID "{...}", "default" for the Windows default
//   ASIO     the registered drivers, probed through the Steinberg interface (real channel counts and rates); id = the driver name
//   ALSA     the PCM names ALSA advertises (snd_device_name_hint), each opened for a moment (non-blocking) to read its real channel count;
//            a software layer that converts any count (plug, PulseAudio), a busy device or one that cannot be opened is reported as 2 channels
//   Core Audio  the system default device only (the backend does not choose another device yet)
//   null     one timed device that needs no hardware
// Probing an ASIO driver loads it: a caller must not list while one of its ASIO streams is open (ASIO drivers serve one client).
#ifdef _WIN32
#include <endpointvolume.h>
#include <cctype>
#include <initializer_list>
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <propsys.h>
#include <mmsystem.h>
#include <dsound.h>
#endif
#ifdef AM_HAVE_ALSA
#include <alsa/asoundlib.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "audiomixer/asio.hpp"
#include "audiomixer/runtime.hpp"

namespace am {
std::string classifyDevice(const std::string& backend, const std::string& name, int formFactor, bool capture) {
  std::string n; for (char c : name) n += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  auto has = [&n](std::initializer_list<const char*> w) { for (const char* x : w) if (n.find(x) != std::string::npos) return true; return false; };
  if (n.rfind("app audio", 0) == 0) return "app";
  if (has({"bluetooth", "hands-free", "handsfree", "a2dp", "airpods", "buds", " bt ", "wh-1000", "wf-1000", "jbl tune", "jbl flip", "bose qc", "soundcore"})) return "bluetooth";
  // a DAC: a playback-only converter (a USB DAC, a headphone DAC / amp, a dongle). Told by its name; the capture side of a device is never one.
  if (!capture && backend != "asio" && has({"dac", "dragonfly", "topping", "fiio", "smsl", "schiit", "ifi ", "hidizs", "chord mojo", "hip-dac", "qudelix", "hi-res audio", "hifi"})) return "dac";
  if (backend == "asio") return has({"asio4all", "flexasio", "asio2ks", "generic low latency", "voicemeeter"}) ? "wrapper" : "interface";
  if (has({"behringer", "umc", "um2", "u-phoria", "focusrite", "scarlett", "motu", "presonus", "audiobox", "steinberg ur", "audient", "rme ", "fireface", "babyface", "zoom ", "tascam", "m-audio", "m-track", "yamaha ag", "universal audio", "apollo", "ssl 2", "komplete audio", "roland rubix", "usb audio codec", "audio interface"})) return "interface";
  switch (formFactor) { case 3: return "headphones"; case 5: case 6: return "headset"; case 1: return "speakers"; case 4: return "microphone"; case 2: return "line"; case 7: case 8: case 9: return "digital"; default: break; }
  if (has({"headset"})) return "headset";
  if (has({"headphone", "earphone", "earbud", "kopfh"})) return "headphones";
  if (has({"microphone", "mic ", "mic)", "mikrofon", "webcam", "camera"}) || n == "mic") return "microphone";
  if (has({"spdif", "s/pdif", "digital", "hdmi", "displayport", "nvidia high", "monitor"})) return "digital";
  if (has({"line in", "line out", "line (", "stereo mix", "aux"})) return "line";
  if (has({"speaker", "lautsprecher", "realtek", "high definition audio"})) return capture ? "microphone" : "speakers";
  return "unknown";
}
namespace {

#ifdef _WIN32
const GUID kClsidDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
const GUID kIidDeviceEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
const GUID kIidClient = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
const PROPERTYKEY kFormFactor = {{0x1DA5D803, 0xD492, 0x4EDD, {0x8C, 0x23, 0xE0, 0xC0, 0xFF, 0xEE, 0x7F, 0x0E}}, 0};      // PKEY_AudioEndpoint_FormFactor
const PROPERTYKEY kFriendlyName = {{0xA45C254E, 0xDF1C, 0x4EFD, {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}}, 14};   // PKEY_Device_FriendlyName

std::string utf8(const wchar_t* w) {
  if (!w) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  if (n <= 1) return {};
  std::string s(static_cast<std::size_t>(n - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
  return s;
}

std::string endpointId(IMMDevice* d) {
  LPWSTR w = nullptr; std::string id;
  if (SUCCEEDED(d->GetId(&w))) { id = utf8(w); CoTaskMemFree(w); }
  return id;
}

std::string defaultEndpoint(IMMDeviceEnumerator* en, EDataFlow flow) {
  IMMDevice* d = nullptr; std::string id;
  if (SUCCEEDED(en->GetDefaultAudioEndpoint(flow, eConsole, &d))) { id = endpointId(d); d->Release(); }
  return id;
}

void addWasapi(std::vector<DeviceInfo>& out) {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);   // already initialised by the caller: fine
  IMMDeviceEnumerator* en = nullptr;
  if (FAILED(CoCreateInstance(kClsidDeviceEnumerator, nullptr, CLSCTX_ALL, kIidDeviceEnumerator, reinterpret_cast<void**>(&en)))) return;
  for (const EDataFlow flow : {eRender, eCapture}) {
    const std::string def = defaultEndpoint(en, flow);
    IMMDeviceCollection* col = nullptr;
    if (FAILED(en->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &col))) continue;
    UINT n = 0; col->GetCount(&n);
    for (UINT i = 0; i < n; ++i) {
      IMMDevice* d = nullptr;
      if (FAILED(col->Item(i, &d))) continue;
      DeviceInfo info; info.backend = "wasapi"; info.id = endpointId(d); int form = -1;
      IPropertyStore* ps = nullptr;
      if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &ps))) {
        PROPVARIANT v; PropVariantInit(&v);
        if (SUCCEEDED(ps->GetValue(kFriendlyName, &v)) && v.vt == VT_LPWSTR) info.name = utf8(v.pwszVal);
        PropVariantClear(&v); PropVariantInit(&v);
        if (SUCCEEDED(ps->GetValue(kFormFactor, &v)) && v.vt == VT_UI4) form = static_cast<int>(v.ulVal);
        PropVariantClear(&v); ps->Release();
      }
      int ch = 2;
      IAudioClient* ac = nullptr;
      if (SUCCEEDED(d->Activate(kIidClient, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&ac)))) {
        WAVEFORMATEX* f = nullptr;
        if (SUCCEEDED(ac->GetMixFormat(&f)) && f) { ch = f->nChannels; info.defaultRate = f->nSamplesPerSec; CoTaskMemFree(f); }
        ac->Release();
      }
      if (flow == eRender) { info.outChannels = ch; info.defaultOut = info.id == def; }
      else { info.inChannels = ch; info.defaultIn = info.id == def; }
      if (info.name.empty()) info.name = info.id;
      info.kind = classifyDevice("wasapi", info.name, form, flow == eCapture);
      out.push_back(info);
      if (flow == eRender) {   // APP AUDIO: the same endpoint as an INPUT in loopback mode = everything Windows plays on it (a music player, a browser, a streaming app)
        DeviceInfo lb; lb.backend = "wasapi"; lb.id = "loopback:" + info.id; lb.name = "App audio: what plays on " + info.name; lb.inChannels = ch; lb.defaultRate = info.defaultRate; lb.kind = "app"; out.push_back(lb); }
      d->Release();
    }
    col->Release();
  }
  en->Release();
}


struct DsList { std::vector<DeviceInfo>* out; bool capture; };
BOOL CALLBACK dsEnum(LPGUID guid, LPCSTR desc, LPCSTR, LPVOID ctx) {
  DsList* l = static_cast<DsList*>(ctx); DeviceInfo info; info.backend = "dsound";
  if (guid) { char b[64]; std::snprintf(b, sizeof b, "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", static_cast<unsigned long>(guid->Data1), guid->Data2, guid->Data3, guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3], guid->Data4[4], guid->Data4[5], guid->Data4[6], guid->Data4[7]); info.id = b; }
  else info.id = "default";
  info.name = desc ? desc : info.id; info.defaultRate = 48000;
  if (l->capture) { info.inChannels = 2; info.defaultIn = guid == nullptr; } else { info.outChannels = 2; info.defaultOut = guid == nullptr; }
  l->out->push_back(info); return TRUE;
}
void addDsound(std::vector<DeviceInfo>& out) { DsList r{&out, false}; DirectSoundEnumerateA(&dsEnum, &r); DsList c{&out, true}; DirectSoundCaptureEnumerateA(&dsEnum, &c); }

void addAsio(std::vector<DeviceInfo>& out, bool probe) {
  for (const auto& d : listAsioDrivers()) {
    AsioProbeInfo p; if (probe) p = probeAsioDriver(d); else { p.inChannels = 2; p.outChannels = 2; }   // a driver of the other bitness is not loaded: it comes back with error set and no channels
    DeviceInfo info; info.backend = "asio"; info.id = d.name; info.name = d.bits == static_cast<int>(sizeof(void*) * 8) ? d.name : d.name + " (" + std::to_string(d.bits) + "-bit driver)";
    info.inChannels = static_cast<int>(p.inChannels); info.outChannels = static_cast<int>(p.outChannels);
    info.sampleRates = p.sampleRates;
    if (!p.sampleRates.empty()) {
      info.defaultRate = p.sampleRates.front();
      for (const double r : p.sampleRates) if (r == 48000) info.defaultRate = r;
    }
    out.push_back(info);
  }
}
#endif

#ifdef AM_HAVE_ALSA
// The channels one direction of a PCM offers: >0 the device's own maximum, 0 a layer that converts any count, -1 it could not be opened (busy, absent).
// rate: 48000 when the device takes it, else the closest end of its range.
int alsaChannels(const char* name, snd_pcm_stream_t dir, double* rate) {
  snd_pcm_t* pcm = nullptr;
  if (snd_pcm_open(&pcm, name, dir, SND_PCM_NONBLOCK) < 0) return -1;
  snd_pcm_hw_params_t* hw = nullptr;
  int ch = -1;
  if (snd_pcm_hw_params_malloc(&hw) >= 0) {
    if (snd_pcm_hw_params_any(pcm, hw) >= 0) {
      unsigned mx = 0;
      if (snd_pcm_hw_params_get_channels_max(hw, &mx) >= 0) ch = mx > 64 ? 0 : static_cast<int>(mx);
      unsigned lo = 0, hi = 0; int d = 0;
      if (rate && snd_pcm_hw_params_get_rate_min(hw, &lo, &d) >= 0 && snd_pcm_hw_params_get_rate_max(hw, &hi, &d) >= 0 && hi > 0)
        *rate = (48000 >= lo && 48000 <= hi) ? 48000.0 : static_cast<double>(48000 < lo ? lo : hi);
    }
    snd_pcm_hw_params_free(hw);
  }
  snd_pcm_close(pcm);
  return ch;
}

void addAlsa(std::vector<DeviceInfo>& out) {
  void** hints = nullptr;
  if (snd_device_name_hint(-1, "pcm", &hints) < 0) return;
  for (void** h = hints; *h; ++h) {
    char* name = snd_device_name_get_hint(*h, "NAME");
    char* desc = snd_device_name_get_hint(*h, "DESC");
    char* io = snd_device_name_get_hint(*h, "IOID");
    if (name && std::strcmp(name, "null") != 0) {
      DeviceInfo info; info.backend = "alsa"; info.id = name;
      std::string label = desc ? desc : name;
      for (char& c : label) if (c == '\n') c = ' ';
      info.name = label;
      const bool in = !io || std::strcmp(io, "Input") == 0, outDir = !io || std::strcmp(io, "Output") == 0;
      double rate = 48000;
      const int ci = in ? alsaChannels(name, SND_PCM_STREAM_CAPTURE, &rate) : -1, co = outDir ? alsaChannels(name, SND_PCM_STREAM_PLAYBACK, &rate) : -1;
      info.inChannels = in ? (ci > 0 ? ci : 2) : 0; info.outChannels = outDir ? (co > 0 ? co : 2) : 0;
      info.defaultRate = rate;
      info.defaultIn = info.defaultOut = std::strcmp(name, "default") == 0;
      out.push_back(info);
    }
    std::free(name); std::free(desc); std::free(io);
  }
  snd_device_name_free_hint(hints);
}
#endif

}  // namespace

std::string deviceFingerprint() {
  std::string f;
#ifdef _WIN32
  { std::vector<DeviceInfo> v; addWasapi(v); for (const auto& d : v) { f += d.id; f += d.inChannels > 0 ? 'i' : 'o'; f += '|'; } }
  for (const auto& d : listAsioDrivers()) { f += "asio:"; f += d.name; f += '|'; }   // registry only: the driver is not loaded
  { std::vector<DeviceInfo> v; addDsound(v); for (const auto& d : v) { f += d.id; f += '|'; } }
#endif
#ifdef AM_HAVE_ALSA
  { void** hints = nullptr; if (snd_device_name_hint(-1, "pcm", &hints) >= 0) { for (void** h = hints; *h; ++h) { char* name = snd_device_name_get_hint(*h, "NAME"); if (name) { f += name; f += '|'; std::free(name); } } snd_device_name_free_hint(hints); } }
#endif
  return f;
}

std::vector<DeviceInfo> listDevices(bool probeAsio) {
  (void)probeAsio;
  std::vector<DeviceInfo> v;
#ifdef AM_HAVE_ALSA
  addAlsa(v);
#endif
#ifdef _WIN32
  addWasapi(v);
  addAsio(v, probeAsio);
  addDsound(v);
#endif
#ifdef __APPLE__
  DeviceInfo ca; ca.backend = "coreaudio"; ca.name = "System default"; ca.inChannels = 2; ca.outChannels = 2; ca.defaultIn = ca.defaultOut = true;
  v.push_back(ca);
#endif
  DeviceInfo nul; nul.backend = "null"; nul.id = "null"; nul.name = "Null device (timed, no hardware)"; nul.inChannels = 2; nul.outChannels = 2;
  v.push_back(nul);
  for (DeviceInfo& d : v) if (d.kind.empty()) d.kind = d.backend == "null" ? "unknown" : classifyDevice(d.backend, d.name, -1, d.inChannels > 0 && d.outChannels == 0);   // every backend that has no form factor of its own: by name
  return v;
}


#ifdef _WIN32
namespace {
const GUID kIidEndpointVolume = {0x5CDF2C82, 0x841E, 0x4546, {0x97, 0x22, 0x0C, 0xF7, 0x40, 0x78, 0x22, 0x9A}};   // IAudioEndpointVolume
IAudioEndpointVolume* defaultRenderVolume() {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);   // already initialised by the caller: fine
  IMMDeviceEnumerator* en = nullptr; IMMDevice* d = nullptr; IAudioEndpointVolume* v = nullptr;
  if (FAILED(CoCreateInstance(kClsidDeviceEnumerator, nullptr, CLSCTX_ALL, kIidDeviceEnumerator, reinterpret_cast<void**>(&en)))) return nullptr;
  if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &d))) { d->Activate(kIidEndpointVolume, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&v)); d->Release(); }
  en->Release(); return v;
}
}  // namespace
bool systemMuteGet(bool& muted) { IAudioEndpointVolume* v = defaultRenderVolume(); if (!v) return false; BOOL m = FALSE; const bool ok = SUCCEEDED(v->GetMute(&m)); v->Release(); muted = m != FALSE; return ok; }
bool systemMuteSet(bool muted) { IAudioEndpointVolume* v = defaultRenderVolume(); if (!v) return false; const HRESULT r = v->SetMute(muted ? TRUE : FALSE, nullptr); v->Release(); return SUCCEEDED(r); }
#else
bool systemMuteGet(bool&) { return false; }
bool systemMuteSet(bool) { return false; }
#endif
}  // namespace am
