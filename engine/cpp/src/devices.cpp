// listDevices(): every device the backends of this binary can open, for `audiomixer-native --list-devices` (the bridge's device list).
//   WASAPI   active render and capture endpoints: id = the endpoint id string, channels and rate from the shared-mode mix format; every render endpoint
//            also as "loopback:<id>", an input that captures what Windows plays on it (app audio: players, browsers, streaming apps);
//            and "loopback:system" (Windows 10 2004 / build 19041 and later): COMPUTER SOUND, every program except this one (process loopback,
//            this process tree excluded), so the music can be the input while the mix plays on the same headphones or speakers without a loop
//   DirectSound  every render and capture device DirectSound enumerates (Bluetooth headsets / speakers included); id = the device GUID "{...}", "default" for the Windows default
//   ASIO     the registered drivers, probed through the Steinberg interface (real channel counts and rates); id = the driver name
//   ALSA     the PCM names ALSA advertises (snd_device_name_hint), each opened for a moment (non-blocking) to read its real channel count;
//            a software layer that converts any count (plug, PulseAudio), a busy device or one that cannot be opened is reported as 2 channels
//   Core Audio  the system default device only (the backend does not choose another device yet)
//   null     one timed device that needs no hardware
// Probing an ASIO driver loads it: a caller must not list while one of its ASIO streams is open (ASIO drivers serve one client).
#include "audiomixer/driver_intel.hpp"
#include "audiomixer/app_sound.hpp"
#include <algorithm>
#include <mutex>
#ifdef _WIN32
#include <endpointvolume.h>
#include <cctype>
#include <initializer_list>
#include <windows.h>
#include <shellapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <ksmedia.h>
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
int digitalOutputPair(const std::vector<std::string>& names) {
  auto digital = [](const std::string& s) { std::string n; for (char c : s) n += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (n.find("adat") != std::string::npos) return false;
    for (const char* w : {"spdif", "s/pdif", "s-pdif", "s pdif", "optical", "toslink", "coax", "aes", "digital", "dig out", "dig "}) if (n.find(w) != std::string::npos) return true;
    return false; };
  for (std::size_t i = 2; i + 1 < names.size(); i += 2) if (digital(names[i]) && digital(names[i + 1])) return static_cast<int>(i);
  for (std::size_t i = 2; i + 1 < names.size(); ++i) if (digital(names[i]) && digital(names[i + 1])) return static_cast<int>(i);   // a pair that starts on an odd output
  return -1;
}
std::string classifyDevice(const std::string& backend, const std::string& name, int formFactor, bool capture) {
  std::string n; for (char c : name) n += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  auto has = [&n](std::initializer_list<const char*> w) { for (const char* x : w) if (n.find(x) != std::string::npos) return true; return false; };
  if (n.rfind("app audio", 0) == 0) return "app";
  // A DIGITAL CABLE (Windows says so: S/PDIF, HDMI / DisplayPort, digital passthrough): never a Bluetooth link, whatever the brand in the
  // name ("JBL Bar (HDMI)", "Bose Solo (Optical)"): told first, so a soundbar or an amplifier on optical / coaxial / HDMI is not hidden as Bluetooth
  if (formFactor == 7 || formFactor == 8 || formFactor == 9) return "digital";
  if (!capture && has({"optical", "toslink", "coaxial", "coax out", "spdif", "s/pdif", "hdmi", "displayport"})) return "digital";
  if (has({"bluetooth", "hands-free", "handsfree", "a2dp", "airpods", "buds", " bt ", "wh-1000", "wf-1000", "jbl tune", "jbl flip", "bose qc", "soundcore"})) return "bluetooth";
  // a DAC: a playback-only converter (a USB DAC, a headphone DAC / amp, a dongle). Told by its name; the capture side of a device is never one.
  if (!capture && backend != "asio" && has({"dac", "dragonfly", "topping", "fiio", "smsl", "schiit", "ifi ", "hidizs", "chord mojo", "hip-dac", "qudelix", "hi-res audio", "hifi"})) return "dac";
  if (backend == "asio") { const drv::AsioIdentity a = drv::identifyAsio(name); return a.type == "wrapper" ? "wrapper" : (a.type == "virtual" || a.type == "network") ? "virtual" : "interface"; }   // see identifyAsio
  if (has({"behringer", "umc", "um2", "u-phoria", "focusrite", "scarlett", "motu", "presonus", "audiobox", "steinberg ur", "audient", "rme ", "fireface", "babyface", "zoom ", "tascam", "m-audio", "m-track", "yamaha ag", "universal audio", "apollo", "ssl 2", "komplete audio", "roland rubix", "usb audio codec", "audio interface"})) return "interface";
  switch (formFactor) { case 3: return "headphones"; case 5: case 6: return "headset"; case 1: return "speakers"; case 4: return "microphone"; case 2: return "line"; case 7: case 8: case 9: return "digital"; default: break; }
  if (has({"headset"})) return "headset";
  if (has({"headphone", "earphone", "earbud", "kopfh"})) return "headphones";
  if (has({"microphone", "mic ", "mic)", "mikrofon", "webcam", "camera"}) || n == "mic") return "microphone";
  if (has({"spdif", "s/pdif", "optical", "toslink", "coax", "digital", "hdmi", "displayport", "nvidia high", "display audio", "amd high definition"})) return "digital";   // "monitor" alone is not one: studio monitors are speakers
  if (has({"line in", "line out", "line (", "stereo mix", "aux"})) return "line";
  if (has({"speaker", "lautsprecher", "realtek", "high definition audio", "monitor", "soundbar", "sound bar", "amplifier", "receiver"})) return capture ? (n.find("monitor of") != std::string::npos ? "app" : "microphone") : "speakers";
  return "unknown";
}
namespace {
std::string hardwareName(const std::string& name) {   // "Speakers (USB Audio CODEC)" -> "USB Audio CODEC"; no brackets: the whole name
  const std::size_t a = name.find('('), b = name.rfind(')');
  std::string h = a != std::string::npos && b != std::string::npos && b > a + 1 ? name.substr(a + 1, b - a - 1) : name;
  while (!h.empty() && (h[0] == ' ' || (h[0] >= '0' && h[0] <= '9') || h[0] == '-')) h.erase(h.begin());   // "2- USB Audio CODEC": Windows' numbering of a second copy
  return h;
}
std::string lowerOf(std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
}  // namespace

std::vector<DuplexPair> duplexPairs(const std::vector<DeviceInfo>& devices, const std::string& backend) {
  std::vector<DuplexPair> out;
  auto usable = [&](const DeviceInfo& d) { return d.backend == backend && d.id.rfind("loopback:", 0) != 0 && d.kind != "app" && d.kind != "bluetooth"; };
  auto keyOf = [](const DeviceInfo& d) { return d.group.empty() ? "n:" + lowerOf(hardwareName(d.name)) : "g:" + d.group; };
  for (const DeviceInfo& o : devices) {
    if (!usable(o) || o.outChannels <= 0) continue;
    const std::string key = keyOf(o); bool seen = false; for (const DuplexPair& p : out) if (p.outId == o.id) seen = true; if (seen) continue;
    const DeviceInfo* in = nullptr;
    for (const DeviceInfo& i : devices) { if (!usable(i) || i.inChannels <= 0 || keyOf(i) != key) continue; if (i.id == o.id) { in = &i; break; } if (!in || (i.defaultIn && !in->defaultIn) || i.inChannels > in->inChannels) in = &i; }
    if (!in) continue;
    bool dup = false; for (const DuplexPair& p : out) if (p.inId == in->id && p.name == hardwareName(o.name)) dup = true; if (dup) continue;   // one pair per piece of hardware (its first output)
    DuplexPair p; p.backend = backend; p.name = hardwareName(o.name); p.inId = in->id; p.outId = o.id; p.inName = in->name; p.outName = o.name; p.inChannels = in->inChannels; p.outChannels = o.outChannels;
    p.rate = o.defaultRate > 0 ? o.defaultRate : 48000; p.kind = o.kind;
    const std::string n = lowerOf(o.name + " " + in->name);
    p.usb = o.bus == "USB" || in->bus == "USB" || n.find("usb") != std::string::npos || o.kind == "interface" || o.kind == "dac";
    out.push_back(p);
  }
  std::stable_sort(out.begin(), out.end(), [](const DuplexPair& a, const DuplexPair& b) { return a.usb > b.usb; });
  return out;
}

std::string soundSettingsTarget(const std::string& page) {
  if (page == "sound") return "ms-settings:sound";
  if (page == "volume-mixer") return "ms-settings:apps-volume";
  if (page == "devices") return "ms-settings:sound-devices";
  if (page == "microphone-privacy") return "ms-settings:privacy-microphone";
  if (page == "playback") return "control.exe mmsys.cpl,,0";
  if (page == "recording") return "control.exe mmsys.cpl,,1";
  return {};
}

namespace {

#ifdef _WIN32
const GUID kClsidDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
const GUID kIidDeviceEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
const GUID kIidClient = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
const PROPERTYKEY kFormFactor = {{0x1DA5D803, 0xD492, 0x4EDD, {0x8C, 0x23, 0xE0, 0xC0, 0xFF, 0xEE, 0x7F, 0x0E}}, 0};      // PKEY_AudioEndpoint_FormFactor
const PROPERTYKEY kFriendlyName = {{0xA45C254E, 0xDF1C, 0x4EFD, {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}}, 14};   // PKEY_Device_FriendlyName
const PROPERTYKEY kEnumerator = {{0xA45C254E, 0xDF1C, 0x4EFD, {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}}, 24};     // PKEY_Device_EnumeratorName: "USB", "HDAUDIO" ...
const PROPERTYKEY kContainerId = {{0x8C7ED206, 0x3F8A, 0x4827, {0xB3, 0xAB, 0xAE, 0x9E, 0x1F, 0xAE, 0xFC, 0x6C}}, 2};     // PKEY_Device_ContainerId: one per piece of hardware
const PROPERTYKEY kDeviceFormat = {{0xF19F064D, 0x082C, 0x4E27, {0xBC, 0x73, 0x68, 0x82, 0xA1, 0xBB, 0x8E, 0x4C}}, 0};    // PKEY_AudioEngine_DeviceFormat
const PROPERTYKEY kDisableSysFx = {{0x1DA5D803, 0xD492, 0x4EDD, {0x8C, 0x23, 0xE0, 0xC0, 0xFF, 0xEE, 0x7F, 0x0E}}, 5};    // PKEY_AudioEndpoint_Disable_SysFx
// what the property store says about where an endpoint sits: its bus and its hardware (container) id
void busAndGroup(IPropertyStore* ps, std::string& bus, std::string& group);

std::string utf8(const wchar_t* w) {
  if (!w) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  if (n <= 1) return {};
  std::string s(static_cast<std::size_t>(n - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
  return s;
}

void busAndGroup(IPropertyStore* ps, std::string& bus, std::string& group) {
  PROPVARIANT v; PropVariantInit(&v);
  if (SUCCEEDED(ps->GetValue(kEnumerator, &v)) && v.vt == VT_LPWSTR) bus = utf8(v.pwszVal);
  PropVariantClear(&v); PropVariantInit(&v);
  if (SUCCEEDED(ps->GetValue(kContainerId, &v)) && v.vt == VT_CLSID && v.puuid) { const GUID& g = *v.puuid; char b[40];
    std::snprintf(b, sizeof b, "%08lx-%04x-%04x-%02x%02x%02x%02x%02x%02x%02x%02x", static_cast<unsigned long>(g.Data1), g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    group = b; if (group == "00000000-0000-0000-ffffffffffffffff") group.clear(); }   // the "no container" id: every built-in device shares it
  PropVariantClear(&v);
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

// process loopback (ActivateAudioInterfaceAsync with VAD\\Process_Loopback) exists from Windows 10 2004 (build 19041)
bool processLoopbackAvailable() {
  using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*); HMODULE nt = GetModuleHandleW(L"ntdll.dll"); if (!nt) return false;
  const auto fn = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(nt, "RtlGetVersion"))); if (!fn) return false;
  OSVERSIONINFOW v{}; v.dwOSVersionInfoSize = sizeof v; if (fn(&v) != 0) return false;
  return v.dwMajorVersion > 10 || (v.dwMajorVersion == 10 && v.dwBuildNumber >= 19041);
}

void addWasapi(std::vector<DeviceInfo>& out) {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);   // already initialised by the caller: fine
  if (processLoopbackAvailable()) {   // COMPUTER SOUND: the music of every app, never the console's own output (no loop on the same headphones / speakers)
    DeviceInfo sys; sys.backend = "wasapi"; sys.id = "loopback:system"; sys.name = "Computer sound: every app (music, browser, player)"; sys.inChannels = 2; sys.defaultRate = 48000; sys.kind = "app"; out.push_back(sys); 
    // APP SOUND: each program that has a sound stream open now (Spotify, TIDAL, Deezer, Roon, a browser playing YouTube ...), on its own
    for (const am::app::Session& s : am::app::sessions()) { DeviceInfo d; d.backend = "wasapi"; d.id = am::app::deviceId(s.pid); d.name = "App sound: " + s.label + (s.active ? "" : " (silent now)"); d.inChannels = 2; d.defaultRate = 48000; d.kind = "app"; out.push_back(d); } }
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
        PropVariantClear(&v); busAndGroup(ps, info.bus, info.group); ps->Release();
      }
      int ch = 2;
      IAudioClient* ac = nullptr;
      if (SUCCEEDED(d->Activate(kIidClient, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&ac)))) {
        WAVEFORMATEX* f = nullptr;
        if (SUCCEEDED(ac->GetMixFormat(&f)) && f) {
          ch = f->nChannels; info.defaultRate = f->nSamplesPerSec;
          // the rates this endpoint takes on its own (exclusive mode), 44.1 kHz up to 768 kHz, in any of the usual sample formats; the shared-mode rate is always in
          static const DWORD kRates[] = {44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000, 705600, 768000};
          static const WORD kBits[][2] = {{32, 32}, {32, 24}, {24, 24}, {16, 16}};
          for (DWORD r : kRates) {
            bool ok = r == f->nSamplesPerSec;
            for (int k = 0; k < 5 && !ok; ++k) {
              WAVEFORMATEXTENSIBLE x{}; x.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE; x.Format.nChannels = f->nChannels; x.Format.nSamplesPerSec = r;
              const bool isFloat = k == 0; const WORD bits = isFloat ? 32 : kBits[k - 1][0], valid = isFloat ? 32 : kBits[k - 1][1];
              x.Format.wBitsPerSample = bits; x.Format.nBlockAlign = static_cast<WORD>(f->nChannels * bits / 8); x.Format.nAvgBytesPerSec = r * x.Format.nBlockAlign;
              x.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX); x.Samples.wValidBitsPerSample = valid;
              x.dwChannelMask = f->nChannels == 1 ? SPEAKER_FRONT_CENTER : SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
              static const GUID kPcm = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}}, kFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
              x.SubFormat = isFloat ? kFloat : kPcm;
              ok = ac->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &x.Format, nullptr) == S_OK;
            }
            if (ok) info.sampleRates.push_back(static_cast<double>(r));
          }
          CoTaskMemFree(f);
        }
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


