// EXPERIMENTAL, UNWIRED: see native/wasapi-exclusive/README.md.
#include "wasapi.h"

namespace wasapiexclusive {

bool NotYetIntegrated(std::string& reason) {
  reason =
      "wasapi_exclusive.openExclusive() is not wired into Audio Mixer: WASAPI exclusive mode gives one process "
      "sole ownership of the endpoint, and bridge/audify.js (RtAudio) may already have this same device open in "
      "shared mode. Opening it exclusively from here without first coordinating with bridge/audify.js's own "
      "stream lifecycle would make one of the two opens fail, or silently cut the other one off mid-stream. See "
      "native/wasapi-exclusive/README.md, section \"Before wiring this in for real\", before removing this guard.";
  return false;
}

#ifdef _WIN32
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>

namespace {
// Narrow a device's wide-string id / friendly name to UTF-8 for the N-API boundary.
std::string Narrow(const wchar_t* w) {
  if (!w) return std::string();
  int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  if (len <= 0) return std::string();
  std::string out(len - 1, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, -1, out.empty() ? nullptr : &out[0], len, nullptr, nullptr);
  return out;
}
std::wstring Widen(const std::string& s) {
  if (s.empty()) return std::wstring();
  int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring out(len > 0 ? len - 1 : 0, L'\0');
  if (len > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], len);
  return out;
}
// RAII so an early return (a failed enumerator call) never forgets CoUninitialize(); CoInitializeEx is reference
// counted per thread, so a nested call elsewhere in the same thread is harmless.
struct ComScope {
  HRESULT hr;
  ComScope() : hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
  ~ComScope() { if (SUCCEEDED(hr)) CoUninitialize(); }
  bool Ok() const { return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE; }   // already initialized differently by Node/libuv: fine, just don't double-uninitialize wrongly
};
}  // namespace

std::vector<Endpoint> ListEndpoints() {
  std::vector<Endpoint> out;
  ComScope com; if (!com.Ok()) return out;
  IMMDeviceEnumerator* enumerator = nullptr;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&enumerator))) return out;

  std::wstring defaultRenderId, defaultCaptureId;
  IMMDevice* defDev = nullptr;
  if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defDev)) && defDev) {
    LPWSTR id = nullptr; if (SUCCEEDED(defDev->GetId(&id)) && id) { defaultRenderId = id; CoTaskMemFree(id); }
    defDev->Release();
  }
  defDev = nullptr;
  if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &defDev)) && defDev) {
    LPWSTR id = nullptr; if (SUCCEEDED(defDev->GetId(&id)) && id) { defaultCaptureId = id; CoTaskMemFree(id); }
    defDev->Release();
  }

  for (int flow = 0; flow < 2; ++flow) {
    const EDataFlow dataFlow = flow == 0 ? eRender : eCapture;
    IMMDeviceCollection* collection = nullptr;
    if (FAILED(enumerator->EnumAudioEndpoints(dataFlow, DEVICE_STATE_ACTIVE, &collection)) || !collection) continue;
    UINT count = 0; collection->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
      IMMDevice* device = nullptr; if (FAILED(collection->Item(i, &device)) || !device) continue;
      LPWSTR idW = nullptr; std::string id, name; bool isDefault = false;
      if (SUCCEEDED(device->GetId(&idW)) && idW) {
        id = Narrow(idW);
        isDefault = (dataFlow == eRender ? defaultRenderId : defaultCaptureId) == idW;
        CoTaskMemFree(idW);
      }
      IPropertyStore* store = nullptr;
      if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) && store) {
        PROPVARIANT var; PropVariantInit(&var);
        if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &var)) && var.vt == VT_LPWSTR) name = Narrow(var.pwszVal);
        PropVariantClear(&var);
        store->Release();
      }
      device->Release();
      if (!id.empty()) out.push_back({ id, name, dataFlow == eRender, isDefault });
    }
    collection->Release();
  }
  enumerator->Release();
  return out;
}

bool ProbeExclusiveFormat(const std::string& endpointId, int sampleRate, int channels, int bitsPerSample,
                           bool& supported, std::string& error) {
  supported = false;
  if (sampleRate <= 0 || channels <= 0 || (bitsPerSample != 16 && bitsPerSample != 24 && bitsPerSample != 32)) {
    error = "sampleRate and channels must be positive; bitsPerSample must be 16, 24 or 32";
    return false;
  }
  ComScope com; if (!com.Ok()) { error = "CoInitializeEx failed"; return false; }
  IMMDeviceEnumerator* enumerator = nullptr;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&enumerator))) {
    error = "could not create the device enumerator"; return false;
  }
  IMMDevice* device = nullptr;
  const std::wstring idW = Widen(endpointId);
  const HRESULT getHr = enumerator->GetDevice(idW.c_str(), &device);
  enumerator->Release();
  if (FAILED(getHr) || !device) { error = "endpoint id not found (it may have been unplugged or changed since listEndpoints() was called)"; return false; }

  IAudioClient* client = nullptr;
  const HRESULT actHr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&client);
  device->Release();
  if (FAILED(actHr) || !client) { error = "could not activate IAudioClient on this endpoint (it may be in exclusive use by another program already)"; return false; }

  WAVEFORMATEX wfx = {};
  wfx.wFormatTag = WAVE_FORMAT_PCM;
  wfx.nChannels = static_cast<WORD>(channels);
  wfx.nSamplesPerSec = static_cast<DWORD>(sampleRate);
  wfx.wBitsPerSample = static_cast<WORD>(bitsPerSample);
  wfx.nBlockAlign = static_cast<WORD>(wfx.nChannels * wfx.wBitsPerSample / 8);
  wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

  // Exclusive mode's IsFormatSupported contract: the closest-match out parameter must be null (passing one here is
  // itself an error for this share mode, per the WASAPI documentation) - there is no "nearest supported format"
  // negotiation in exclusive mode the way there is in shared mode, only an exact accept or refuse.
  const HRESULT hr = client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &wfx, nullptr);
  client->Release();
  if (hr == S_OK) { supported = true; return true; }
  if (hr == AUDCLNT_E_UNSUPPORTED_FORMAT) { supported = false; return true; }
  error = "IsFormatSupported returned an unexpected result (hr=0x" + std::to_string(static_cast<unsigned long>(hr)) + ")";
  return false;
}
#endif

}  // namespace wasapiexclusive
