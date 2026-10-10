// AudioDevices.exe - lists the Windows WASAPI audio endpoints as JSON on stdout (read-only: it opens no stream and changes nothing).
//   { "ok":true, "devices":[ {"id":"...","name":"Speakers (Realtek)","kind":"output","channels":2,"sampleRate":48000,"default":true,"state":"active"} ] }
// Build (MinGW):  x86_64-w64-mingw32-g++ -O2 -static -municode -o AudioDevices-x64.exe AudioDevices.cpp -lole32 -loleaut32 -luuid
//                 i686-w64-mingw32-g++   -O2 -static -municode -o AudioDevices-x86.exe AudioDevices.cpp -lole32 -loleaut32 -luuid
#define _WIN32_WINNT 0x0601
#define INITGUID
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propidl.h>
#include <cstdio>
#include <string>

static std::string utf8(const wchar_t* w) {
  if (!w) return "";
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  if (n <= 1) return "";
  std::string s(n - 1, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
  return s;
}
static std::string esc(const std::string& in) {
  std::string o;
  for (unsigned char c : in) {
    if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
    else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
    else o += (char)c;
  }
  return o;
}

int wmain() {
  if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) { puts("{\"ok\":false,\"error\":\"COM init failed\"}"); return 1; }
  IMMDeviceEnumerator* en = nullptr;
  if (FAILED(CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, IID_IMMDeviceEnumerator, (void**)&en)) || !en) {
    puts("{\"ok\":false,\"error\":\"no WASAPI device enumerator\"}"); CoUninitialize(); return 1;
  }
  printf("{\"ok\":true,\"devices\":[");
  bool first = true;
  for (int pass = 0; pass < 2; pass++) {
    EDataFlow flow = pass == 0 ? eCapture : eRender;
    IMMDevice* def = nullptr; std::wstring defId;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(flow, eConsole, &def)) && def) { LPWSTR id = nullptr; if (SUCCEEDED(def->GetId(&id)) && id) { defId = id; CoTaskMemFree(id); } def->Release(); }
    IMMDeviceCollection* col = nullptr;
    if (FAILED(en->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &col)) || !col) continue;
    UINT n = 0; col->GetCount(&n);
    for (UINT i = 0; i < n; i++) {
      IMMDevice* d = nullptr; if (FAILED(col->Item(i, &d)) || !d) continue;
      LPWSTR id = nullptr; d->GetId(&id);
      std::string name, ident = utf8(id);
      IPropertyStore* ps = nullptr;
      if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &ps)) && ps) {
        PROPVARIANT v; PropVariantInit(&v);
        if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) name = utf8(v.pwszVal);
        PropVariantClear(&v); ps->Release();
      }
      unsigned ch = 0, rate = 0;
      IAudioClient* ac = nullptr;
      if (SUCCEEDED(d->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr, (void**)&ac)) && ac) {
        WAVEFORMATEX* mix = nullptr;
        if (SUCCEEDED(ac->GetMixFormat(&mix)) && mix) { ch = mix->nChannels; rate = mix->nSamplesPerSec; CoTaskMemFree(mix); }
        ac->Release();
      }
      bool isDef = id && !defId.empty() && defId == id;
      printf("%s{\"id\":\"%s\",\"name\":\"%s\",\"kind\":\"%s\",\"channels\":%u,\"sampleRate\":%u,\"default\":%s,\"state\":\"active\"}",
        first ? "" : ",", esc(ident).c_str(), esc(name).c_str(), pass == 0 ? "input" : "output", ch, rate, isDef ? "true" : "false");
      first = false;
      if (id) CoTaskMemFree(id);
      d->Release();
    }
    col->Release();
  }
  puts("]}");
  en->Release(); CoUninitialize();
  return 0;
}
