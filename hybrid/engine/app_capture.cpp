#ifdef _WIN32
#include "app_capture.hpp"
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <tlhelp32.h>
#include <avrt.h>
#include <map>

namespace amfb {
namespace {

// audioclientactivationparams.h (Windows SDK 10.0.20348) - declared here so older tool chains build too
struct ProcessLoopbackParams { DWORD targetProcessId; int mode; };          // mode 0 = the program and everything it started
struct ActivationParams { int type; ProcessLoopbackParams loopback; };      // type 1 = process loopback
const wchar_t* const kLoopbackDevice = L"VAD\\Process_Loopback";
const GUID kIidAudioClient = {0x1CB9AD4C, 0xDBFA, 0x4c32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
const GUID kIidCaptureClient = {0xC8ADBD64, 0xE71E, 0x48a0, {0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17}};
const GUID kIidHandler = {0x41D949AB, 0x9862, 0x444A, {0x80, 0xF6, 0xC2, 0x61, 0x33, 0x4D, 0xA5, 0xEB}};
const GUID kIidAgile = {0x94EA2B94, 0xE9CC, 0x49E0, {0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90}};
const GUID kFloatSubtype = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

using ActivateFn = HRESULT(WINAPI*)(LPCWSTR, REFIID, PROPVARIANT*, IActivateAudioInterfaceCompletionHandler*, IActivateAudioInterfaceAsyncOperation**);

class Handler final : public IActivateAudioInterfaceCompletionHandler {
 public:
  Handler() : done_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
  virtual ~Handler() { CloseHandle(done_); }
  HANDLE done() const { return done_; }
  STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, IID_IUnknown) || IsEqualGUID(iid, kIidHandler) || IsEqualGUID(iid, kIidAgile)) { *out = this; AddRef(); return S_OK; }
    *out = nullptr; return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ULONG(InterlockedIncrement(&refs_)); }
  STDMETHODIMP_(ULONG) Release() override { const LONG n = InterlockedDecrement(&refs_); if (n == 0) delete this; return ULONG(n); }
  STDMETHODIMP ActivateCompleted(IActivateAudioInterfaceAsyncOperation*) override { SetEvent(done_); return S_OK; }

 private:
  LONG refs_ = 1; HANDLE done_;
};

// The first process of that name whose parent is not the same program: the root of its process tree.
DWORD findRoot(const std::wstring& exe) {
  const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return 0;
  std::map<DWORD, DWORD> parentOf;
  PROCESSENTRY32W e{}; e.dwSize = sizeof e;
  for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e))
    if (_wcsicmp(e.szExeFile, exe.c_str()) == 0) parentOf[e.th32ProcessID] = e.th32ParentProcessID;
  CloseHandle(snap);
  for (const auto& [pid, parent] : parentOf) if (!parentOf.count(parent)) return pid;
  return parentOf.empty() ? 0 : parentOf.begin()->first;
}

template <class T> struct Com {
  T* p = nullptr;
  ~Com() { if (p) p->Release(); }
  T* operator->() const { return p; }
};

}  // namespace

void AppCapture::start() {
  if (th_.joinable()) return;
  quit_ = false;
  wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  th_ = std::thread([this] { run(); });
}

void AppCapture::stop() {
  if (!th_.joinable()) return;
  quit_ = true;
  SetEvent(static_cast<HANDLE>(wake_));
  th_.join();
  CloseHandle(static_cast<HANDLE>(wake_));
  wake_ = nullptr;
}

void AppCapture::run() {
  const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  while (!quit_) {
    const DWORD pid = findRoot(exe_);
    if (pid) session(pid);
    capturing_ = false;
    if (!quit_) WaitForSingleObject(static_cast<HANDLE>(wake_), 2000);   // look again every two seconds
  }
  if (SUCCEEDED(co)) CoUninitialize();
}

