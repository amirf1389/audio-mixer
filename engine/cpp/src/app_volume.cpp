// VOLUME CONTROL: step / dB arithmetic, key routing, the per-app session list (app_volume.hpp), and Windows' audio sessions.
#include "audiomixer/app_volume.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <thread>
#endif

namespace am::vol {

int clampStep(int s) { return s <= 1 ? 1 : s >= 3 ? 3 : 2; }

double stepDb(double db, int dir, int step, double lo, double hi) {
  const double s = clampStep(step);
  if (!std::isfinite(db) || db < lo) db = lo;
  if (db > hi) db = hi;
  if (dir == 0) return db;
  const double k = db / s, eps = 1e-6;
  double next = dir > 0 ? (std::floor(k + eps) + 1) * s : (std::ceil(k - eps) - 1) * s;
  if (dir > 0 && db <= lo) next = std::floor(lo / s + eps) * s + s;   // from off: the first grid line above it
  return std::clamp(next, lo, hi);
}

float stepScalar(float level, int dir, int step) {
  const double s = 0.02 * clampStep(step);
  double v = std::isfinite(level) ? std::clamp(static_cast<double>(level), 0.0, 1.0) : 0.0;
  if (dir == 0) return static_cast<float>(v);
  const double k = v / s, eps = 1e-6;
  v = dir > 0 ? (std::floor(k + eps) + 1) * s : (std::ceil(k - eps) - 1) * s;
  return static_cast<float>(std::clamp(v, 0.0, 1.0));
}

double scalarToDb(float level) { return level > 1e-6f ? 20.0 * std::log10(static_cast<double>(level)) : -120.0; }
float dbToScalar(double db) { return db <= -119.0 ? 0.f : static_cast<float>(std::clamp(std::pow(10.0, db / 20.0), 0.0, 1.0)); }

std::string dbText(double db) {
  if (!std::isfinite(db) || db <= kMasterMinDb) return "-\xe2\x88\x9e";
  char b[16]; std::snprintf(b, sizeof b, "%s%.1f", db > 0.04 ? "+" : "", std::fabs(db) < 0.05 ? 0.0 : db); return b;
}

bool route(const KeyConfig& cfg, Key key, Source src, Action& out) {
  if (src == Source::Media && !cfg.mediaKeys) return false;   // left to Windows and the other programs
  if (src == Source::Hotkey && !cfg.hotkeys) return false;
  out = Action{}; out.target = cfg.target;
  switch (key) { case Key::Up: out.dir = 1; break; case Key::Down: out.dir = -1; break; case Key::Mute: out.toggleMute = true; break; }
  return true;
}

// ---- memory layer ----
bool MemoryLayer::setVolume(const std::string& id, float v) {
  std::function<void(const std::string&, float, bool, bool)> cb; float nv = 0; bool m = false; bool found = false;
  { std::lock_guard<std::mutex> g(m_); ++sets; for (Session& s : s_) if (s.id == id) { s.volume = std::clamp(v, 0.f, 1.f); nv = s.volume; m = s.muted; found = true; } cb = onVolume; }
  if (found && cb) cb(id, nv, m, true);
  return found;
}
bool MemoryLayer::setMute(const std::string& id, bool mu) {
  std::function<void(const std::string&, float, bool, bool)> cb; float v = 0; bool found = false;
  { std::lock_guard<std::mutex> g(m_); ++sets; for (Session& s : s_) if (s.id == id) { s.muted = mu; v = s.volume; found = true; } cb = onVolume; }
  if (found && cb) cb(id, v, mu, true);
  return found;
}
void MemoryLayer::add(const Session& s) { { std::lock_guard<std::mutex> g(m_); s_.push_back(s); } if (onListChanged) onListChanged(); }
void MemoryLayer::remove(const std::string& id) {
  { std::lock_guard<std::mutex> g(m_); s_.erase(std::remove_if(s_.begin(), s_.end(), [&](const Session& s) { return s.id == id; }), s_.end()); }
  if (onListChanged) onListChanged();
}
void MemoryLayer::poke(const std::string& id, float v, bool mu) {
  bool found = false;
  { std::lock_guard<std::mutex> g(m_); for (Session& s : s_) if (s.id == id) { s.volume = v; s.muted = mu; found = true; } }
  if (found && onVolume) onVolume(id, v, mu, false);
}

// ---- the list ----
void SessionList::sortSessions(std::vector<Session>& v) {
  std::stable_sort(v.begin(), v.end(), [](const Session& a, const Session& b) {
    if (a.system != b.system) return a.system;
    std::string x = a.name, y = b.name; for (char& c : x) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); for (char& c : y) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return x != y ? x < y : a.id < b.id; });
}

