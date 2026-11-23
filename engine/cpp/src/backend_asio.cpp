// ASIO backend (Windows): the mixer runs on any installed ASIO driver, through the Steinberg ASIO SDK 2.3 interface (IASIO, drivers/asio/sdk, GPLv3).
// The driver is chosen by name (DeviceConfig::outDevice, else inDevice; a part of the name is enough, "" = the first registered driver). The inputs of the driver feed the
// strips, the stereo master goes to output channels 0 and 1, the block size is the driver's buffer size nearest to DeviceConfig::frames. ASIO drivers are single-client: a second
// open() while one is running fails. Sample types: Int16 / Int24 / Int32 / Float32 / Float64, little endian (what Windows drivers use).
// Host duties a real driver expects (ASIO4ALL, FlexASIO, the interfaces' own drivers): time-info buffer switches (kAsioSupportsTimeInfo), and the
// driver's requests to reset / resync / change the buffer size (kAsioResetRequest ...), which a host must answer 1 and then act on OUTSIDE the
// callback: a worker thread stops the stream, disposes the buffers, re-queries the buffer size and the channel types, creates the buffers again
// and restarts, without the Runtime noticing (the callback stays; a changed buffer size is accepted up to the 8 periods the Runtime holds scratch for). Channel counts the driver lacks are filled with silence
// instead of refusing the driver (a USB DAC has no inputs). Cross-compiled with mingw-w64 (-Werror) in the tests; run against real drivers only
// on a Windows PC.
#ifdef _WIN32
#include "audiomixer/sample_format.hpp"
#include "asio_sdk.h"
#include <shlobj.h>
#include <softpub.h>
#include <wintrust.h>
#include "audiomixer/sysopt.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "audiomixer/asio.hpp"
#include "backends.hpp"

