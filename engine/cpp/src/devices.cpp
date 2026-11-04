// listDevices(): every device the backends of this binary can open, for `audiomixer-native --list-devices` (the bridge's device list).
//   WASAPI   active render and capture endpoints: id = the endpoint id string, channels and rate from the shared-mode mix format
//   ASIO     the registered drivers, probed through the Steinberg interface (real channel counts and rates); id = the driver name
//   ALSA     the PCM names ALSA advertises (snd_device_name_hint), each opened for a moment (non-blocking) to read its real channel count;
//            a software layer that converts any count (plug, PulseAudio), a busy device or one that cannot be opened is reported as 2 channels
//   Core Audio  the system default device only (the backend does not choose another device yet)
//   null     one timed device that needs no hardware
// Probing an ASIO driver loads it: a caller must not list while one of its ASIO streams is open (ASIO drivers serve one client).
#ifdef _WIN32
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <propsys.h>
#endif
#ifdef AM_HAVE_ALSA
#include <alsa/asoundlib.h>
#endif
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "audiomixer/asio.hpp"
#include "audiomixer/runtime.hpp"

namespace am {
namespace {

#ifdef _WIN32
const GUID kClsidDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
const GUID kIidDeviceEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
const GUID kIidClient = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
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
      DeviceInfo info; info.backend = "wasapi"; info.id = endpointId(d);
      IPropertyStore* ps = nullptr;
      if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &ps))) {
        PROPVARIANT v; PropVariantInit(&v);
        if (SUCCEEDED(ps->GetValue(kFriendlyName, &v)) && v.vt == VT_LPWSTR) info.name = utf8(v.pwszVal);
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
      out.push_back(info);
      d->Release();
    }
    col->Release();
  }
  en->Release();
}

void addAsio(std::vector<DeviceInfo>& out) {
  for (const auto& d : listAsioDrivers()) {
    const AsioProbeInfo p = probeAsioDriver(d);
    DeviceInfo info; info.backend = "asio"; info.id = d.name; info.name = d.name;
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

std::vector<DeviceInfo> listDevices() {
  std::vector<DeviceInfo> v;
#ifdef AM_HAVE_ALSA
  addAlsa(v);
#endif
#ifdef _WIN32
  addWasapi(v);
  addAsio(v);
#endif
#ifdef __APPLE__
  DeviceInfo ca; ca.backend = "coreaudio"; ca.name = "System default"; ca.inChannels = 2; ca.outChannels = 2; ca.defaultIn = ca.defaultOut = true;
  v.push_back(ca);
#endif
  DeviceInfo nul; nul.backend = "null"; nul.id = "null"; nul.name = "Null device (timed, no hardware)"; nul.inChannels = 2; nul.outChannels = 2;
  v.push_back(nul);
  return v;
}

}  // namespace am
