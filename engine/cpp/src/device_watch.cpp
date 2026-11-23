// DEVICE WATCH (see audiomixer/device_watch.hpp)
#include "audiomixer/device_watch.hpp"

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <mmdeviceapi.h>
#endif

namespace am {

StreamFault classifyStreamError(long hr) {
  const unsigned long u = static_cast<unsigned long>(hr) & 0xFFFFFFFFul;   // the 32-bit HRESULT (long is 64-bit off Windows)
  if ((u & 0x80000000ul) == 0) return StreamFault::None;
  switch (u) {
    case 0x88890004ul:   // AUDCLNT_E_DEVICE_INVALIDATED: unplugged, disabled, its format changed in Windows' Sound settings
    case 0x88890010ul:   // AUDCLNT_E_SERVICE_NOT_RUNNING: the Windows audio service restarted
    case 0x88890026ul:   // AUDCLNT_E_RESOURCES_INVALIDATED
    case 0x80070490ul:   // E_NOTFOUND (HRESULT_FROM_WIN32(ERROR_NOT_FOUND)): the endpoint is gone
      return StreamFault::Lost;
    case 0x8889000Aul:   // AUDCLNT_E_DEVICE_IN_USE: another program holds it in exclusive mode
      return StreamFault::InUse;
    default: return StreamFault::Other;
  }
}

DeviceDiff diffDeviceNames(const std::vector<std::string>& before, const std::vector<std::string>& after, const std::vector<std::string>& openNames) {
  DeviceDiff d;
  for (const auto& n : after) if (std::find(before.begin(), before.end(), n) == before.end() && std::find(d.added.begin(), d.added.end(), n) == d.added.end()) d.added.push_back(n);
  for (const auto& n : before) if (std::find(after.begin(), after.end(), n) == after.end() && std::find(d.gone.begin(), d.gone.end(), n) == d.gone.end()) d.gone.push_back(n);
  for (const auto& o : openNames) if (!o.empty() && std::find(d.gone.begin(), d.gone.end(), o) != d.gone.end()) d.openDeviceGone = true;
  return d;
}

std::string asioDriverToSkip(bool running, const std::string& backend, const std::string& inId, const std::string& outId) {
  if (!running || backend != "asio") return {};
  if (!outId.empty() && outId != "-") return outId;
  if (!inId.empty() && inId != "-") return inId;
  return {};
}

#ifdef _WIN32
namespace {
const GUID kClsidEnum = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
const GUID kIidEnum = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
const GUID kIidNotify = {0x7991EEC9, 0x7E89, 0x4D85, {0x83, 0x90, 0x6C, 0x70, 0x3C, 0xEC, 0x60, 0xC0}};
const GUID kIidUnk = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
// the system calls these on its own threads; each only hands the news on (never blocks, never calls back into the enumerator)
class Client final : public IMMNotificationClient {
 public:
  explicit Client(std::function<void()> f) : f_(std::move(f)) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** p) override {
    if (IsEqualGUID(id, kIidUnk) || IsEqualGUID(id, kIidNotify)) { *p = static_cast<IMMNotificationClient*>(this); AddRef(); return S_OK; }
    *p = nullptr; return E_NOINTERFACE; }
  ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(++refs_); }
  ULONG STDMETHODCALLTYPE Release() override { const long n = --refs_; if (n == 0) delete this; return static_cast<ULONG>(n); }
  HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { fire(); return S_OK; }
  HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { fire(); return S_OK; }
  HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { fire(); return S_OK; }
  HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override { fire(); return S_OK; }
  HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }   // volume / name chatter: not a device change
  void off() { std::lock_guard<std::mutex> l(m_); f_ = nullptr; }
 private:
  void fire() { std::lock_guard<std::mutex> l(m_); if (f_) f_(); }
  std::mutex m_; std::function<void()> f_; std::atomic<long> refs_{1};
};
}  // namespace
struct DeviceChangeWatch::Impl {
  std::thread t; std::mutex m; std::condition_variable cv; bool quit = false, ready = false; std::atomic<bool> ok{false};
};
DeviceChangeWatch::DeviceChangeWatch(std::function<void()> onChange) : impl_(std::make_unique<Impl>()) {
  Impl* im = impl_.get();
  im->t = std::thread([im, f = std::move(onChange)]() mutable {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);   // its own apartment: the console's threads keep whatever they have
    IMMDeviceEnumerator* en = nullptr; Client* c = new Client(std::move(f));
    if (SUCCEEDED(CoCreateInstance(kClsidEnum, nullptr, CLSCTX_ALL, kIidEnum, reinterpret_cast<void**>(&en))) && en && SUCCEEDED(en->RegisterEndpointNotificationCallback(c))) im->ok = true;
    { std::unique_lock<std::mutex> l(im->m); im->ready = true; im->cv.notify_all(); im->cv.wait(l, [im] { return im->quit; }); }
    c->off();
    if (en) { if (im->ok) en->UnregisterEndpointNotificationCallback(c); en->Release(); }
    c->Release();
    if (SUCCEEDED(co)) CoUninitialize();
  });
  std::unique_lock<std::mutex> l(im->m); im->cv.wait(l, [im] { return im->ready; });
}
DeviceChangeWatch::~DeviceChangeWatch() {
  { std::lock_guard<std::mutex> l(impl_->m); impl_->quit = true; } impl_->cv.notify_all();
  if (impl_->t.joinable()) impl_->t.join();
}
bool DeviceChangeWatch::active() const { return impl_->ok.load(); }
#else
struct DeviceChangeWatch::Impl {};
DeviceChangeWatch::DeviceChangeWatch(std::function<void()>) : impl_(std::make_unique<Impl>()) {}
DeviceChangeWatch::~DeviceChangeWatch() = default;
bool DeviceChangeWatch::active() const { return false; }
#endif

}  // namespace am