namespace am {
// the text Windows gives for an HRESULT (CoCreateInstance, driver load), so a failure says why instead of a bare code
std::string hresultText(HRESULT r) {
  char* buf = nullptr; const DWORD n = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, static_cast<DWORD>(r), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<char*>(&buf), 0, nullptr);
  std::string t = n && buf ? std::string(buf, n) : std::string(); if (buf) LocalFree(buf); while (!t.empty() && (t.back() == '\r' || t.back() == '\n' || t.back() == ' ' || t.back() == '.')) t.pop_back();
  char b[48]; std::snprintf(b, sizeof b, " (HRESULT 0x%08lx)", static_cast<unsigned long>(r)); return (t.empty() ? std::string("unknown error") : t) + b;
}
// ---------------------------------------------------------------------------------------------------------------------------------
// SECURE LOADING (asio_trust.hpp): the registry, the system folders and the signature check as the policy sees them
namespace {
std::wstring widen(const std::string& s) { if (s.empty()) return std::wstring(); const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0); std::wstring w(static_cast<std::size_t>(std::max(0, n)), L'\0'); if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n); return w; }
std::string narrow(const wchar_t* w, std::size_t len) { if (!len) return std::string(); const int n = WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len), nullptr, 0, nullptr, nullptr); std::string s(static_cast<std::size_t>(std::max(0, n)), '\0'); if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len), &s[0], n, nullptr, nullptr); return s; }
// a string value, bounded and always terminated; type REG_SZ or REG_EXPAND_SZ only
bool regString(HKEY k, const wchar_t* value, std::string& out, bool* expand) {
  wchar_t buf[1024]; DWORD type = 0, bytes = sizeof buf - sizeof(wchar_t);
  if (RegQueryValueExW(k, value, nullptr, &type, reinterpret_cast<BYTE*>(buf), &bytes) != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return false;
  std::size_t n = bytes / sizeof(wchar_t); buf[std::min<std::size_t>(n, 1023)] = 0; n = wcsnlen(buf, 1023);
  out = narrow(buf, n); if (expand) *expand = type == REG_EXPAND_SZ; return true;
}
std::string knownFolder(REFKNOWNFOLDERID id) { PWSTR p = nullptr; std::string r; if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY, nullptr, &p)) && p) r = narrow(p, wcslen(p)); if (p) CoTaskMemFree(p); return r; }
// the system folders from the system itself (never from this process's environment, which whoever started it could have changed)
bool systemVar(const std::string& name, std::string& v) {
  if (name == "systemroot" || name == "windir" || name == "systemdrive") { wchar_t w[MAX_PATH] = {0}; const UINT n = GetSystemWindowsDirectoryW(w, MAX_PATH); if (!n || n >= MAX_PATH) return false; v = narrow(w, n); if (name == "systemdrive") v = v.substr(0, 2); return !v.empty(); }
  if (name == "programfiles") v = knownFolder(FOLDERID_ProgramFiles);
  else if (name == "programfiles(x86)") v = knownFolder(FOLDERID_ProgramFilesX86);
  else if (name == "programw6432") v = knownFolder(FOLDERID_ProgramFilesX64);
  else if (name == "commonprogramfiles") v = knownFolder(FOLDERID_ProgramFilesCommon);
  else if (name == "commonprogramfiles(x86)") v = knownFolder(FOLDERID_ProgramFilesCommonX86);
  else if (name == "commonprogramw6432") v = knownFolder(FOLDERID_ProgramFilesCommonX64);
  else return false;
  return !v.empty();
}
// Authenticode, offline: no revocation lookups, URL retrieval from the cache only (a check never waits on the network), no UI. Cached per
// path + size + time, so a rescan does not verify every DLL again.
asiotrust::Sig verifySignature(const std::string& path) {
  static std::mutex m; static std::map<std::string, asiotrust::Sig> cache;
  const std::wstring w = widen(path);
  WIN32_FILE_ATTRIBUTE_DATA fa{}; if (!GetFileAttributesExW(w.c_str(), GetFileExInfoStandard, &fa)) return asiotrust::Sig::Unknown;
  char key[64]; std::snprintf(key, sizeof key, "|%lu|%lu|%lu", fa.nFileSizeLow, fa.ftLastWriteTime.dwLowDateTime, fa.ftLastWriteTime.dwHighDateTime);
  { std::lock_guard<std::mutex> g(m); auto it = cache.find(path + key); if (it != cache.end()) return it->second; }
  WINTRUST_FILE_INFO file{}; file.cbStruct = sizeof file; file.pcwszFilePath = w.c_str();
  WINTRUST_DATA data{}; data.cbStruct = sizeof data; data.dwUIChoice = WTD_UI_NONE; data.fdwRevocationChecks = WTD_REVOKE_NONE; data.dwUnionChoice = WTD_CHOICE_FILE; data.pFile = &file;
  data.dwStateAction = WTD_STATEACTION_VERIFY; data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL | WTD_REVOCATION_CHECK_NONE;
  GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
  const LONG r = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
  data.dwStateAction = WTD_STATEACTION_CLOSE; WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
  const asiotrust::Sig sig = r == ERROR_SUCCESS ? asiotrust::Sig::Valid
                           : (r == static_cast<LONG>(TRUST_E_NOSIGNATURE) || r == static_cast<LONG>(TRUST_E_SUBJECT_FORM_UNKNOWN) || r == static_cast<LONG>(TRUST_E_PROVIDER_UNKNOWN)) ? asiotrust::Sig::Unsigned : asiotrust::Sig::Invalid;
  std::lock_guard<std::mutex> g(m); cache[path + key] = sig; return sig;
}
struct WinEnv : asiotrust::Env {
  std::vector<asiotrust::RegEntry> entries(int bits) override {
    std::vector<asiotrust::RegEntry> out; const REGSAM view = bits == 64 ? KEY_WOW64_64KEY : KEY_WOW64_32KEY;
    HKEY root = nullptr; if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO", 0, KEY_READ | view, &root) != ERROR_SUCCESS) return out;
    wchar_t name[256];
    for (DWORD i = 0, n = 255; i < 512 && RegEnumKeyExW(root, i, name, &n, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS; ++i, n = 255) {
      HKEY k = nullptr; if (RegOpenKeyExW(root, name, 0, KEY_READ | view, &k) != ERROR_SUCCESS) continue;
      asiotrust::RegEntry e; e.bits = bits; e.name = narrow(name, wcsnlen(name, 255));
      if (regString(k, L"CLSID", e.clsid, nullptr) && asiotrust::validClsid(e.clsid)) {
        // machine-wide only: HKLM\SOFTWARE\Classes (HKCR would merge in HKCU, which the user's own processes can write)
        const std::wstring sub = L"SOFTWARE\\Classes\\CLSID\\" + widen(e.clsid) + L"\\InprocServer32"; HKEY ip = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, sub.c_str(), 0, KEY_READ | view, &ip) == ERROR_SUCCESS) { regString(ip, nullptr, e.inproc, &e.expandSz); RegCloseKey(ip); }
      }
      out.push_back(e); RegCloseKey(k);
    }
    RegCloseKey(root); return out;
  }
  bool getVar(const std::string& n, std::string& v) override { return systemVar(n, v); }
  bool fileExists(const std::string& p) override { const DWORD a = GetFileAttributesW(widen(p).c_str()); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
  asiotrust::Sig signature(const std::string& p) override { return verifySignature(p); }
};
std::vector<asiotrust::Driver> trustedDrivers(std::vector<asiotrust::Rejected>* rej = nullptr) { WinEnv env; return asiotrust::enumerate(env, rej); }