std::vector<EndpointInfo> wasapiEndpoints() {
  std::vector<EndpointInfo> v; CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  IMMDeviceEnumerator* en = nullptr;
  if (FAILED(CoCreateInstance(kClsidDeviceEnumerator, nullptr, CLSCTX_ALL, kIidDeviceEnumerator, reinterpret_cast<void**>(&en)))) return v;
  for (const EDataFlow flow : {eRender, eCapture}) {
    const std::string def = defaultEndpoint(en, flow); std::string defComm;
    { IMMDevice* dc = nullptr; if (SUCCEEDED(en->GetDefaultAudioEndpoint(flow, eCommunications, &dc)) && dc) { defComm = endpointId(dc); dc->Release(); } }
    IMMDeviceCollection* col = nullptr;
    if (FAILED(en->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE | DEVICE_STATE_UNPLUGGED | DEVICE_STATE_DISABLED, &col)) || !col) continue;
    UINT n = 0; col->GetCount(&n);
    for (UINT i = 0; i < n; ++i) {
      IMMDevice* d = nullptr; if (FAILED(col->Item(i, &d)) || !d) continue;
      EndpointInfo e; e.id = endpointId(d); e.flow = flow == eRender ? "out" : "in"; DWORD st = 0; d->GetState(&st);
      e.state = st == DEVICE_STATE_ACTIVE ? "active" : st == DEVICE_STATE_UNPLUGGED ? "unplugged" : "disabled"; int form = -1;
      IPropertyStore* ps = nullptr;
      if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &ps))) { PROPVARIANT pv; PropVariantInit(&pv);
        if (SUCCEEDED(ps->GetValue(kFriendlyName, &pv)) && pv.vt == VT_LPWSTR) e.name = utf8(pv.pwszVal);
        PropVariantClear(&pv); PropVariantInit(&pv);
        if (SUCCEEDED(ps->GetValue(kFormFactor, &pv)) && pv.vt == VT_UI4) form = static_cast<int>(pv.ulVal);
        PropVariantClear(&pv); PropVariantInit(&pv);
        if (SUCCEEDED(ps->GetValue(kDeviceFormat, &pv)) && pv.vt == VT_BLOB && pv.blob.cbSize >= sizeof(WAVEFORMATEX) && pv.blob.pBlobData) { const WAVEFORMATEX* f = reinterpret_cast<const WAVEFORMATEX*>(pv.blob.pBlobData);
          e.bits = f->wFormatTag == WAVE_FORMAT_EXTENSIBLE && pv.blob.cbSize >= sizeof(WAVEFORMATEXTENSIBLE) ? reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(f)->Samples.wValidBitsPerSample : f->wBitsPerSample; if (e.bits == 0) e.bits = f->wBitsPerSample; }
        PropVariantClear(&pv); PropVariantInit(&pv);
        if (SUCCEEDED(ps->GetValue(kDisableSysFx, &pv)) && pv.vt == VT_UI4) e.enhancementsOff = pv.ulVal ? 1 : 0;
        PropVariantClear(&pv); busAndGroup(ps, e.bus, e.group); ps->Release(); }
      if (st == DEVICE_STATE_ACTIVE) { IAudioClient* ac = nullptr;   // only an active endpoint can say its format
        if (SUCCEEDED(d->Activate(kIidClient, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&ac)))) { WAVEFORMATEX* f = nullptr;
          if (SUCCEEDED(ac->GetMixFormat(&f)) && f) { e.channels = f->nChannels; e.rate = f->nSamplesPerSec; CoTaskMemFree(f); } ac->Release(); } }
      if (e.name.empty()) e.name = e.id;
      e.kind = classifyDevice("wasapi", e.name, form, flow == eCapture); e.isDefault = e.id == def; e.isDefaultComm = e.id == defComm;
      v.push_back(e); d->Release();
    }
    col->Release();
  }
  en->Release(); return v;
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
    info.probeError = probe ? p.error : std::string();
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

