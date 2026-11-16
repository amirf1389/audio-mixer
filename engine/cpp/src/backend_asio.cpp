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

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
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
static void listAsioView(std::vector<AsioDriverInfo>& out, REGSAM view, int bits) {
  HKEY root = nullptr;
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ASIO", 0, KEY_READ | view, &root) != ERROR_SUCCESS) return;
  char name[256];
  for (DWORD i = 0, n = sizeof name; RegEnumKeyExA(root, i, name, &n, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS; ++i, n = sizeof name) {
    HKEY k = nullptr; if (RegOpenKeyExA(root, name, 0, KEY_READ | view, &k) != ERROR_SUCCESS) continue;
    char clsid[96] = {0}; DWORD len = sizeof clsid - 1, type = 0;
    if (RegQueryValueExA(k, "CLSID", nullptr, &type, reinterpret_cast<BYTE*>(clsid), &len) == ERROR_SUCCESS && type == REG_SZ) {
      bool dup = false; for (const auto& d : out) if (d.clsid == clsid) dup = true;   // a driver registered in both views (most 64-bit installers do) is listed once, as 64-bit
      if (!dup) out.push_back({name, clsid, bits});
    }
    RegCloseKey(k);
  }
  RegCloseKey(root);
}
std::vector<AsioDriverInfo> listAsioDrivers() {
  std::vector<AsioDriverInfo> out;
  // the 64-bit view first (KEY_WOW64_64KEY: the real SOFTWARE\ASIO), then the 32-bit view (KEY_WOW64_32KEY = SOFTWARE\WOW6432Node\ASIO) so a
  // 32-bit-only driver is at least seen and named, with its bits; on 32-bit Windows both open the same key
  listAsioView(out, KEY_WOW64_64KEY, 64);
  listAsioView(out, KEY_WOW64_32KEY, 32);
  return out;
}

// Reads one driver's real channel counts and accepted sample rates through the Steinberg interface (no RtAudio). The driver is loaded, asked, and
// released again. ASIO drivers are single-client: never probe one while a stream of this program or the bridge has it open.
AsioProbeInfo probeAsioDriver(const AsioDriverInfo& d) {
  AsioProbeInfo info; info.name = d.name; info.clsid = d.clsid;
  if (d.bits != static_cast<int>(sizeof(void*) * 8)) { info.error = d.bits == 32 ? "a 32-bit driver: only the 32-bit AmF X1 can load it" : "a 64-bit driver: only the 64-bit AmF X1 can load it"; return info; }
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);   // no-op when the thread already has COM (the console's main thread is apartment-threaded: fine, CoCreateInstance works there too)
  wchar_t wide[96] = {0}; MultiByteToWideChar(CP_ACP, 0, d.clsid.c_str(), -1, wide, 95);
  CLSID clsid; if (FAILED(CLSIDFromString(wide, &clsid))) { info.error = "the CLSID is not valid"; return info; }
  IASIO* drv = nullptr;
  HRESULT r = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, clsid, reinterpret_cast<void**>(&drv));
  if (FAILED(r) || !drv) { info.error = "cannot load the driver: " + hresultText(FAILED(r) ? r : E_POINTER); return info; }
  if (!drv->init(GetDesktopWindow())) { char e[128] = {0}; drv->getErrorMessage(e); info.error = std::string("did not start: ") + (e[0] ? e : "init failed"); drv->Release(); return info; }
  long nIn = 0, nOut = 0;
  if (drv->getChannels(&nIn, &nOut) != ASE_OK) { info.error = "getChannels failed"; drv->Release(); return info; }
  info.inChannels = nIn; info.outChannels = nOut;
  static const double kRates[] = {8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000, 705600, 768000};
  for (double rate : kRates) if (drv->canSampleRate(rate) == ASE_OK) info.sampleRates.push_back(rate);
  drv->Release();
  return info;
}