// CRASH ISOLATION: a driver call that faults (access violation, illegal instruction ...) comes back as "false" instead of ending the
// program. MSVC: structured exception handling. mingw-w64 has no __try: a vectored exception handler jumps back to the guarded call
// on the same thread (__builtin_setjmp / __builtin_longjmp, no unwinding through the driver's frames).
bool fatalCode(DWORD c) { return c == EXCEPTION_ACCESS_VIOLATION || c == EXCEPTION_ILLEGAL_INSTRUCTION || c == EXCEPTION_PRIV_INSTRUCTION || c == EXCEPTION_INT_DIVIDE_BY_ZERO || c == EXCEPTION_IN_PAGE_ERROR || c == EXCEPTION_ARRAY_BOUNDS_EXCEEDED || c == EXCEPTION_DATATYPE_MISALIGNMENT || c == EXCEPTION_INT_OVERFLOW; }
#if defined(_MSC_VER)
template <class F> bool sehCall(F& f) { __try { f(); return true; } __except (fatalCode(GetExceptionCode()) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return false; } }
#else
thread_local void* t_jmp[5]; thread_local bool t_armed = false;
LONG CALLBACK guardVeh(EXCEPTION_POINTERS* ep) {
  if (!t_armed || !ep || !ep->ExceptionRecord || !fatalCode(ep->ExceptionRecord->ExceptionCode)) return EXCEPTION_CONTINUE_SEARCH;
  t_armed = false; __builtin_longjmp(t_jmp, 1);
}
template <class F> bool __attribute__((noinline)) sehCall(F& f) {
  static std::once_flag once; std::call_once(once, [] { AddVectoredExceptionHandler(1, guardVeh); });
  if (t_armed) { f(); return true; }   // already inside a guarded call on this thread: that one catches it
  if (__builtin_setjmp(t_jmp)) return false;
  t_armed = true; f(); t_armed = false; return true;
}
#endif
// a guarded call that must also come back in time (load + init, createBuffers): run on a helper thread. A driver that does not answer
// in `ms` is abandoned (its thread and object are left alone, never touched again) and marked faulted.
template <class F> bool timedCall(F f, int ms) {
  struct St { std::mutex m; std::condition_variable cv; bool done = false, ok = false; };
  auto st = std::make_shared<St>();
  std::thread([st, f]() mutable { CoInitializeEx(nullptr, COINIT_MULTITHREADED); const bool ok = sehCall(f); { std::lock_guard<std::mutex> g(st->m); st->done = true; st->ok = ok; } st->cv.notify_all(); }).detach();
  std::unique_lock<std::mutex> lk(st->m);
  if (!st->cv.wait_for(lk, std::chrono::milliseconds(ms), [&] { return st->done; })) return false;
  return st->ok;
}
constexpr int kInitTimeoutMs = 10000, kBuffersTimeoutMs = 8000;
using GetClassObject = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
// load the verified DLL from exactly its path and make the driver object through its own class factory (never the COM lookup)
IASIO* createFromVerifiedDll(const asiotrust::Driver& d, std::string& err) {
  const std::wstring wp = widen(d.path);
  HMODULE mod = LoadLibraryExW(wp.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);   // the driver's own folder and System32 only
  if (!mod) { err = "the driver DLL did not load: " + hresultText(HRESULT_FROM_WIN32(GetLastError())); return nullptr; }
  const auto get = reinterpret_cast<GetClassObject>(reinterpret_cast<void (*)(void)>(GetProcAddress(mod, "DllGetClassObject")));
  if (!get) { err = "the DLL is not a COM driver (no DllGetClassObject)"; return nullptr; }   // the module stays loaded: never unloaded under a driver
  wchar_t wide[64] = {0}; MultiByteToWideChar(CP_UTF8, 0, d.clsid.c_str(), -1, wide, 63);
  CLSID clsid; if (FAILED(CLSIDFromString(wide, &clsid))) { err = "the CLSID is not valid"; return nullptr; }
  IClassFactory* cf = nullptr; HRESULT r = get(clsid, IID_IClassFactory, reinterpret_cast<void**>(&cf));
  if (FAILED(r) || !cf) { err = "cannot load the driver: " + hresultText(FAILED(r) ? r : E_POINTER); return nullptr; }
  IASIO* drv = nullptr; r = cf->CreateInstance(nullptr, clsid, reinterpret_cast<void**>(&drv)); cf->Release();   // ASIO drivers use their CLSID as the interface id too
  if (FAILED(r) || !drv) { err = "cannot load the driver: " + hresultText(FAILED(r) ? r : E_POINTER); return nullptr; }
  return drv;
}
// load + init, guarded and timed. On a fault or a timeout the driver is marked faulted. The driver's error text is sanitised.
IASIO* loadAndInit(const asiotrust::Driver& d, std::string& err) {
  struct Out { IASIO* drv = nullptr; std::string err; bool inited = false; char msg[128] = {0}; };
  auto o = std::make_shared<Out>(); const asiotrust::Driver copy = d;
  const bool ok = timedCall([o, copy] { o->drv = createFromVerifiedDll(copy, o->err); if (!o->drv) return; o->inited = o->drv->init(GetDesktopWindow()) != 0; if (!o->inited) o->drv->getErrorMessage(o->msg); }, kInitTimeoutMs);
  if (!ok) { asiotrust::markFaulted(d.clsid, "load / init crashed or did not answer"); err = "the driver crashed or did not answer while starting: marked faulted"; return nullptr; }
  if (!o->drv) { err = o->err; return nullptr; }
  if (!o->inited) { err = "did not start: " + asiotrust::sanitizeText(o->msg, sizeof o->msg, asiotrust::kMaxErrorText, "init failed"); IASIO* dr = o->drv; auto rel = [dr] { dr->Release(); }; sehCall(rel); return nullptr; }
  return o->drv;
}
}  // namespace

std::vector<AsioDriverInfo> listAsioDrivers() {
  std::vector<AsioDriverInfo> out;
  for (const asiotrust::Driver& d : trustedDrivers()) { AsioDriverInfo i; i.name = d.name; i.clsid = d.clsid; i.bits = d.bits; i.path = d.path; i.sig = d.sig; i.faulted = asiotrust::isFaulted(d.clsid); out.push_back(i); }
  return out;
}
std::vector<asiotrust::Rejected> rejectedAsioDrivers() { std::vector<asiotrust::Rejected> r; trustedDrivers(&r); return r; }
bool asioSelectionFaulted(const std::string& want) { const auto list = trustedDrivers(); const asiotrust::Driver* d = asiotrust::select(list, want); return d && asiotrust::isFaulted(d->clsid); }