// What is plugged in, cheaply: polled every few seconds on the interface thread, so it must not open a device. Only the ids of the
// active WASAPI endpoints (no IAudioClient, no format probing, no property store) and the ASIO drivers in the registry; DirectSound
// lists the same endpoints, so it is not asked here. The full scan (listDevices) runs only when this changes.
std::string deviceFingerprint() {
  std::string f;
#ifdef _WIN32
  const HRESULT coInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED); IMMDeviceEnumerator* en = nullptr;   // Qt's thread may already be in another mode: then COM is ready and is not ours to close
  if (SUCCEEDED(CoCreateInstance(kClsidDeviceEnumerator, nullptr, CLSCTX_ALL, kIidDeviceEnumerator, reinterpret_cast<void**>(&en))) && en) {
    for (EDataFlow flow : {eRender, eCapture}) { IMMDeviceCollection* col = nullptr; if (FAILED(en->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &col)) || !col) continue;
      UINT n = 0; col->GetCount(&n);
      for (UINT i = 0; i < n; ++i) { IMMDevice* d = nullptr; if (FAILED(col->Item(i, &d)) || !d) continue; LPWSTR id = nullptr;
        if (SUCCEEDED(d->GetId(&id)) && id) { for (const wchar_t* c = id; *c; ++c) f += static_cast<char>(*c & 0x7f); f += flow == eRender ? 'o' : 'i'; f += '|'; CoTaskMemFree(id); } d->Release(); }
      col->Release(); }
    en->Release(); }
  if (SUCCEEDED(coInit)) CoUninitialize();
  for (const auto& d : listAsioDrivers()) { f += "asio:"; f += d.name; f += '|'; }   // registry only: the driver is not loaded