SessionList::Diff SessionList::update(std::vector<Session> fresh) {
  sortSessions(fresh);
  Diff d;
  for (const Session& n : fresh) { const Session* o = find(n.id); if (!o) d.added.push_back(n); else if (!(*o == n)) d.changed.push_back(n); }
  for (const Session& o : s_) if (std::none_of(fresh.begin(), fresh.end(), [&](const Session& n) { return n.id == o.id; })) { d.removed.push_back(o.id); pending_.erase(o.id); }
  s_ = std::move(fresh);
  return d;
}

const Session* SessionList::find(const std::string& id) const { for (const Session& s : s_) if (s.id == id) return &s; return nullptr; }

void SessionList::noteSet(const std::string& id, float v) {
  for (Session& s : s_) if (s.id == id) { s.volume = std::clamp(v, 0.f, 1.f); pending_[id].volume = s.volume; }
}
void SessionList::noteMute(const std::string& id, bool m) {
  for (Session& s : s_) if (s.id == id) { s.muted = m; pending_[id].mute = m ? 1 : 0; }
}

bool SessionList::report(const std::string& id, float v, bool m, bool ours) {
  Session* s = nullptr; for (Session& x : s_) if (x.id == id) s = &x;
  if (!s) return false;   // not listed yet: the list refresh brings it
  auto p = pending_.find(id);
  const bool volEcho = p != pending_.end() && p->second.volume >= 0 && std::fabs(p->second.volume - v) < 1e-4f;
  const bool muteEcho = p != pending_.end() && p->second.mute >= 0 && (p->second.mute == 1) == m;
  if (ours || (volEcho && (muteEcho || p->second.mute < 0) && m == s->muted)) {   // the console's own change coming back
    if (p != pending_.end()) { if (volEcho) p->second.volume = -1; if (muteEcho) p->second.mute = -1; if (p->second.volume < 0 && p->second.mute < 0) pending_.erase(p); }
    return false;
  }
  if (p != pending_.end()) pending_.erase(p);   // someone moved it in the Windows mixer: that wins
  if (std::fabs(s->volume - v) < 1e-4f && s->muted == m) return false;
  s->volume = v; s->muted = m; return true;
}

bool validVolumeRequest(const SessionList& list, const std::string& id, double percent, std::string& err) {
  if (id.empty() || id.size() > 512) { err = "id: a listed program"; return false; }
  if (!list.find(id)) { err = "no such program playing"; return false; }
  if (!std::isfinite(percent) || percent < 0 || percent > 100) { err = "volume: 0..100"; return false; }
  return true;
}