namespace {
std::string lower(std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
std::string msg(const char* what, long e) { char b[96]; std::snprintf(b, sizeof b, "%s failed (ASIO error %ld)", what, e); return b; }

class AsioBackend;
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
    const auto drivers = listAsioDrivers();
    if (drivers.empty()) { err = "no ASIO driver is installed (HKLM\\SOFTWARE\\ASIO is empty)"; closeAll(); return false; }
    std::string want = cfg.outDevice != "-" && !cfg.outDevice.empty() ? cfg.outDevice : (cfg.inDevice != "-" ? cfg.inDevice : std::string());
    const AsioDriverInfo* pick = want.empty() ? &drivers[0] : nullptr;
    for (const auto& d : drivers) if (!pick && (d.clsid == want || lower(d.name).find(lower(want)) != std::string::npos)) pick = &d;
    if (!pick) { err = "no ASIO driver named \"" + want + "\""; closeAll(); return false; }
    if (pick->bits != static_cast<int>(sizeof(void*) * 8)) { err = "\"" + pick->name + "\" is a " + std::to_string(pick->bits) + "-bit driver: only the " + std::to_string(pick->bits) + "-bit AmF X1 can load it"; closeAll(); return false; }
    driverName_ = pick->name;

    wchar_t wide[96] = {0}; MultiByteToWideChar(CP_ACP, 0, pick->clsid.c_str(), -1, wide, 95);
    CLSID clsid; if (FAILED(CLSIDFromString(wide, &clsid))) { err = "the CLSID of \"" + pick->name + "\" is not valid"; closeAll(); return false; }
    HRESULT r = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, clsid, reinterpret_cast<void**>(&drv_));     // ASIO drivers use their CLSID as the interface id too
    if (FAILED(r) || !drv_) { err = "cannot load the ASIO driver \"" + pick->name + "\": " + hresultText(FAILED(r) ? r : E_POINTER); drv_ = nullptr; closeAll(); return false; }
    if (!drv_->init(GetDesktopWindow())) { char e[128] = {0}; drv_->getErrorMessage(e); err = "the ASIO driver \"" + pick->name + "\" did not start: " + (e[0] ? e : "init failed"); closeAll(); return false; }

    long nIn = 0, nOut = 0; ASIOError a = drv_->getChannels(&nIn, &nOut);
    if (a != ASE_OK) { err = msg("getChannels", a); closeAll(); return false; }
    // what the driver lacks is filled with silence (a USB DAC has no inputs, an ADC no outputs): never a reason to refuse the driver
    nIn_ = std::min<long>(std::max(0, cfg.inChannels), nIn); nOut_ = std::min<long>(std::max(0, cfg.outChannels), nOut);
    if (nIn_ + nOut_ == 0) { char b[160]; std::snprintf(b, sizeof b, "\"%s\" has %ld inputs and %ld outputs: nothing to open", pick->name.c_str(), nIn, nOut); err = b; closeAll(); return false; }
    a = drv_->canSampleRate(cfg.sampleRate);
    if (a != ASE_OK) { char b[128]; std::snprintf(b, sizeof b, "\"%s\" does not run at %.0f Hz", pick->name.c_str(), cfg.sampleRate); err = b; closeAll(); return false; }
    a = drv_->setSampleRate(cfg.sampleRate);
    if (a != ASE_OK) {   // an externally clocked interface refuses to be set but may already run at that rate
      ASIOSampleRate now = 0; if (drv_->getSampleRate(&now) != ASE_OK || std::fabs(now - cfg.sampleRate) > 0.5) { err = msg("setSampleRate", a); closeAll(); return false; }
    }