#endif
#ifdef AM_HAVE_ALSA
  { void** hints = nullptr; if (snd_device_name_hint(-1, "pcm", &hints) >= 0) { for (void** h = hints; *h; ++h) { char* name = snd_device_name_get_hint(*h, "NAME"); if (name) { f += name; f += '|'; std::free(name); } } snd_device_name_free_hint(hints); } }
#endif
  return f;
}

std::vector<EndpointInfo> listEndpoints() {
#ifdef _WIN32
  return wasapiEndpoints();
#else
  std::vector<EndpointInfo> v;
  for (const DeviceInfo& d : listDevices(false)) {
    if (d.backend == "null" || d.id.rfind("loopback:", 0) == 0) continue;
    for (const bool out : {true, false}) { const int ch = out ? d.outChannels : d.inChannels; if (ch <= 0) continue;
      EndpointInfo e; e.id = d.id; e.name = d.name; e.flow = out ? "out" : "in"; e.state = "active"; e.kind = d.kind; e.channels = ch; e.rate = d.defaultRate; e.isDefault = out ? d.defaultOut : d.defaultIn; v.push_back(e); } }
  return v;
#endif
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
// The default output's volume control, kept between calls (the interface thread asks for the mute state twice a second): re-made every
// 10 s so a new default device is followed, never on every call. Interface thread only.
IAudioEndpointVolume* makeRenderVolume();
// several threads ask (the console's worker, the remote service's volume watcher): COM is set up on each calling thread, and the cached
// endpoint is shared under a lock
std::mutex g_volM;
IAudioEndpointVolume* defaultRenderVolume() {
  thread_local const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) || true; (void)com;   // once per thread (an STA thread keeps its own: harmless)
  std::lock_guard<std::mutex> g(g_volM);
  static IAudioEndpointVolume* cached = nullptr; static ULONGLONG at = 0; const ULONGLONG now = GetTickCount64();
  if (!cached || now - at > 10000) { if (cached) cached->Release(); cached = makeRenderVolume(); at = now; }
  if (cached) { cached->AddRef(); }
  return cached;
}
IAudioEndpointVolume* makeRenderVolume() {
  IMMDeviceEnumerator* en = nullptr; IMMDevice* d = nullptr; IAudioEndpointVolume* v = nullptr;
  if (FAILED(CoCreateInstance(kClsidDeviceEnumerator, nullptr, CLSCTX_ALL, kIidDeviceEnumerator, reinterpret_cast<void**>(&en)))) return nullptr;
  if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &d))) { d->Activate(kIidEndpointVolume, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&v)); d->Release(); }
  en->Release(); return v;
}
}  // namespace
// WINDOWS' DEFAULT DEVICE: the policy interface Windows' own Sound panel uses (not in the SDK headers, the same on Windows 7 to 11). Only
// SetDefaultEndpoint is called; the methods before it are declared so that it sits at its place in the table.
namespace {
struct IPolicyConfigAm : IUnknown {
  virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX**) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, WAVEFORMATEX**) = 0;
  virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void*) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void*) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR, ERole) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};