// ---- this computer ----
#ifndef _WIN32
namespace {
class NoLayer final : public SessionLayer {
 public:
  bool available() const override { return false; }
  std::vector<Session> list() override { return {}; }
  bool setVolume(const std::string&, float) override { return false; }
  bool setMute(const std::string&, bool) override { return false; }
};
}  // namespace
std::unique_ptr<SessionLayer> makeSystemLayer() { return std::make_unique<NoLayer>(); }
#else
namespace {
const CLSID kClsidEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
const IID kIidEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
const IID kIidManager2 = {0x77AA99A0, 0x1BD6, 0x484F, {0x8B, 0xC7, 0x2C, 0x65, 0x4C, 0x9A, 0x9B, 0x6F}};
const IID kIidControl2 = {0xBFB7FF88, 0x7239, 0x4FC9, {0x8F, 0xA2, 0x07, 0xC9, 0x50, 0xBE, 0x9C, 0x6D}};
const IID kIidSimpleVolume = {0x87CE5498, 0x68D6, 0x44E5, {0x92, 0x15, 0x6D, 0xA4, 0x7E, 0xF8, 0x83, 0xD8}};
const IID kIidSessionEvents = {0x24918ACC, 0x64B3, 0x37C1, {0x8C, 0xA9, 0x74, 0xA6, 0x6E, 0x99, 0x57, 0xA8}};
const IID kIidSessionNotification = {0x641DD20B, 0x4D41, 0x49CC, {0xAB, 0xA3, 0x17, 0x4B, 0x94, 0x77, 0xBB, 0x08}};
// the console's own changes carry this event context: their echo is recognised
const GUID kOurContext = {0x6a1f3c2e, 0x8d4b, 0x4e57, {0x9a, 0x21, 0x5c, 0x0e, 0x7b, 0x33, 0xd1, 0x42}};

std::string utf8(const wchar_t* w) {
  if (!w || !*w) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr); if (n <= 1) return {};
  std::string s(static_cast<std::size_t>(n - 1), '\0'); WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr); return s;
}
std::string programPath(DWORD pid) {
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid); if (!h) return {};
  wchar_t buf[MAX_PATH * 2]; DWORD n = MAX_PATH * 2; std::string out; if (QueryFullProcessImageNameW(h, 0, buf, &n)) out = utf8(buf);
  CloseHandle(h); return out;
}
std::string baseName(const std::string& path) {
  std::string b = path.substr(path.find_last_of("\\/") == std::string::npos ? 0 : path.find_last_of("\\/") + 1);
  if (b.size() > 4) { std::string e = b.substr(b.size() - 4); for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); if (e == ".exe") b.resize(b.size() - 4); }
  return b;
}

// what both callback objects report to: kept alive by them, cut off when the layer goes
struct Sink {
  std::mutex m; bool alive = true;
  std::function<void(const std::string&, float, bool, bool)> onVolume; std::function<void()> onList;
  void volume(const std::string& id, float v, bool mu, bool ours) { std::function<void(const std::string&, float, bool, bool)> f; { std::lock_guard<std::mutex> g(m); if (!alive) return; f = onVolume; } if (f) f(id, v, mu, ours); }
  void list() { std::function<void()> f; { std::lock_guard<std::mutex> g(m); if (!alive) return; f = onList; } if (f) f(); }
};

class Events final : public IAudioSessionEvents {
 public:
  Events(std::shared_ptr<Sink> s, std::string id) : sink_(std::move(s)), id_(std::move(id)) {}
  ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }
  ULONG STDMETHODCALLTYPE Release() override { const LONG r = InterlockedDecrement(&ref_); if (r == 0) delete this; return static_cast<ULONG>(r); }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** p) override {
    if (!p) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, kIidSessionEvents)) { *p = static_cast<IAudioSessionEvents*>(this); AddRef(); return S_OK; }
    *p = nullptr; return E_NOINTERFACE; }
  HRESULT STDMETHODCALLTYPE OnDisplayNameChanged(LPCWSTR, LPCGUID) override { sink_->list(); return S_OK; }
  HRESULT STDMETHODCALLTYPE OnIconPathChanged(LPCWSTR, LPCGUID) override { sink_->list(); return S_OK; }
  HRESULT STDMETHODCALLTYPE OnSimpleVolumeChanged(float v, BOOL mu, LPCGUID ctx) override { sink_->volume(id_, v, mu != FALSE, ctx && IsEqualGUID(*ctx, kOurContext)); return S_OK; }
  HRESULT STDMETHODCALLTYPE OnChannelVolumeChanged(DWORD, float*, DWORD, LPCGUID) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnGroupingParamChanged(LPCGUID, LPCGUID) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnStateChanged(AudioSessionState s) override { if (s == AudioSessionStateExpired) sink_->list(); return S_OK; }
  HRESULT STDMETHODCALLTYPE OnSessionDisconnected(AudioSessionDisconnectReason) override { sink_->list(); return S_OK; }
 private:
  ~Events() = default;
  LONG ref_ = 1; std::shared_ptr<Sink> sink_; std::string id_;
};