// Reads one driver's real channel counts and accepted sample rates through the Steinberg interface (no RtAudio). The driver is loaded, asked, and
// released again. ASIO drivers are single-client: never probe one while a stream of this program or the bridge has it open.
// The CLSID is looked up in the registry again: a path or a CLSID that is not registered (trusted) is never loaded.
bool asioStreamOpen();   // defined with the backend below
AsioProbeInfo probeAsioDriver(const AsioDriverInfo& want) {
  AsioProbeInfo info; info.name = want.name; info.clsid = want.clsid;
  const auto list = trustedDrivers(); const asiotrust::Driver* dp = nullptr;
  for (const auto& x : list) if (x.clsid == want.clsid) dp = &x;
  if (!dp) { info.error = "not a registered, trusted ASIO driver"; return info; }
  const asiotrust::Driver d = *dp;
  std::string why; if (!asiotrust::loadAllowed(d, &why)) { info.error = why; return info; }
  if (asioStreamOpen()) { info.error = "in use by this program"; return info; }   // single-client drivers: a second instance made for a look could tear down the stream that is playing
  if (d.bits != static_cast<int>(sizeof(void*) * 8)) { info.error = d.bits == 32 ? "a 32-bit driver: only the 32-bit AmF X1 can load it" : "a 64-bit driver: only the 64-bit AmF X1 can load it"; return info; }
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);   // no-op when the thread already has COM
  IASIO* drv = loadAndInit(d, info.error); if (!drv) return info;
  struct Q { long nIn = 0, nOut = 0; bool chOk = false; std::vector<double> rates; asiotrust::BufferSizes b; bool bufOk = false; } q;
  auto ask = [&q, drv] {
    q.chOk = drv->getChannels(&q.nIn, &q.nOut) == ASE_OK; if (!q.chOk) return;
    static const double kRates[] = {8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000, 705600, 768000};
    for (double rate : kRates) if (drv->canSampleRate(rate) == ASE_OK) q.rates.push_back(rate);
    q.bufOk = drv->getBufferSize(&q.b.mn, &q.b.mx, &q.b.pref, &q.b.gran) == ASE_OK; };
  if (!sehCall(ask)) { asiotrust::markFaulted(d.clsid, "crashed while it was read"); info.error = "the driver crashed while it was read: marked faulted"; return info; }   // the object is abandoned, never touched again
  if (!q.chOk) info.error = "getChannels failed";
  else { info.inChannels = asiotrust::clampChannels(q.nIn); info.outChannels = asiotrust::clampChannels(q.nOut); info.sampleRates = q.rates;
         if (q.bufOk && asiotrust::validateBuffers(q.b)) { info.bufMin = q.b.mn; info.bufMax = q.b.mx; info.bufPreferred = q.b.pref; info.bufGranularity = q.b.gran; } }
  auto rel = [drv] { drv->Release(); }; if (!sehCall(rel)) asiotrust::markFaulted(d.clsid, "crashed when released");
  return info;
}