const GUID kClsidPolicyConfig = {0x870AF99C, 0x171D, 0x4F9E, {0xAF, 0x0D, 0xE6, 0x3D, 0xF4, 0x0C, 0x2B, 0xC9}};
const GUID kIidPolicyConfig = {0xF8679F50, 0x850A, 0x41CF, {0x9C, 0x72, 0x43, 0x0F, 0x29, 0x02, 0x90, 0xC8}};
std::wstring wide(const std::string& s) { const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0); std::wstring w(n > 0 ? static_cast<std::size_t>(n - 1) : 0, L'\0'); if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n); return w; }
}  // namespace
bool setDefaultEndpoint(const std::string& id, bool alsoCommunications, std::string& err) {
  if (id.empty() || id.size() > 512 || id.rfind("loopback:", 0) == 0) { err = "not a Windows sound device"; return false; }
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  { IMMDeviceEnumerator* en = nullptr; IMMDevice* d = nullptr; DWORD st = 0;   // it must be a device Windows has, and switched on
    if (FAILED(CoCreateInstance(kClsidDeviceEnumerator, nullptr, CLSCTX_ALL, kIidDeviceEnumerator, reinterpret_cast<void**>(&en))) || !en) { err = "Windows' device list is not available"; return false; }
    const bool found = SUCCEEDED(en->GetDevice(wide(id).c_str(), &d)) && d && SUCCEEDED(d->GetState(&st)); if (d) d->Release(); en->Release();
    if (!found) { err = "Windows does not have that device any more"; return false; }
    if (st != DEVICE_STATE_ACTIVE) { err = "that device is unplugged or switched off in Windows"; return false; } }
  IPolicyConfigAm* pc = nullptr;
  if (FAILED(CoCreateInstance(kClsidPolicyConfig, nullptr, CLSCTX_ALL, kIidPolicyConfig, reinterpret_cast<void**>(&pc))) || !pc) { err = "this Windows does not let a program choose the default device: use Windows' Sound settings"; return false; }
  const std::wstring w = wide(id); HRESULT r = pc->SetDefaultEndpoint(w.c_str(), eConsole);
  if (SUCCEEDED(r)) r = pc->SetDefaultEndpoint(w.c_str(), eMultimedia);
  if (SUCCEEDED(r) && alsoCommunications) r = pc->SetDefaultEndpoint(w.c_str(), eCommunications);
  pc->Release();
  if (FAILED(r)) { char b[64]; std::snprintf(b, sizeof b, "Windows refused (0x%08lx)", static_cast<unsigned long>(r)); err = b; return false; }
  return true;
}
bool openSoundSettings(const std::string& page, std::string& err) {
  const std::string t = soundSettingsTarget(page); if (t.empty()) { err = "no such settings page"; return false; }
  const bool panel = t.rfind("control.exe ", 0) == 0;   // the classic Sound panel on its tab, or a Settings page by its own address: fixed strings only
  const std::wstring file = panel ? L"control.exe" : wide(t), args = panel ? wide(t.substr(12)) : std::wstring();
  const auto r = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", file.c_str(), panel ? args.c_str() : nullptr, nullptr, SW_SHOWNORMAL));
  if (r <= 32) { err = "Windows could not open that page"; return false; }
  return true;
}
bool systemMuteGet(bool& muted) { IAudioEndpointVolume* v = defaultRenderVolume(); if (!v) return false; BOOL m = FALSE; const bool ok = SUCCEEDED(v->GetMute(&m)); v->Release(); muted = m != FALSE; return ok; }
bool systemMuteSet(bool muted) { IAudioEndpointVolume* v = defaultRenderVolume(); if (!v) return false; const HRESULT r = v->SetMute(muted ? TRUE : FALSE, nullptr); v->Release(); return SUCCEEDED(r); }
bool systemVolumeGet(float& level) { IAudioEndpointVolume* v = defaultRenderVolume(); if (!v) return false; float f = 0; const bool ok = SUCCEEDED(v->GetMasterVolumeLevelScalar(&f)); v->Release(); level = f; return ok; }
bool systemVolumeSet(float level) { IAudioEndpointVolume* v = defaultRenderVolume(); if (!v) return false; const HRESULT r = v->SetMasterVolumeLevelScalar(std::clamp(level, 0.0f, 1.0f), nullptr); v->Release(); return SUCCEEDED(r); }
#else
bool setDefaultEndpoint(const std::string&, bool, std::string& err) { err = "choosing Windows' default device is a Windows function"; return false; }
bool openSoundSettings(const std::string& page, std::string& err) { err = soundSettingsTarget(page).empty() ? "no such settings page" : "Windows' sound settings exist on Windows only"; return false; }
bool systemMuteGet(bool&) { return false; }
bool systemMuteSet(bool) { return false; }
bool systemVolumeGet(float&) { return false; }
bool systemVolumeSet(float) { return false; }
#endif
}  // namespace am