bool AppCapture::session(unsigned long pid) {
  const HMODULE mm = LoadLibraryW(L"mmdevapi.dll");
  const auto activate = mm ? reinterpret_cast<ActivateFn>(reinterpret_cast<void*>(GetProcAddress(mm, "ActivateAudioInterfaceAsync"))) : nullptr;
  if (!activate) return false;

  ActivationParams params{1, {DWORD(pid), 0}};
  PROPVARIANT pv; PropVariantInit(&pv);
  pv.vt = VT_BLOB; pv.blob.cbSize = sizeof params; pv.blob.pBlobData = reinterpret_cast<BYTE*>(&params);

  Handler* handler = new Handler;
  Com<IActivateAudioInterfaceAsyncOperation> op;
  HRESULT hr = activate(kLoopbackDevice, kIidAudioClient, &pv, handler, &op.p);
  if (SUCCEEDED(hr)) WaitForSingleObject(handler->done(), 5000);
  Com<IAudioClient> client;
  if (SUCCEEDED(hr)) {
    HRESULT result = E_FAIL; IUnknown* unk = nullptr;
    hr = op->GetActivateResult(&result, &unk);
    if (SUCCEEDED(hr)) hr = result;
    if (SUCCEEDED(hr) && unk) hr = unk->QueryInterface(kIidAudioClient, reinterpret_cast<void**>(&client.p));
    if (unk) unk->Release();
  }
  handler->Release();
  if (FAILED(hr) || !client.p) return false;                             // Windows older than 10 2004, or the program is gone

  WAVEFORMATEXTENSIBLE fmt{};
  fmt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE; fmt.Format.nChannels = 2; fmt.Format.nSamplesPerSec = DWORD(rate_);
  fmt.Format.wBitsPerSample = 32; fmt.Format.nBlockAlign = 8; fmt.Format.nAvgBytesPerSec = DWORD(rate_) * 8;
  fmt.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
  fmt.Samples.wValidBitsPerSample = 32; fmt.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT; fmt.SubFormat = kFloatSubtype;
  const DWORD flags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                      0x80000000u /*AUTOCONVERTPCM*/ | 0x08000000u /*SRC_DEFAULT_QUALITY*/;
  if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0, &fmt.Format, nullptr))) return false;

  const HANDLE ready = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  Com<IAudioCaptureClient> cap;
  bool ok = SUCCEEDED(client->SetEventHandle(ready)) &&
            SUCCEEDED(client->GetService(kIidCaptureClient, reinterpret_cast<void**>(&cap.p))) && SUCCEEDED(client->Start());
  const HANDLE proc = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid));
  DWORD task = 0;
  const HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task);
  capturing_ = ok;

  static const float silence[2048] = {};
  while (ok && !quit_) {
    HANDLE waits[3] = {ready, static_cast<HANDLE>(wake_), proc};
    const DWORD w = WaitForMultipleObjects(proc ? 3 : 2, waits, FALSE, 200);
    if (w == WAIT_OBJECT_0 + 2) break;                                   // the program closed
    if (w == WAIT_OBJECT_0 + 1 || w == WAIT_FAILED) continue;
    for (;;) {
      UINT32 frames = 0;
      if (FAILED(cap->GetNextPacketSize(&frames))) { ok = false; break; }
      if (!frames) break;
      BYTE* data = nullptr; DWORD f = 0;
      if (FAILED(cap->GetBuffer(&data, &frames, &f, nullptr, nullptr))) { ok = false; break; }
      if (f & AUDCLNT_BUFFERFLAGS_SILENT) {
        for (UINT32 left = frames; left;) { const UINT32 n = left < 1024 ? left : 1024; ring_.write(silence, n * 2); left -= n; }
      } else {
        ring_.write(reinterpret_cast<const float*>(data), std::size_t(frames) * 2);   // a full ring drops the rest: the mixer is behind
      }
      cap->ReleaseBuffer(frames);
    }
  }
  client->Stop();
  if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
  if (proc) CloseHandle(proc);
  CloseHandle(ready);
  return true;
}

}  // namespace amfb
#endif
