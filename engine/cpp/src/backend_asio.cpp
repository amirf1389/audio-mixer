// ASIO backend (Windows): the mixer runs on any installed ASIO driver, through the Steinberg ASIO SDK 2.3 interface (IASIO, drivers/asio/sdk, GPLv3).
// The driver is chosen by name (DeviceConfig::outDevice, else inDevice; a part of the name is enough, "" = the first registered driver). The inputs of the driver feed the
// strips, the stereo master goes to output channels 0 and 1, the block size is the driver's buffer size nearest to DeviceConfig::frames. ASIO drivers are single-client: a second
// open() while one is running fails. Sample types: Int16 / Int24 / Int32 / Float32 / Float64, little endian (what Windows drivers use).
// Compiled with mingw-w64 in the tests (scripts/build-engine.js --io); it has not been run against a real ASIO driver.
#ifdef _WIN32
#include "asio_sdk.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "audiomixer/asio.hpp"
#include "backends.hpp"

namespace am {
std::vector<AsioDriverInfo> listAsioDrivers() {
  std::vector<AsioDriverInfo> out; HKEY root = nullptr;
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ASIO", 0, KEY_READ, &root) != ERROR_SUCCESS) return out;
  char name[256];
  for (DWORD i = 0, n = sizeof name; RegEnumKeyExA(root, i, name, &n, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS; ++i, n = sizeof name) {
    HKEY k = nullptr; if (RegOpenKeyExA(root, name, 0, KEY_READ, &k) != ERROR_SUCCESS) continue;
    char clsid[96] = {0}; DWORD len = sizeof clsid - 1, type = 0;
    if (RegQueryValueExA(k, "CLSID", nullptr, &type, reinterpret_cast<BYTE*>(clsid), &len) == ERROR_SUCCESS && type == REG_SZ) out.push_back({name, clsid});
    RegCloseKey(k);
  }
  RegCloseKey(root);
  return out;
}

// Reads one driver's real channel counts and accepted sample rates through the Steinberg interface (no RtAudio). The driver is loaded, asked, and
// released again. ASIO drivers are single-client: never probe one while a stream of this program or the bridge has it open.
AsioProbeInfo probeAsioDriver(const AsioDriverInfo& d) {
  AsioProbeInfo info; info.name = d.name; info.clsid = d.clsid;
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  wchar_t wide[96] = {0}; MultiByteToWideChar(CP_ACP, 0, d.clsid.c_str(), -1, wide, 95);
  CLSID clsid; if (FAILED(CLSIDFromString(wide, &clsid))) { info.error = "the CLSID is not valid"; return info; }
  IASIO* drv = nullptr;
  HRESULT r = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, clsid, reinterpret_cast<void**>(&drv));
  if (FAILED(r) || !drv) { char b[96]; std::snprintf(b, sizeof b, "cannot load the driver (0x%08lx)", static_cast<unsigned long>(r)); info.error = b; return info; }
  if (!drv->init(GetDesktopWindow())) { char e[128] = {0}; drv->getErrorMessage(e); info.error = std::string("did not start: ") + (e[0] ? e : "init failed"); drv->Release(); return info; }
  long nIn = 0, nOut = 0;
  if (drv->getChannels(&nIn, &nOut) != ASE_OK) { info.error = "getChannels failed"; drv->Release(); return info; }
  info.inChannels = nIn; info.outChannels = nOut;
  static const double kRates[] = {8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000};
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
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);            // already initialised by the host: fine
    AsioBackend* none = nullptr;
    if (!g_active.compare_exchange_strong(none, this)) { err = "ASIO allows one driver at a time: another one is already open"; return false; }
    mine_ = true;
    const auto drivers = listAsioDrivers();
    if (drivers.empty()) { err = "no ASIO driver is installed (HKLM\\SOFTWARE\\ASIO is empty)"; return false; }
    std::string want = cfg.outDevice != "-" && !cfg.outDevice.empty() ? cfg.outDevice : (cfg.inDevice != "-" ? cfg.inDevice : std::string());
    const AsioDriverInfo* pick = want.empty() ? &drivers[0] : nullptr;
    for (const auto& d : drivers) if (!pick && (d.clsid == want || lower(d.name).find(lower(want)) != std::string::npos)) pick = &d;
    if (!pick) { err = "no ASIO driver named \"" + want + "\""; return false; }
    driverName_ = pick->name;