    long mn = 0, mx = 0, pref = 0, gran = 0; a = drv_->getBufferSize(&mn, &mx, &pref, &gran);
    if (a != ASE_OK) { err = msg("getBufferSize", a); closeAll(); return false; }
    frames_ = chooseBuffer(static_cast<long>(cfg.frames), mn, mx, pref, gran);
    if (frames_ > static_cast<long>(std::max<unsigned>(cfg.frames, 16)) * 8) { err = "the smallest ASIO buffer of this driver is larger than 8 periods of the requested block"; closeAll(); return false; }
    if (!createBuffers(err)) { closeAll(); return false; }
    worker_ = std::thread([this] { workerLoop(); });
    return true;
  }
  // the buffers and channel types for the current frames_ (open, and again after a driver reset)
  bool createBuffers(std::string& err) {
    infos_.assign(static_cast<std::size_t>(nIn_ + nOut_), ASIOBufferInfo());
    for (long i = 0; i < nIn_; ++i) { infos_[static_cast<std::size_t>(i)].isInput = ASIOTrue; infos_[static_cast<std::size_t>(i)].channelNum = i; }
    for (long i = 0; i < nOut_; ++i) { infos_[static_cast<std::size_t>(nIn_ + i)].isInput = ASIOFalse; infos_[static_cast<std::size_t>(nIn_ + i)].channelNum = i; }
    callbacks_.bufferSwitch = &AsioBackend::onSwitch; callbacks_.sampleRateDidChange = &AsioBackend::onRate; callbacks_.asioMessage = &AsioBackend::onMessage; callbacks_.bufferSwitchTimeInfo = &AsioBackend::onSwitchTime;
    ASIOError a = drv_->createBuffers(infos_.data(), nIn_ + nOut_, frames_, &callbacks_);
    if (a != ASE_OK) { err = msg("createBuffers", a); return false; }
    buffers_ = true;

    types_.assign(infos_.size(), 0); outNames_.clear();
    for (std::size_t i = 0; i < infos_.size(); ++i) {
      ASIOChannelInfo ci; std::memset(&ci, 0, sizeof ci); ci.channel = infos_[i].channelNum; ci.isInput = infos_[i].isInput;
      a = drv_->getChannelInfo(&ci); if (a != ASE_OK) { err = msg("getChannelInfo", a); return false; }
      if (!supported(ci.type)) { char b[128]; std::snprintf(b, sizeof b, "\"%s\" uses the sample type %ld, which this backend does not convert", driverName_.c_str(), static_cast<long>(ci.type)); err = b; return false; }
      types_[i] = ci.type; if (!ci.isInput) outNames_.emplace_back(ci.name, strnlen(ci.name, sizeof ci.name));
    }
    const std::size_t n = static_cast<std::size_t>(frames_);
    inBuf_.assign(static_cast<std::size_t>(std::max<long>(1, nIn_)), std::vector<float>(n, 0.0f)); outBuf_.assign(static_cast<std::size_t>(std::max<long>(1, nOut_)), std::vector<float>(n, 0.0f));
    inp_.clear(); outp_.clear();
    for (long i = 0; i < nIn_; ++i) inp_.push_back(inBuf_[static_cast<std::size_t>(i)].data());
    for (long i = 0; i < nOut_; ++i) outp_.push_back(outBuf_[static_cast<std::size_t>(i)].data());
    // the runtime hands out cfg.inChannels / cfg.outChannels pointers: fill what the driver does not have with silence
    zeros_.assign(n, 0.0f); sink_.assign(n, 0.0f);
    while (static_cast<int>(inp_.size()) < cfg_.inChannels) inp_.push_back(zeros_.data());
    while (static_cast<int>(outp_.size()) < cfg_.outChannels) outp_.push_back(sink_.data());
    long inLat = 0, outLat = 0; if (drv_->getLatencies(&inLat, &outLat) == ASE_OK) { inLatency_ = inLat; outLatency_ = outLat; }
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
      const bool wasRunning = running_.exchange(false);
      while (inCallback_.load()) std::this_thread::yield();   // a bufferSwitch that started before running_ went false finishes first
      if (drv_) {
        drv_->stop();
        if (buffers_) { drv_->disposeBuffers(); buffers_ = false; }
        long mn = 0, mx = 0, pref = 0, gran = 0;
        if (drv_->getBufferSize(&mn, &mx, &pref, &gran) == ASE_OK) { const long f = chooseBuffer(frames_, mn, mx, pref, gran); if (f == frames_ || f <= static_cast<long>(std::max<unsigned>(cfg_.frames, 16)) * 8) frames_ = f; }
        std::string err; if (createBuffers(err) && wasRunning) { running_ = true; if (drv_->start() != ASE_OK) running_ = false; }
        ++resets_;
      }
      lk.lock();
    }
  }
  void requestReset() { { std::lock_guard<std::mutex> g(wm_); resetPending_ = true; } wcv_.notify_one(); }
  bool start(std::string& err) override {
    if (!drv_) { err = "open() first"; return false; }
    running_ = true; ASIOError a = drv_->start();
    if (a != ASE_OK) { running_ = false; err = msg("start", a); return false; }
    return true;
  }
  void stop() override { if (drv_ && running_.exchange(false)) { while (inCallback_.load()) std::this_thread::yield(); drv_->stop(); } }
  std::uint64_t xruns() const override { return xruns_; }
  const std::string& driverName() const { return driverName_; }
  long inputLatency() const { return inLatency_; }     // frames, as the driver reports them
  long outputLatency() const { return outLatency_; }
  std::uint64_t resets() const { return resets_; }

 private:
  static long chooseBuffer(long want, long mn, long mx, long pref, long gran) {
    if (mn == mx) return mn;
    if (want <= 0) return pref;
    long best = pref;
    if (gran == -1) {                                           // powers of two between min and max
      long b = mn, bestD = 1L << 30; for (; b <= mx; b *= 2) { const long d = std::labs(b - want); if (d < bestD) { bestD = d; best = b; } if (b == 0) break; }
    } else if (gran > 0) {                                      // min + k * granularity
      long k = (want - mn + gran / 2) / gran; if (k < 0) k = 0; best = mn + k * gran; if (best > mx) best = mx;
    } else best = std::min(std::max(want, mn), mx);
    return best;
  }
  static const char* typeName(ASIOSampleType t) {
    switch (t) { case ASIOSTInt16LSB: return "16-bit integer"; case ASIOSTInt24LSB: return "24-bit integer"; case ASIOSTInt32LSB: return "32-bit integer";
                 case ASIOSTFloat32LSB: return "32-bit float"; case ASIOSTFloat64LSB: return "64-bit float"; default: return "unknown"; }
  }
  std::vector<std::string> outputNames() const override { return outNames_; }
  std::string sampleFormat() const override {
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
    if (!running_ || !buffers_) {   // stopped or mid-reset: hand the driver silence, never stale output
      for (long i = 0; i < nOut_ && i < static_cast<long>(infos_.size()) - nIn_; ++i) { const ASIOSampleType t = types_[static_cast<std::size_t>(nIn_ + i)]; const std::size_t bytes = static_cast<std::size_t>(frames_) * (t == ASIOSTInt16LSB ? 2 : t == ASIOSTInt24LSB ? 3 : t == ASIOSTFloat64LSB ? 8 : 4); if (buffers_) std::memset(infos_[static_cast<std::size_t>(nIn_ + i)].buffers[index], 0, bytes); }
      return; }
    for (long i = 0; i < nIn_; ++i) toFloat(types_[static_cast<std::size_t>(i)], infos_[static_cast<std::size_t>(i)].buffers[index], inBuf_[static_cast<std::size_t>(i)].data(), frames_);
    for (auto& v : outBuf_) std::fill(v.begin(), v.end(), 0.0f);
    cb_(inp_.data(), outp_.data(), static_cast<std::size_t>(frames_));
    for (long i = 0; i < nOut_; ++i) fromFloat(types_[static_cast<std::size_t>(nIn_ + i)], outBuf_[static_cast<std::size_t>(i)].data(), infos_[static_cast<std::size_t>(nIn_ + i)].buffers[index], frames_);
    if (drv_) drv_->outputReady();                              // optional: drivers without it answer ASE_NotPresent
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
      case kAsioLatenciesChanged: if (s && s->drv_) { long i = 0, o = 0; if (s->drv_->getLatencies(&i, &o) == ASE_OK) { s->inLatency_ = i; s->outLatency_ = o; } } return 1;
      case kAsioSupportsTimeInfo: return 1;                     // the driver then calls bufferSwitchTimeInfo, which this host handles the same way
      case kAsioSupportsTimeCode: return 0;
      default: return 0;
    }
  }
  void closeAll() {
    { std::lock_guard<std::mutex> g(wm_); quit_ = true; } wcv_.notify_one();
    if (worker_.joinable()) worker_.join();
    if (drv_) { if (buffers_) { drv_->disposeBuffers(); buffers_ = false; } drv_->Release(); drv_ = nullptr; }
    if (mine_) { AsioBackend* me = this; g_active.compare_exchange_strong(me, nullptr); mine_ = false; }
  }

  DeviceConfig cfg_; AudioCallback cb_; IASIO* drv_ = nullptr; std::string driverName_;
  long nIn_ = 0, nOut_ = 0, frames_ = 0; std::atomic<bool> buffers_{false}; bool mine_ = false;
  std::vector<ASIOBufferInfo> infos_; std::vector<ASIOSampleType> types_; std::vector<std::string> outNames_; ASIOCallbacks callbacks_{};
  std::vector<std::vector<float>> inBuf_, outBuf_; std::vector<float> zeros_, sink_; std::vector<const float*> inp_; std::vector<float*> outp_;
  std::atomic<bool> running_{false}; std::atomic<int> inCallback_{0}; std::atomic<std::uint64_t> xruns_{0}, resets_{0}; std::atomic<long> inLatency_{0}, outLatency_{0};
  std::thread worker_; std::mutex wm_; std::condition_variable wcv_; bool resetPending_ = false, quit_ = false;
};
}  // namespace
std::unique_ptr<Backend> makeAsioBackend() { return std::make_unique<AsioBackend>(); }
}  // namespace am
#endif