class Created final : public IAudioSessionNotification {
 public:
  explicit Created(std::shared_ptr<Sink> s) : sink_(std::move(s)) {}
  ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }
  ULONG STDMETHODCALLTYPE Release() override { const LONG r = InterlockedDecrement(&ref_); if (r == 0) delete this; return static_cast<ULONG>(r); }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** p) override {
    if (!p) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, kIidSessionNotification)) { *p = static_cast<IAudioSessionNotification*>(this); AddRef(); return S_OK; }
    *p = nullptr; return E_NOINTERFACE; }
  HRESULT STDMETHODCALLTYPE OnSessionCreated(IAudioSessionControl*) override { sink_->list(); return S_OK; }
 private:
  ~Created() = default;
  LONG ref_ = 1; std::shared_ptr<Sink> sink_;
};

// Every COM object lives on one worker thread of its own (multithreaded apartment): the console's interface thread is a single-threaded
// apartment, and the calls never cross apartments. Requests are queued to it; the callbacks above only forward (no COM call in them).
class WinLayer final : public SessionLayer {
 public:
  WinLayer() : sink_(std::make_shared<Sink>()) {
    th_ = std::thread([this] { run(); });
    ok_ = call<bool>([this] { return open(); });
  }
  ~WinLayer() override {
    { std::lock_guard<std::mutex> g(sink_->m); sink_->alive = false; }
    call<bool>([this] { close(); return true; });
    { std::lock_guard<std::mutex> g(qm_); stop_ = true; } qc_.notify_all(); if (th_.joinable()) th_.join();
  }
  bool available() const override { return ok_; }
  std::vector<Session> list() override { wire(); return call<std::vector<Session>>([this] { return enumerate(); }); }
  bool setVolume(const std::string& id, float v) override { return call<bool>([this, id, v] { Item* it = item(id); return it && it->vol && SUCCEEDED(it->vol->SetMasterVolume(std::clamp(v, 0.f, 1.f), &kOurContext)); }); }
  bool setMute(const std::string& id, bool m) override { return call<bool>([this, id, m] { Item* it = item(id); return it && it->vol && SUCCEEDED(it->vol->SetMute(m ? TRUE : FALSE, &kOurContext)); }); }