    wchar_t wide[96] = {0}; MultiByteToWideChar(CP_ACP, 0, pick->clsid.c_str(), -1, wide, 95);
    CLSID clsid; if (FAILED(CLSIDFromString(wide, &clsid))) { err = "the CLSID of \"" + pick->name + "\" is not valid"; return false; }
    HRESULT r = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, clsid, reinterpret_cast<void**>(&drv_));     // ASIO drivers use their CLSID as the interface id too
    if (FAILED(r) || !drv_) { char b[96]; std::snprintf(b, sizeof b, "cannot load the ASIO driver \"%s\" (0x%08lx)", pick->name.c_str(), static_cast<unsigned long>(r)); err = b; return false; }
    if (!drv_->init(GetDesktopWindow())) { char e[128] = {0}; drv_->getErrorMessage(e); err = "the ASIO driver \"" + pick->name + "\" did not start: " + (e[0] ? e : "init failed"); return false; }

    long nIn = 0, nOut = 0; ASIOError a = drv_->getChannels(&nIn, &nOut);
    if (a != ASE_OK) { err = msg("getChannels", a); return false; }
    nIn_ = std::min<long>(std::max(0, cfg.inChannels), nIn); nOut_ = std::min<long>(std::max(0, cfg.outChannels), nOut);
    if (cfg.inChannels > nIn || cfg.outChannels > nOut) { char b[160]; std::snprintf(b, sizeof b, "\"%s\" has %ld inputs and %ld outputs, %d and %d were asked", pick->name.c_str(), nIn, nOut, cfg.inChannels, cfg.outChannels); err = b; return false; }
    a = drv_->canSampleRate(cfg.sampleRate);
    if (a != ASE_OK) { char b[128]; std::snprintf(b, sizeof b, "\"%s\" does not run at %.0f Hz", pick->name.c_str(), cfg.sampleRate); err = b; return false; }
    a = drv_->setSampleRate(cfg.sampleRate); if (a != ASE_OK) { err = msg("setSampleRate", a); return false; }

    long mn = 0, mx = 0, pref = 0, gran = 0; a = drv_->getBufferSize(&mn, &mx, &pref, &gran);
    if (a != ASE_OK) { err = msg("getBufferSize", a); return false; }
    frames_ = chooseBuffer(static_cast<long>(cfg.frames), mn, mx, pref, gran);
    if (frames_ > static_cast<long>(std::max<unsigned>(cfg.frames, 16)) * 8) { err = "the smallest ASIO buffer of this driver is larger than 8 periods of the requested block"; return false; }

    infos_.assign(static_cast<std::size_t>(nIn_ + nOut_), ASIOBufferInfo());
    for (long i = 0; i < nIn_; ++i) { infos_[static_cast<std::size_t>(i)].isInput = ASIOTrue; infos_[static_cast<std::size_t>(i)].channelNum = i; }
    for (long i = 0; i < nOut_; ++i) { infos_[static_cast<std::size_t>(nIn_ + i)].isInput = ASIOFalse; infos_[static_cast<std::size_t>(nIn_ + i)].channelNum = i; }
    callbacks_.bufferSwitch = &AsioBackend::onSwitch; callbacks_.sampleRateDidChange = &AsioBackend::onRate; callbacks_.asioMessage = &AsioBackend::onMessage; callbacks_.bufferSwitchTimeInfo = &AsioBackend::onSwitchTime;
    a = drv_->createBuffers(infos_.data(), nIn_ + nOut_, frames_, &callbacks_);
    if (a != ASE_OK) { err = msg("createBuffers", a); return false; }
    buffers_ = true;

    types_.assign(infos_.size(), 0);
    for (std::size_t i = 0; i < infos_.size(); ++i) {
      ASIOChannelInfo ci; std::memset(&ci, 0, sizeof ci); ci.channel = infos_[i].channelNum; ci.isInput = infos_[i].isInput;
      a = drv_->getChannelInfo(&ci); if (a != ASE_OK) { err = msg("getChannelInfo", a); return false; }
      if (!supported(ci.type)) { char b[128]; std::snprintf(b, sizeof b, "\"%s\" uses the sample type %ld, which this backend does not convert", pick->name.c_str(), static_cast<long>(ci.type)); err = b; return false; }
      types_[i] = ci.type;
    }
    const std::size_t n = static_cast<std::size_t>(frames_);
    inBuf_.assign(static_cast<std::size_t>(std::max<long>(1, nIn_)), std::vector<float>(n, 0.0f)); outBuf_.assign(static_cast<std::size_t>(std::max<long>(1, nOut_)), std::vector<float>(n, 0.0f));
    inp_.clear(); outp_.clear();
    for (long i = 0; i < nIn_; ++i) inp_.push_back(inBuf_[static_cast<std::size_t>(i)].data());
    for (long i = 0; i < nOut_; ++i) outp_.push_back(outBuf_[static_cast<std::size_t>(i)].data());
    // the runtime hands out cfg.inChannels / cfg.outChannels pointers: fill what the driver does not have with silence
    zeros_.assign(n, 0.0f); sink_.assign(n, 0.0f);
    while (static_cast<int>(inp_.size()) < cfg.inChannels) inp_.push_back(zeros_.data());
    while (static_cast<int>(outp_.size()) < cfg.outChannels) outp_.push_back(sink_.data());
    return true;
  }
  bool start(std::string& err) override {
    if (!drv_) { err = "open() first"; return false; }
    running_ = true; ASIOError a = drv_->start();
    if (a != ASE_OK) { running_ = false; err = msg("start", a); return false; }
    return true;
  }
  void stop() override { if (drv_ && running_.exchange(false)) drv_->stop(); }
  std::uint64_t xruns() const override { return xruns_; }
  const std::string& driverName() const { return driverName_; }

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
  static bool supported(ASIOSampleType t) { return t == ASIOSTInt16LSB || t == ASIOSTInt24LSB || t == ASIOSTInt32LSB || t == ASIOSTFloat32LSB || t == ASIOSTFloat64LSB; }
  static void toFloat(ASIOSampleType t, const void* src, float* dst, long n) {
    switch (t) {
      case ASIOSTInt16LSB: { auto p = static_cast<const std::int16_t*>(src); for (long i = 0; i < n; ++i) dst[i] = static_cast<float>(p[i]) / 32768.0f; break; }
      case ASIOSTInt24LSB: { auto p = static_cast<const std::uint8_t*>(src); for (long i = 0; i < n; ++i) { std::int32_t v = (p[3 * i] << 8) | (p[3 * i + 1] << 16) | (static_cast<std::int32_t>(p[3 * i + 2]) << 24); dst[i] = static_cast<float>(v >> 8) / 8388608.0f; } break; }
      case ASIOSTInt32LSB: { auto p = static_cast<const std::int32_t*>(src); for (long i = 0; i < n; ++i) dst[i] = static_cast<float>(static_cast<double>(p[i]) / 2147483648.0); break; }
      case ASIOSTFloat32LSB: std::memcpy(dst, src, static_cast<std::size_t>(n) * 4); break;
      case ASIOSTFloat64LSB: { auto p = static_cast<const double*>(src); for (long i = 0; i < n; ++i) dst[i] = static_cast<float>(p[i]); break; }
      default: std::memset(dst, 0, static_cast<std::size_t>(n) * 4);
    }
  }
  static float clip1(float x) { return x > 1.0f ? 1.0f : (x < -1.0f ? -1.0f : x); }
  static void fromFloat(ASIOSampleType t, const float* src, void* dst, long n) {
    switch (t) {
      case ASIOSTInt16LSB: { auto p = static_cast<std::int16_t*>(dst); for (long i = 0; i < n; ++i) p[i] = static_cast<std::int16_t>(std::lrintf(clip1(src[i]) * 32767.0f)); break; }
      case ASIOSTInt24LSB: { auto p = static_cast<std::uint8_t*>(dst); for (long i = 0; i < n; ++i) { const std::int32_t v = static_cast<std::int32_t>(std::lrintf(clip1(src[i]) * 8388607.0f)); p[3 * i] = static_cast<std::uint8_t>(v); p[3 * i + 1] = static_cast<std::uint8_t>(v >> 8); p[3 * i + 2] = static_cast<std::uint8_t>(v >> 16); } break; }
      case ASIOSTInt32LSB: { auto p = static_cast<std::int32_t*>(dst); for (long i = 0; i < n; ++i) p[i] = static_cast<std::int32_t>(std::llrint(static_cast<double>(clip1(src[i])) * 2147483647.0)); break; }
      case ASIOSTFloat32LSB: std::memcpy(dst, src, static_cast<std::size_t>(n) * 4); break;
      case ASIOSTFloat64LSB: { auto p = static_cast<double*>(dst); for (long i = 0; i < n; ++i) p[i] = src[i]; break; }
      default: break;
    }
  }
  void block(long index) {
    if (!running_) return;
    for (long i = 0; i < nIn_; ++i) toFloat(types_[static_cast<std::size_t>(i)], infos_[static_cast<std::size_t>(i)].buffers[index], inBuf_[static_cast<std::size_t>(i)].data(), frames_);
    for (auto& v : outBuf_) std::fill(v.begin(), v.end(), 0.0f);
    cb_(inp_.data(), outp_.data(), static_cast<std::size_t>(frames_));
    for (long i = 0; i < nOut_; ++i) fromFloat(types_[static_cast<std::size_t>(nIn_ + i)], outBuf_[static_cast<std::size_t>(i)].data(), infos_[static_cast<std::size_t>(nIn_ + i)].buffers[index], frames_);
    if (drv_) drv_->outputReady();                              // optional: drivers without it answer ASE_NotPresent
  }
  static void onSwitch(long index, ASIOBool) { if (AsioBackend* s = g_active.load()) s->block(index); }
  static ASIOTime* onSwitchTime(ASIOTime* t, long index, ASIOBool) { if (AsioBackend* s = g_active.load()) s->block(index); return t; }
  static void onRate(ASIOSampleRate) {}
  static long onMessage(long selector, long value, void*, double*) {
    AsioBackend* s = g_active.load();
    switch (selector) {
      case kAsioSelectorSupported: return value == kAsioEngineVersion || value == kAsioOverload ? 1 : 0;
      case kAsioEngineVersion: return 2;
      case kAsioOverload: if (s) ++s->xruns_; return 1;
      default: return 0;                                        // time info, reset requests, ...: not handled; the host would have to reopen the driver
    }
  }
  void closeAll() {
    if (drv_) { if (buffers_) { drv_->disposeBuffers(); buffers_ = false; } drv_->Release(); drv_ = nullptr; }
    if (mine_) { AsioBackend* me = this; g_active.compare_exchange_strong(me, nullptr); mine_ = false; }
  }

  DeviceConfig cfg_; AudioCallback cb_; IASIO* drv_ = nullptr; std::string driverName_;
  long nIn_ = 0, nOut_ = 0, frames_ = 0; bool buffers_ = false, mine_ = false;
  std::vector<ASIOBufferInfo> infos_; std::vector<ASIOSampleType> types_; ASIOCallbacks callbacks_{};
  std::vector<std::vector<float>> inBuf_, outBuf_; std::vector<float> zeros_, sink_; std::vector<const float*> inp_; std::vector<float*> outp_;
  std::atomic<bool> running_{false}; std::atomic<std::uint64_t> xruns_{0};
};
}  // namespace
std::unique_ptr<Backend> makeAsioBackend() { return std::make_unique<AsioBackend>(); }
}  // namespace am
#endif