namespace {
std::string lower(std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
std::string msg(const char* what, long e) { char b[96]; std::snprintf(b, sizeof b, "%s failed (ASIO error %ld)", what, e); return b; }

class AsioBackend;
ASIOCallbacks g_callbacks{};                     // static: a driver abandoned after a timeout may still hold this pointer
std::atomic<AsioBackend*> g_active{nullptr};     // the ASIO callbacks carry no user pointer: one driver at a time, like ASIO itself

class AsioBackend : public Backend {
 public:
  ~AsioBackend() override { stop(); closeAll(); }
  const char* name() const override { return "asio"; }

  bool open(const DeviceConfig& cfg, AudioCallback cb, std::string& err) override {
    cfg_ = cfg; cb_ = std::move(cb);
    { std::lock_guard<std::mutex> g(wm_); quit_ = false; resetPending_ = false; }   // a backend may be opened again after close
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);            // already initialised by the host: fine
    AsioBackend* none = nullptr;
    if (!g_active.compare_exchange_strong(none, this)) { err = "ASIO allows one driver at a time: another one is already open"; return false; }
    mine_ = true;
    // SECURE SELECTION: a registered, trusted driver chosen by its NAME (or registered CLSID); a path from a settings file, the remote or
    // the command line selects nothing (asio_trust.hpp)
    const auto drivers = trustedDrivers();
    if (drivers.empty()) { err = "no trusted ASIO driver is installed (HKLM\\SOFTWARE\\ASIO lists none in System32 / Program Files)"; closeAll(); return false; }
    std::string want = cfg.outDevice != "-" && !cfg.outDevice.empty() ? cfg.outDevice : (cfg.inDevice != "-" ? cfg.inDevice : std::string());
    std::string why; const asiotrust::Driver* pick = asiotrust::select(drivers, want, &why);
    if (!pick) { err = "no ASIO driver named \"" + asiotrust::sanitizeText(want, asiotrust::kMaxDriverName) + "\": " + why; closeAll(); return false; }
    if (pick->bits != static_cast<int>(sizeof(void*) * 8)) { err = "\"" + pick->name + "\" is a " + std::to_string(pick->bits) + "-bit driver: only the " + std::to_string(pick->bits) + "-bit AmF X1 can load it"; closeAll(); return false; }
    if (!asiotrust::loadAllowed(*pick, &why)) { err = "\"" + pick->name + "\": " + why; closeAll(); return false; }
    driverName_ = pick->name; clsid_ = pick->clsid;
    { std::string e; drv_ = loadAndInit(*pick, e); if (!drv_) { err = "the ASIO driver \"" + pick->name + "\" " + e; closeAll(); return false; } }

    long nIn = 0, nOut = 0; ASIOError a = ASE_OK;
    if (!safe([&] { a = drv_->getChannels(&nIn, &nOut); }, "getChannels")) { err = faultErr(); closeAll(); return false; }
    if (a != ASE_OK) { err = msg("getChannels", a); closeAll(); return false; }
    nIn = asiotrust::clampChannels(nIn); nOut = asiotrust::clampChannels(nOut);   // a driver's numbers are checked before they size anything
    // what the driver lacks is filled with silence (a USB DAC has no inputs, an ADC no outputs): never a reason to refuse the driver
    nIn_ = std::min<long>(std::max(0, cfg.inChannels), nIn); nOut_ = std::min<long>(std::max(0, cfg.outChannels), nOut);
    if (nIn_ + nOut_ == 0) { char b[160]; std::snprintf(b, sizeof b, "\"%s\" has %ld inputs and %ld outputs: nothing to open", pick->name.c_str(), nIn, nOut); err = b; closeAll(); return false; }
    if (!safe([&] { a = drv_->canSampleRate(cfg.sampleRate); }, "canSampleRate")) { err = faultErr(); closeAll(); return false; }
    if (a != ASE_OK) { char b[128]; std::snprintf(b, sizeof b, "\"%s\" does not run at %.0f Hz", pick->name.c_str(), cfg.sampleRate); err = b; closeAll(); return false; }
    if (!safe([&] { a = drv_->setSampleRate(cfg.sampleRate); }, "setSampleRate")) { err = faultErr(); closeAll(); return false; }
    if (a != ASE_OK) {   // an externally clocked interface refuses to be set but may already run at that rate
      ASIOSampleRate now = 0; ASIOError g = ASE_OK; if (!safe([&] { g = drv_->getSampleRate(&now); }, "getSampleRate")) { err = faultErr(); closeAll(); return false; }
      if (g != ASE_OK || !asiotrust::validRate(now) || std::fabs(now - cfg.sampleRate) > 0.5) { err = msg("setSampleRate", a); closeAll(); return false; }
    }

    asiotrust::BufferSizes bs;
    if (!safe([&] { a = drv_->getBufferSize(&bs.mn, &bs.mx, &bs.pref, &bs.gran); }, "getBufferSize")) { err = faultErr(); closeAll(); return false; }
    if (a != ASE_OK) { err = msg("getBufferSize", a); closeAll(); return false; }
    if (!asiotrust::validateBuffers(bs)) { char b[160]; std::snprintf(b, sizeof b, "\"%s\" reported impossible buffer sizes (min %ld, max %ld)", pick->name.c_str(), bs.mn, bs.mx); err = b; closeAll(); return false; }
    frames_ = chooseBuffer(static_cast<long>(cfg.frames), bs.mn, bs.mx, bs.pref, bs.gran);
    if (frames_ > static_cast<long>(std::max<unsigned>(cfg.frames, 16)) * 8) { err = "the smallest ASIO buffer of this driver is larger than 8 periods of the requested block"; closeAll(); return false; }
    if (!createBuffers(err)) { closeAll(); return false; }
    worker_ = std::thread([this] { workerLoop(); });
    return true;
  }
  // CRASH ISOLATION for every call into the driver: a fault marks it faulted, the object is abandoned (never touched again, not even
  // released) and the stream is closed; the runtime then falls back to WASAPI
  template <class F> bool safe(F f, const char* what) {
    if (!drv_) return false;
    if (sehCall(f)) return true;
    asiotrust::markFaulted(clsid_, std::string("crashed in ") + what); faulted_ = true; drv_ = nullptr; buffers_ = false; running_ = false; return false;
  }
  std::string faultErr() const { return "the ASIO driver \"" + driverName_ + "\" crashed (" + asiotrust::faultText(clsid_) + "): marked faulted for this run"; }
  // the buffers and channel types for the current frames_ (open, and again after a driver reset)
  bool createBuffers(std::string& err) {
    infos_.assign(static_cast<std::size_t>(nIn_ + nOut_), ASIOBufferInfo());
    for (long i = 0; i < nIn_; ++i) { infos_[static_cast<std::size_t>(i)].isInput = ASIOTrue; infos_[static_cast<std::size_t>(i)].channelNum = i; }
    for (long i = 0; i < nOut_; ++i) { infos_[static_cast<std::size_t>(nIn_ + i)].isInput = ASIOFalse; infos_[static_cast<std::size_t>(nIn_ + i)].channelNum = i; }
    g_callbacks.bufferSwitch = &AsioBackend::onSwitch; g_callbacks.sampleRateDidChange = &AsioBackend::onRate; g_callbacks.asioMessage = &AsioBackend::onMessage; g_callbacks.bufferSwitchTimeInfo = &AsioBackend::onSwitchTime;
    // timed and guarded on a helper thread, into a copy it owns: a driver that never returns can only ever write into that copy
    struct Cb { std::vector<ASIOBufferInfo> infos; ASIOError a = ASE_OK; }; auto cb = std::make_shared<Cb>(); cb->infos = infos_;
    IASIO* d = drv_; const long nb = nIn_ + nOut_, fr = frames_;
    if (!timedCall([cb, d, nb, fr] { cb->a = d->createBuffers(cb->infos.data(), nb, fr, &g_callbacks); }, kBuffersTimeoutMs)) {
      asiotrust::markFaulted(clsid_, "createBuffers crashed or did not answer"); faulted_ = true; drv_ = nullptr; err = faultErr(); return false; }
    ASIOError a = cb->a;
    if (a != ASE_OK) { err = msg("createBuffers", a); return false; }
    infos_ = cb->infos;
    for (long i = 0; i < nb; ++i) if (!infos_[static_cast<std::size_t>(i)].buffers[0] || !infos_[static_cast<std::size_t>(i)].buffers[1]) { err = "the driver returned no buffer memory"; buffers_ = true; return false; }
    buffers_ = true;

    std::unique_lock<std::mutex> names(nm_);   // sampleFormat() / outputNames() read these from the control thread while a reset rebuilds them here
    types_.assign(infos_.size(), 0); outNames_.clear();
    for (std::size_t i = 0; i < infos_.size(); ++i) {
      ASIOChannelInfo ci; std::memset(&ci, 0, sizeof ci); ci.channel = infos_[i].channelNum; ci.isInput = infos_[i].isInput;
      if (!safe([&] { a = drv_->getChannelInfo(&ci); }, "getChannelInfo")) { names.unlock(); err = faultErr(); return false; }
      if (a != ASE_OK) { err = msg("getChannelInfo", a); return false; }
      if (!supported(ci.type)) { char b[128]; std::snprintf(b, sizeof b, "\"%s\" uses the sample type %ld, which this backend does not convert", driverName_.c_str(), static_cast<long>(ci.type)); err = b; return false; }
      types_[i] = ci.type; if (!ci.isInput) outNames_.push_back(asiotrust::sanitizeText(ci.name, sizeof ci.name, asiotrust::kMaxChannelName));
    }
    names.unlock();
    const std::size_t n = static_cast<std::size_t>(frames_);
    inBuf_.assign(static_cast<std::size_t>(std::max<long>(1, nIn_)), std::vector<float>(n, 0.0f)); outBuf_.assign(static_cast<std::size_t>(std::max<long>(1, nOut_)), std::vector<float>(n, 0.0f));
    inp_.clear(); outp_.clear();
    for (long i = 0; i < nIn_; ++i) inp_.push_back(inBuf_[static_cast<std::size_t>(i)].data());
    for (long i = 0; i < nOut_; ++i) outp_.push_back(outBuf_[static_cast<std::size_t>(i)].data());
    // the runtime hands out cfg.inChannels / cfg.outChannels pointers: fill what the driver does not have with silence
    zeros_.assign(n, 0.0f); sink_.assign(n, 0.0f);
    while (static_cast<int>(inp_.size()) < cfg_.inChannels) inp_.push_back(zeros_.data());
    while (static_cast<int>(outp_.size()) < cfg_.outChannels) outp_.push_back(sink_.data());
    long inLat = 0, outLat = 0; ASIOError la = ASE_NotPresent; if (!safe([&] { la = drv_->getLatencies(&inLat, &outLat); }, "getLatencies")) { err = faultErr(); return false; }
    if (la == ASE_OK) { inLatency_ = asiotrust::clampLatency(inLat); outLatency_ = asiotrust::clampLatency(outLat); }
    return true;
  }
  // a driver asked for a reset (its settings changed, the device was re-plugged, ASIO4ALL's panel was used): done here, never in the callback
  void workerLoop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);   // the reset worker talks to the driver object from its own thread
    struct Uninit { ~Uninit() { CoUninitialize(); } } uninit;
    std::unique_lock<std::mutex> lk(wm_);
    for (;;) {
      wcv_.wait(lk, [this] { return quit_ || resetPending_; });
      if (quit_) return;
      resetPending_ = false; lk.unlock();
      std::unique_lock<std::mutex> drvLock(dm_);   // stop() waits here: it can no longer slip in between "stopped for the reset" and "started again"
      const bool wasRunning = running_.exchange(false);
      while (inCallback_.load()) std::this_thread::yield();   // a bufferSwitch that started before running_ went false finishes first
      if (drv_) {
        safe([&] { drv_->stop(); }, "stop");
        if (drv_ && buffers_) { buffers_ = false; safe([&] { drv_->disposeBuffers(); }, "disposeBuffers"); }
        asiotrust::BufferSizes bs; ASIOError a = ASE_NotPresent;
        if (safe([&] { a = drv_->getBufferSize(&bs.mn, &bs.mx, &bs.pref, &bs.gran); }, "getBufferSize") && a == ASE_OK && asiotrust::validateBuffers(bs)) { const long f = chooseBuffer(frames_, bs.mn, bs.mx, bs.pref, bs.gran); if (f == frames_ || f <= static_cast<long>(std::max<unsigned>(cfg_.frames, 16)) * 8) frames_ = f; }
        std::string err; if (drv_ && createBuffers(err) && wasRunning && wantRun_.load()) { tuned_ = false; running_ = true; ASIOError st = ASE_OK; if (!safe([&] { st = drv_->start(); }, "start") || st != ASE_OK) running_ = false; }
        ++resets_;
      }
      drvLock.unlock();
      lk.lock();
    }
  }
  void requestReset() { { std::lock_guard<std::mutex> g(wm_); resetPending_ = true; } wcv_.notify_one(); }
  bool start(std::string& err) override {
    if (!drv_) { err = "open() first"; return false; }
    am::sysopt::prepareAudioTuning(); tuned_ = false;   // the driver's own thread calls block(): it is tuned on its first call
    std::lock_guard<std::mutex> g(dm_); wantRun_ = true;
    running_ = true; ASIOError a = ASE_OK;
    if (!safe([&] { a = drv_->start(); }, "start")) { wantRun_ = false; err = faultErr(); return false; }
    if (a != ASE_OK) { running_ = false; wantRun_ = false; err = msg("start", a); return false; }
    return true;
  }
  // STOP ALWAYS STOPS: a driver reset in progress had already cleared running_, so this used to return without stopping and the worker
  // then started the stream again, into a mixer that was being destroyed. Now the wish to run is cleared first, the reset (if any) is
  // waited for, and the driver is stopped whatever state the reset left it in.
  void stop() override {
    if (!drv_) return; wantRun_ = false;
    std::lock_guard<std::mutex> g(dm_);
    const bool was = running_.exchange(false); while (inCallback_.load()) std::this_thread::yield();
    if (drv_ && (was || buffers_)) safe([&] { drv_->stop(); }, "stop");
  }
  std::uint64_t xruns() const override { return xruns_; }
  const std::string& driverName() const { return driverName_; }
  void latencyFrames(long& in, long& out) const override { in = inLatency_; out = outLatency_; }   // ASIO reports them with the buffer included
  long inputLatency() const { return inLatency_; }     // frames, as the driver reports them
  long outputLatency() const { return outLatency_; }
  std::uint64_t resets() const { return resets_; }

 private:
  static long chooseBuffer(long want, long mn, long mx, long pref, long gran) { return asioChooseBuffer(want, mn, mx, pref, gran); }
  static const char* typeName(ASIOSampleType t) {
    switch (t) { case ASIOSTInt16LSB: return "16-bit integer"; case ASIOSTInt24LSB: return "24-bit integer"; case ASIOSTInt32LSB: return "32-bit integer";
                 case ASIOSTFloat32LSB: return "32-bit float"; case ASIOSTFloat64LSB: return "64-bit float"; default: return "unknown"; }
  }
  std::vector<std::string> outputNames() const override { std::lock_guard<std::mutex> g(nm_); return outNames_; }
  std::string sampleFormat() const override {
    std::lock_guard<std::mutex> g(nm_);
    if (types_.empty()) return std::string();
    const std::string in = nIn_ > 0 ? typeName(types_[0]) : "", out = nOut_ > 0 && static_cast<long>(types_.size()) > nIn_ ? typeName(types_[static_cast<std::size_t>(nIn_)]) : "";
    if (!in.empty() && in == out) return in + " (ASIO)";
    return (in.empty() ? "" : "in " + in) + (in.empty() || out.empty() ? "" : " · ") + (out.empty() ? "" : "out " + out) + " (ASIO)";
  }
  static bool supported(ASIOSampleType t) { return t == ASIOSTInt16LSB || t == ASIOSTInt24LSB || t == ASIOSTInt32LSB || t == ASIOSTFloat32LSB || t == ASIOSTFloat64LSB; }
  // am::fmt (sample_format.hpp): the same scale both ways, rounding, clamping: an untouched integer sample comes out the same
  static void toFloat(ASIOSampleType t, const void* src, float* dst, long n) {
    switch (t) {
      case ASIOSTInt16LSB: { auto p = static_cast<const std::int16_t*>(src); for (long i = 0; i < n; ++i) dst[i] = am::fmt::fromI16(p[i]); break; }
      case ASIOSTInt24LSB: { auto p = static_cast<const std::uint8_t*>(src); for (long i = 0; i < n; ++i) dst[i] = am::fmt::fromI24(am::fmt::unpack24(p + 3 * i)); break; }
      case ASIOSTInt32LSB: { auto p = static_cast<const std::int32_t*>(src); for (long i = 0; i < n; ++i) dst[i] = am::fmt::fromI32(p[i]); break; }
      case ASIOSTFloat32LSB: std::memcpy(dst, src, static_cast<std::size_t>(n) * 4); break;
      case ASIOSTFloat64LSB: { auto p = static_cast<const double*>(src); for (long i = 0; i < n; ++i) dst[i] = static_cast<float>(p[i]); break; }
      default: std::memset(dst, 0, static_cast<std::size_t>(n) * 4);
    }
  }
  static void fromFloat(ASIOSampleType t, const float* src, void* dst, long n) {
    switch (t) {
      case ASIOSTInt16LSB: { auto p = static_cast<std::int16_t*>(dst); for (long i = 0; i < n; ++i) p[i] = am::fmt::toI16(src[i]); break; }
      case ASIOSTInt24LSB: { auto p = static_cast<std::uint8_t*>(dst); for (long i = 0; i < n; ++i) am::fmt::pack24(p + 3 * i, am::fmt::toI24(src[i])); break; }
      case ASIOSTInt32LSB: { auto p = static_cast<std::int32_t*>(dst); for (long i = 0; i < n; ++i) p[i] = am::fmt::toI32(src[i]); break; }
      case ASIOSTFloat32LSB: std::memcpy(dst, src, static_cast<std::size_t>(n) * 4); break;
      case ASIOSTFloat64LSB: { auto p = static_cast<double*>(dst); for (long i = 0; i < n; ++i) p[i] = src[i]; break; }
      default: break;
    }
  }
  void block(long index) {
    struct Guard { std::atomic<int>& c; explicit Guard(std::atomic<int>& x) : c(x) { ++c; } ~Guard() { --c; } } guard(inCallback_);
    if (!tuned_.exchange(true)) am::sysopt::tuneAudioThread();   // once per start, on the driver's audio thread
    if (!running_ || !buffers_) {   // stopped or mid-reset: hand the driver silence, never stale output
      for (long i = 0; i < nOut_ && i < static_cast<long>(infos_.size()) - nIn_; ++i) { const ASIOSampleType t = types_[static_cast<std::size_t>(nIn_ + i)]; const std::size_t bytes = static_cast<std::size_t>(frames_) * (t == ASIOSTInt16LSB ? 2 : t == ASIOSTInt24LSB ? 3 : t == ASIOSTFloat64LSB ? 8 : 4); if (buffers_) std::memset(infos_[static_cast<std::size_t>(nIn_ + i)].buffers[index], 0, bytes); }
      return; }
    for (long i = 0; i < nIn_; ++i) toFloat(types_[static_cast<std::size_t>(i)], infos_[static_cast<std::size_t>(i)].buffers[index], inBuf_[static_cast<std::size_t>(i)].data(), frames_);
    for (auto& v : outBuf_) std::fill(v.begin(), v.end(), 0.0f);
    cb_(inp_.data(), outp_.data(), static_cast<std::size_t>(frames_));
    for (long i = 0; i < nOut_; ++i) fromFloat(types_[static_cast<std::size_t>(nIn_ + i)], outBuf_[static_cast<std::size_t>(i)].data(), infos_[static_cast<std::size_t>(nIn_ + i)].buffers[index], frames_);
    if (IASIO* d = drv_) d->outputReady();                     // optional: drivers without it answer ASE_NotPresent
  }
  static void onSwitch(long index, ASIOBool) { if (AsioBackend* s = g_active.load()) s->block(index); }
  static ASIOTime* onSwitchTime(ASIOTime* t, long index, ASIOBool) { if (AsioBackend* s = g_active.load()) s->block(index); return t; }
  static void onRate(ASIOSampleRate) { if (AsioBackend* s = g_active.load()) s->requestReset(); }   // the clock moved: buffers and types may differ now
  static long onMessage(long selector, long value, void*, double*) {
    AsioBackend* s = g_active.load();
    switch (selector) {
      case kAsioSelectorSupported:
        return (value == kAsioEngineVersion || value == kAsioOverload || value == kAsioResetRequest || value == kAsioBufferSizeChange || value == kAsioResyncRequest || value == kAsioLatenciesChanged || value == kAsioSupportsTimeInfo) ? 1 : 0;
      case kAsioEngineVersion: return 2;
      case kAsioOverload: if (s) ++s->xruns_; return 1;
      case kAsioResetRequest: case kAsioBufferSizeChange: case kAsioResyncRequest: if (s) s->requestReset(); return 1;   // accepted; done on the worker thread
      case kAsioLatenciesChanged: if (s && s->drv_) { long i = 0, o = 0; if (s->drv_->getLatencies(&i, &o) == ASE_OK) { s->inLatency_ = asiotrust::clampLatency(i); s->outLatency_ = asiotrust::clampLatency(o); } } return 1;
      case kAsioSupportsTimeInfo: return 1;                     // the driver then calls bufferSwitchTimeInfo, which this host handles the same way
      case kAsioSupportsTimeCode: return 0;
      default: return 0;
    }
  }
  void closeAll() {
    { std::lock_guard<std::mutex> g(wm_); quit_ = true; } wcv_.notify_one();
    if (worker_.joinable()) worker_.join();
    if (drv_ && buffers_) { buffers_ = false; safe([&] { drv_->disposeBuffers(); }, "disposeBuffers"); }
    if (drv_) { safe([&] { drv_->Release(); }, "Release"); drv_ = nullptr; }
    if (mine_) { AsioBackend* me = this; g_active.compare_exchange_strong(me, nullptr); mine_ = false; }
  }

  DeviceConfig cfg_; AudioCallback cb_; IASIO* drv_ = nullptr; std::string driverName_, clsid_; bool faulted_ = false;
  long nIn_ = 0, nOut_ = 0, frames_ = 0; std::atomic<bool> buffers_{false}; bool mine_ = false;
  std::vector<ASIOBufferInfo> infos_; std::vector<ASIOSampleType> types_; std::vector<std::string> outNames_;
  std::vector<std::vector<float>> inBuf_, outBuf_; std::vector<float> zeros_, sink_; std::vector<const float*> inp_; std::vector<float*> outp_;
  std::atomic<bool> running_{false}, tuned_{false}; std::atomic<int> inCallback_{0}; std::atomic<std::uint64_t> xruns_{0}, resets_{0}; std::atomic<long> inLatency_{0}, outLatency_{0};
  std::thread worker_; std::mutex wm_; std::condition_variable wcv_; bool resetPending_ = false, quit_ = false;
  std::mutex dm_; mutable std::mutex nm_; std::atomic<bool> wantRun_{false};   // dm_: start / stop / the reset, one at a time; nm_: the channel names and types
};
}  // namespace
bool asioStreamOpen() { return g_active.load() != nullptr; }
std::unique_ptr<Backend> makeAsioBackend() { return std::make_unique<AsioBackend>(); }
}  // namespace am
#endif