 private:
  struct Item { std::string id; IAudioSessionControl2* ctl = nullptr; ISimpleAudioVolume* vol = nullptr; Events* ev = nullptr; };
  void wire() { std::lock_guard<std::mutex> g(sink_->m); sink_->onVolume = onVolume; sink_->onList = onListChanged; }
  template <class T> T call(std::function<T()> f) {
    auto p = std::make_shared<std::promise<T>>(); std::future<T> r = p->get_future();
    { std::lock_guard<std::mutex> g(qm_); if (stop_) return T{}; q_.push_back([p, f] { p->set_value(f()); }); } qc_.notify_one();
    return r.get();
  }
  void run() {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (;;) { std::function<void()> f; { std::unique_lock<std::mutex> l(qm_); qc_.wait(l, [this] { return stop_ || !q_.empty(); }); if (q_.empty()) break; f = std::move(q_.front()); q_.pop_front(); } f(); }
    if (SUCCEEDED(co)) CoUninitialize();
  }
  bool open() {
    IMMDeviceEnumerator* en = nullptr; IMMDevice* d = nullptr;
    if (FAILED(CoCreateInstance(kClsidEnumerator, nullptr, CLSCTX_ALL, kIidEnumerator, reinterpret_cast<void**>(&en)))) return false;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &d))) {
      LPWSTR wid = nullptr; if (SUCCEEDED(d->GetId(&wid))) { device_ = utf8(wid); CoTaskMemFree(wid); }
      d->Activate(kIidManager2, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&mgr_)); d->Release(); }
    en->Release();
    if (!mgr_) return false;
    created_ = new Created(sink_);
    IAudioSessionEnumerator* se = nullptr; if (SUCCEEDED(mgr_->GetSessionEnumerator(&se))) se->Release();   // Windows sends OnSessionCreated only after one enumeration
    mgr_->RegisterSessionNotification(created_);
    return true;
  }
  void close() {
    for (Item& it : items_) { drop(it); }
    items_.clear();
    if (mgr_ && created_) mgr_->UnregisterSessionNotification(created_);
    if (created_) { created_->Release(); created_ = nullptr; }
    if (mgr_) { mgr_->Release(); mgr_ = nullptr; }
  }
  static void drop(Item& it) {
    if (it.ctl && it.ev) it.ctl->UnregisterAudioSessionNotification(it.ev);
    if (it.ev) { it.ev->Release(); }
    if (it.vol) { it.vol->Release(); }
    if (it.ctl) { it.ctl->Release(); }
    it = Item{};
  }
  Item* item(const std::string& id) { for (Item& it : items_) if (it.id == id) return &it; return nullptr; }
  std::string defaultDevice() {
    IMMDeviceEnumerator* en = nullptr; IMMDevice* d = nullptr; std::string id;
    if (FAILED(CoCreateInstance(kClsidEnumerator, nullptr, CLSCTX_ALL, kIidEnumerator, reinterpret_cast<void**>(&en)))) return id;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &d))) { LPWSTR w = nullptr; if (SUCCEEDED(d->GetId(&w))) { id = utf8(w); CoTaskMemFree(w); } d->Release(); }
    en->Release(); return id;
  }
  std::vector<Session> enumerate() {
    if (mgr_ && defaultDevice() != device_) { close(); ok_ = open(); }   // the default output changed: its sessions instead
    std::vector<Session> out; if (!mgr_) return out;
    IAudioSessionEnumerator* se = nullptr; if (FAILED(mgr_->GetSessionEnumerator(&se))) return out;
    int n = 0; se->GetCount(&n);
    std::vector<Item> keep;
    for (int i = 0; i < n && i < 256; ++i) {
      IAudioSessionControl* c = nullptr; if (FAILED(se->GetSession(i, &c)) || !c) continue;
      IAudioSessionControl2* c2 = nullptr; c->QueryInterface(kIidControl2, reinterpret_cast<void**>(&c2)); c->Release(); if (!c2) continue;
      AudioSessionState st = AudioSessionStateInactive; c2->GetState(&st);
      LPWSTR wid = nullptr; std::string id; if (SUCCEEDED(c2->GetSessionInstanceIdentifier(&wid))) { id = utf8(wid); CoTaskMemFree(wid); }
      if (st == AudioSessionStateExpired || id.empty()) { c2->Release(); continue; }
      Session s; s.id = id; DWORD pid = 0; c2->GetProcessId(&pid); s.pid = pid; s.system = c2->IsSystemSoundsSession() == S_OK;
      LPWSTR w = nullptr; if (SUCCEEDED(c2->GetDisplayName(&w))) { s.name = utf8(w); CoTaskMemFree(w); }
      w = nullptr; if (SUCCEEDED(c2->GetIconPath(&w))) { s.icon = utf8(w); CoTaskMemFree(w); }
      const std::string path = s.system ? std::string() : programPath(pid);
      if (s.name.empty() || s.name[0] == '@') s.name = s.system ? "System sounds" : baseName(path);
      if (s.name.empty()) s.name = "Program " + std::to_string(pid);
      if (s.icon.empty() || s.icon[0] == '@') s.icon = path;
      Item it;
      if (Item* old = item(id)) { c2->Release(); it = *old; *old = Item{}; }   // known: its control and its events stay as they are
      else { it.id = id; it.ctl = c2; c2->QueryInterface(kIidSimpleVolume, reinterpret_cast<void**>(&it.vol)); it.ev = new Events(sink_, id); it.ctl->RegisterAudioSessionNotification(it.ev); }
      if (it.vol) { float v = 1; BOOL m = FALSE; it.vol->GetMasterVolume(&v); it.vol->GetMute(&m); s.volume = v; s.muted = m != FALSE; }
      keep.push_back(it); out.push_back(std::move(s));
    }
    se->Release();
    for (Item& it : items_) drop(it);   // the sessions gone
    items_ = std::move(keep);
    return out;
  }

  std::shared_ptr<Sink> sink_; std::thread th_; std::mutex qm_; std::condition_variable qc_; std::deque<std::function<void()>> q_; bool stop_ = false;
  std::atomic<bool> ok_{false}; IAudioSessionManager2* mgr_ = nullptr; Created* created_ = nullptr; std::vector<Item> items_; std::string device_;
};
}  // namespace
std::unique_ptr<SessionLayer> makeSystemLayer() { return std::make_unique<WinLayer>(); }
#endif

}  // namespace am::vol
