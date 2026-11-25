#include "audiomixer/sysopt.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <chrono>
#include <cmath>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <malloc.h>
#  include <setupapi.h>
#  include <pdh.h>
#  include <pdhmsg.h>
#else
#  include <fstream>
#  include <sstream>
#  include <dirent.h>
#  include <unistd.h>
#  if defined(__GLIBC__)
#    include <malloc.h>
#  endif
#endif
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#  define AM_SYSOPT_X86 1
#  if defined(_MSC_VER)
#    include <intrin.h>
#  else
#    include <cpuid.h>
#  endif
#endif

#include <fstream>
namespace am::sysopt {

// ---------------------------------------------------------------------------------------------------------------------------------- pure parts

Topology classify(const std::vector<CpuSet>& sets) {
  Topology t; t.logical = static_cast<int>(sets.size());
  if (sets.empty()) return t;
  std::set<int> classes; for (const CpuSet& s : sets) classes.insert(s.efficiencyClass);
  t.classes = static_cast<int>(classes.size());
  const int top = *classes.rbegin(), bottom = *classes.begin();
  std::map<int, std::set<int>> coresOf;   // class -> physical cores (two logical processors of one core count once)
  for (const CpuSet& s : sets) coresOf[s.efficiencyClass].insert(s.core);
  for (const auto& [cls, cores] : coresOf) {
    const int n = static_cast<int>(cores.size());
    if (cls == top) t.perfCores += n;
    else if (t.classes >= 3 && cls == bottom) t.lowPowerCores += n;   // Core Ultra: P, E and the low-power E cores of the SoC tile
    else t.effCores += n;
  }
  if (t.classes > 1) for (const CpuSet& s : sets) if (s.efficiencyClass == top) t.fastIds.push_back(s.id);
  return t;
}

static std::string lower(std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
static bool has(const std::string& hay, const char* needle) { return hay.find(needle) != std::string::npos; }

std::string vendorOf(const std::string& name) {
  const std::string n = lower(name);
  if (has(n, "intel") || has(n, "genuineintel") || has(n, "ai boost")) return "intel";
  if (has(n, "amd") || has(n, "ryzen") || has(n, "authenticamd") || has(n, "xdna") || has(n, "epyc")) return "amd";
  if (has(n, "qualcomm") || has(n, "snapdragon") || has(n, "hexagon") || has(n, "oryon")) return "qualcomm";
  if (has(n, "apple")) return "apple";
  if (has(n, "arm") || has(n, "cortex") || has(n, "neoverse")) return "arm";
  return "other";
}

// a device name that is an AI accelerator ("NPU" as a word: not the "NPU" inside "INPUT")
bool isNpuName(const std::string& name) {
  const std::string n = lower(name);
  if (has(n, "ai boost") || has(n, "hexagon") || has(n, "ryzen ai") || has(n, "xdna") || has(n, "neural")) return true;
  for (std::size_t p = n.find("npu"); p != std::string::npos; p = n.find("npu", p + 1)) {
    const bool left = p == 0 || !std::isalpha(static_cast<unsigned char>(n[p - 1]));
    const bool right = p + 3 >= n.size() || !std::isalpha(static_cast<unsigned char>(n[p + 3]));
    if (left && right) return true;
  }
  return false;
}

static const char* buildArchName() {
#if defined(_M_ARM64) || defined(__aarch64__)
  return "arm64";
#elif defined(_M_X64) || defined(__x86_64__)
  return "x64";
#else
  return "x86";
#endif
}

static void simd(CpuInfo& c) {
#if defined(AM_SYSOPT_X86)
  unsigned a = 0, b = 0, cx = 0, d = 0;
#  if defined(_MSC_VER)
  int r[4]; __cpuid(r, 1); cx = static_cast<unsigned>(r[2]); __cpuidex(r, 7, 0); b = static_cast<unsigned>(r[1]);
#  else
  if (__get_cpuid(1, &a, &b, &cx, &d)) c.fma = (cx >> 12) & 1u;
  if (__get_cpuid_count(7, 0, &a, &b, &cx, &d)) { c.avx2 = (b >> 5) & 1u; c.avx512 = (b >> 16) & 1u; }
#  endif
#  if defined(_MSC_VER)
  c.fma = (cx >> 12) & 1u; c.avx2 = (b >> 5) & 1u; c.avx512 = (b >> 16) & 1u;
#  endif
  (void)a; (void)d;
#elif defined(__aarch64__) || defined(_M_ARM64)
  c.neon = true;   // every ARM64 processor has NEON (Advanced SIMD)
#endif
}

static std::atomic<bool> g_fast{true};
void setFastCoresForAudio(bool on) { g_fast = on; }
bool fastCoresForAudio() { return g_fast; }

static std::mutex g_noteM;
static char g_note[160] = "not started yet";   // written by an audio thread with snprintf: no allocation there
static AudioTune g_tune;
std::string audioThreadNote() { std::lock_guard<std::mutex> g(g_noteM); return g_note; }
AudioTune audioThreadState() { std::lock_guard<std::mutex> g(g_noteM); return g_tune; }

#if defined(_WIN32)
// ---------------------------------------------------------------------------------------------------------------------------------- Windows

namespace {
struct PowerState { ULONG Version, ControlMask, StateMask; };   // THREAD_POWER_THROTTLING_STATE
constexpr int kThreadPowerThrottling = 3;                     // THREAD_INFORMATION_CLASS (missing from some headers)
using FnGetCpuSets = BOOL(WINAPI*)(PSYSTEM_CPU_SET_INFORMATION, ULONG, PULONG, HANDLE, ULONG);
using FnSelectCpuSets = BOOL(WINAPI*)(HANDLE, const ULONG*, ULONG);
using FnSetThreadInfo = BOOL(WINAPI*)(HANDLE, int, LPVOID, DWORD);
using FnWow64_2 = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
struct MemCounters { DWORD cb, PageFaultCount; SIZE_T PeakWorkingSetSize, WorkingSetSize, QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage, QuotaPeakNonPagedPoolUsage, QuotaNonPagedPoolUsage, PagefileUsage, PeakPagefileUsage, PrivateUsage; };
using FnMemInfo = BOOL(WINAPI*)(HANDLE, MemCounters*, DWORD);
template <class F> F k32(const char* n) { return reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), n))); }

std::vector<CpuSet> cpuSets() {
  std::vector<CpuSet> out; auto get = k32<FnGetCpuSets>("GetSystemCpuSetInformation"); if (!get) return out;
  ULONG len = 0; get(nullptr, 0, &len, GetCurrentProcess(), 0); if (len == 0) return out;
  std::vector<unsigned char> buf(len);
  if (!get(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buf.data()), len, &len, GetCurrentProcess(), 0)) return out;
  for (ULONG off = 0; off + sizeof(DWORD) * 2 <= len;) {
    const auto* s = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(buf.data() + off); if (s->Size == 0) break;
    if (s->Type == CpuSetInformation) { CpuSet c; c.id = s->CpuSet.Id; c.core = s->CpuSet.Group * 256 + s->CpuSet.CoreIndex; c.efficiencyClass = s->CpuSet.EfficiencyClass; c.parked = s->CpuSet.Parked; out.push_back(c); }
    off += s->Size;
  }
  return out;
}
const Topology& topology() { static std::once_flag once; static Topology t; std::call_once(once, [] { t = classify(cpuSets()); }); return t; }

std::string regString(HKEY root, const wchar_t* key, const wchar_t* value) {
  wchar_t buf[256]; DWORD size = sizeof buf;
  if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return {};
  char out[512]; const int n = WideCharToMultiByte(CP_UTF8, 0, buf, -1, out, sizeof out, nullptr, nullptr); if (n <= 0) return {};
  std::string s(out); while (!s.empty() && s.back() == ' ') s.pop_back(); while (!s.empty() && s.front() == ' ') s.erase(s.begin()); return s;
}
std::string utf8(const wchar_t* w) { char out[512]; const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, out, sizeof out, nullptr, nullptr); return n > 0 ? std::string(out) : std::string(); }
}  // namespace

CpuInfo cpu() {
  CpuInfo c; c.buildArch = buildArchName();
  c.name = regString(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString");
  c.vendor = vendorOf(c.name.empty() ? regString(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"VendorIdentifier") : c.name);
  USHORT proc = 0, native = 0; c.arch = c.buildArch;
  if (auto w = k32<FnWow64_2>("IsWow64Process2"); w && w(GetCurrentProcess(), &proc, &native)) {
    if (native == IMAGE_FILE_MACHINE_ARM64) c.arch = "arm64"; else if (native == IMAGE_FILE_MACHINE_AMD64) c.arch = "x64"; else if (native == IMAGE_FILE_MACHINE_I386) c.arch = "x86";
  }
  c.emulated = c.arch == "arm64" && c.buildArch != "arm64";
  if (c.emulated) c.neon = true;   // the processor has it; this x64 build runs on it through emulation
  simd(c);
  c.topo = topology();
  if (c.topo.logical == 0) { SYSTEM_INFO si; GetNativeSystemInfo(&si); c.topo.logical = static_cast<int>(si.dwNumberOfProcessors); c.topo.classes = 1; c.topo.perfCores = c.topo.logical; }
  return c;
}

NpuInfo npu() {
  NpuInfo r;
  auto scan = [&](const GUID* cls, DWORD flags) {
    HDEVINFO h = SetupDiGetClassDevsW(cls, nullptr, nullptr, flags); if (h == INVALID_HANDLE_VALUE) return;
    SP_DEVINFO_DATA d; d.cbSize = sizeof d;
    for (DWORD i = 0; !r.found && SetupDiEnumDeviceInfo(h, i, &d); ++i) {
      wchar_t name[256] = {0};
      if (!SetupDiGetDeviceRegistryPropertyW(h, &d, SPDRP_FRIENDLYNAME, nullptr, reinterpret_cast<PBYTE>(name), sizeof name - sizeof(wchar_t), nullptr))
        SetupDiGetDeviceRegistryPropertyW(h, &d, SPDRP_DEVICEDESC, nullptr, reinterpret_cast<PBYTE>(name), sizeof name - sizeof(wchar_t), nullptr);
      const std::string n = utf8(name); if (n.empty()) continue;
      if (cls || isNpuName(n)) { r.found = true; r.name = n; r.vendor = vendorOf(n); }
    }
    SetupDiDestroyDeviceInfoList(h);
  };
  static const GUID kComputeAccelerator = {0xf01a9d53, 0x3ff6, 0x48d2, {0x9f, 0x97, 0xc8, 0xa7, 0x00, 0x4b, 0xe1, 0x0c}};   // "Neural processors" in Device Manager
  scan(&kComputeAccelerator, DIGCF_PRESENT);
  if (!r.found) scan(nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);   // older drivers list the NPU under System devices
  return r;
}

MemInfo memory() {
  MemInfo m; MEMORYSTATUSEX s; s.dwLength = sizeof s;
  if (GlobalMemoryStatusEx(&s)) { m.totalMb = s.ullTotalPhys >> 20; m.availMb = s.ullAvailPhys >> 20; m.loadPct = static_cast<int>(s.dwMemoryLoad); }
  if (auto f = k32<FnMemInfo>("K32GetProcessMemoryInfo")) { MemCounters c{}; c.cb = sizeof c; if (f(GetCurrentProcess(), &c, sizeof c)) { m.processMb = c.WorkingSetSize >> 20; m.privateMb = c.PrivateUsage >> 20; } }
  return m;
}

// PROCESSOR PANEL (Windows): pdh.dll is opened at run time, so the program needs no import of it and works where it is missing
namespace {
struct Pdh {
  HMODULE dll = nullptr; HANDLE q = nullptr; HANDLE perf = nullptr, freq = nullptr, temp = nullptr, power = nullptr; bool primed = false;
  decltype(&PdhCollectQueryData) collect = nullptr; decltype(&PdhGetFormattedCounterArrayW) array = nullptr;
  Pdh() {
    dll = LoadLibraryExW(L"pdh.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32); if (!dll) return;
    auto open = reinterpret_cast<decltype(&PdhOpenQueryW)>(reinterpret_cast<void*>(GetProcAddress(dll, "PdhOpenQueryW")));
    auto add = reinterpret_cast<decltype(&PdhAddEnglishCounterW)>(reinterpret_cast<void*>(GetProcAddress(dll, "PdhAddEnglishCounterW")));
    collect = reinterpret_cast<decltype(&PdhCollectQueryData)>(reinterpret_cast<void*>(GetProcAddress(dll, "PdhCollectQueryData")));
    array = reinterpret_cast<decltype(&PdhGetFormattedCounterArrayW)>(reinterpret_cast<void*>(GetProcAddress(dll, "PdhGetFormattedCounterArrayW")));
    if (!open || !add || !collect || !array || open(nullptr, 0, &q) != 0) { q = nullptr; return; }
    if (add(q, L"\\Processor Information(_Total)\\% Processor Performance", 0, &perf) != 0) perf = nullptr;
    if (add(q, L"\\Processor Information(_Total)\\Processor Frequency", 0, &freq) != 0) freq = nullptr;
    if (add(q, L"\\Thermal Zone Information(*)\\Temperature", 0, &temp) != 0) temp = nullptr;
    if (add(q, L"\\Power Meter(*)\\Power", 0, &power) != 0) power = nullptr;
  }
  // every instance of a counter as doubles (empty when the counter or its values are not there)
  std::vector<double> values(HANDLE c) const {
    std::vector<double> out; if (!c || !array) return out;
    DWORD bytes = 0, n = 0; if (array(c, PDH_FMT_DOUBLE, &bytes, &n, nullptr) != static_cast<PDH_STATUS>(PDH_MORE_DATA) || bytes == 0) return out;
    std::vector<unsigned char> buf(bytes); auto* it = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
    if (array(c, PDH_FMT_DOUBLE, &bytes, &n, it) != 0) return out;
    for (DWORD i = 0; i < n; ++i) if (it[i].FmtValue.CStatus == 0 && std::isfinite(it[i].FmtValue.doubleValue)) out.push_back(it[i].FmtValue.doubleValue);
    return out;
  }
};
std::uint64_t ft64(const FILETIME& f) { return (static_cast<std::uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime; }
}  // namespace

Sensors sensors() {
  static std::mutex m; std::lock_guard<std::mutex> g(m);
  static Pdh pdh; static std::uint64_t lastIdle = 0, lastTotal = 0;
  Sensors s;
  FILETIME idle, kern, user;   // kernel time includes idle time
  if (GetSystemTimes(&idle, &kern, &user)) {
    const std::uint64_t i = ft64(idle), t = ft64(kern) + ft64(user);
    static double lastLoad = -1;   // two readings inside one system tick: the previous load stands (no new interval to measure yet)
    if (lastTotal && t > lastTotal) { lastLoad = std::clamp(100.0 * (1.0 - static_cast<double>(i - lastIdle) / static_cast<double>(t - lastTotal)), 0.0, 100.0); lastIdle = i; lastTotal = t; }
    else if (!lastTotal) { lastIdle = i; lastTotal = t; }
    if (lastLoad >= 0) { s.loadOk = true; s.loadPct = lastLoad; }
  }
  if (pdh.q && pdh.collect(pdh.q) == 0) {
    if (!pdh.primed) { pdh.primed = true; Sleep(120); pdh.collect(pdh.q); }   // rate counters need two samples
    const std::vector<double> f = pdh.values(pdh.freq), p = pdh.values(pdh.perf);
    if (!f.empty() && f[0] > 0) { s.clockOk = true; s.perfPct = p.empty() ? 0 : p[0]; s.mhz = s.perfPct > 0 ? f[0] * s.perfPct / 100.0 : f[0]; }
    for (double k : pdh.values(pdh.temp)) { const double c = k - 273.15; if (c > 1 && c < 130) s.tempsC.push_back(c); }   // the counter is in kelvin; 0 K readings are zones with no sensor
    double mw = 0; for (double v : pdh.values(pdh.power)) mw += v; if (mw > 0) { s.powerOk = true; s.watts = mw / 1000.0; s.powerFrom = "power meter"; }
  }
  for (double c : s.tempsC) s.maxTempC = std::max(s.maxTempC, c);
  SYSTEM_POWER_STATUS ps; if (GetSystemPowerStatus(&ps)) { s.ac = ps.ACLineStatus == 1 ? 1 : ps.ACLineStatus == 0 ? 0 : -1; s.batteryPct = ps.BatteryLifePercent <= 100 ? ps.BatteryLifePercent : -1; s.saver = ps.SystemStatusFlag == 1; }
  return s;
}

static bool threadPower(bool control, bool eco) {
  static const auto set = k32<FnSetThreadInfo>("SetThreadInformation"); if (!set) return false;
  PowerState p{1, control ? 1u : 0u, eco ? 1u : 0u};
  return set(GetCurrentThread(), kThreadPowerThrottling, &p, sizeof p) != FALSE;
}

void prepareAudioTuning() { (void)topology(); }

void tuneAudioThread() {
  const bool high = threadPower(true, false);   // HighQoS: never slowed down for power, whatever the power plan
  const Topology& t = topology();
  static const auto select = k32<FnSelectCpuSets>("SetThreadSelectedCpuSets");
  bool pinned = false;
  if (select) {
    if (g_fast && !t.fastIds.empty()) pinned = select(GetCurrentThread(), reinterpret_cast<const ULONG*>(t.fastIds.data()), static_cast<ULONG>(t.fastIds.size())) != FALSE;
    else select(GetCurrentThread(), nullptr, 0);
  }
  std::lock_guard<std::mutex> g(g_noteM);
  g_tune.highQoS = high; g_tune.perfCores = t.perfCores; g_tune.state = pinned ? 1 : (t.classes > 1 && !g_fast) ? 2 : 3;
  if (pinned) std::snprintf(g_note, sizeof g_note, "audio on the %d performance cores, never slowed for power", t.perfCores);
  else if (t.classes > 1 && !g_fast) std::snprintf(g_note, sizeof g_note, "audio on any core (fast cores off), %s", high ? "never slowed for power" : "power control not available");
  else std::snprintf(g_note, sizeof g_note, "all cores alike: audio on any core, %s", high ? "never slowed for power" : "power control not available");
}

bool setThreadEco(bool eco) { return threadPower(eco, eco); }   // off: back to Windows' own choice

std::uint64_t trimMemory(bool workingSet) {
  HANDLE heaps[64]; const DWORD n = GetProcessHeaps(64, heaps);
  for (DWORD i = 0; i < n && i < 64; ++i) HeapCompact(heaps[i], 0);
  _heapmin();
  if (workingSet) SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));   // pages go to Windows' compressed store
  return memory().processMb;
}

#else
// ---------------------------------------------------------------------------------------------------------------------------------- Linux / other

namespace {
std::string readFile(const std::string& p) { std::ifstream f(p); std::stringstream s; s << f.rdbuf(); return s.str(); }
long readLong(const std::string& p, long dflt) { std::ifstream f(p); long v; return f >> v ? v : dflt; }
long kbField(const std::string& text, const char* key) { const std::size_t p = text.find(key); if (p == std::string::npos) return 0; return std::atol(text.c_str() + p + std::strlen(key)); }

std::vector<CpuSet> cpuSets() {
  std::vector<CpuSet> out; const long n = sysconf(_SC_NPROCESSORS_CONF);
  for (long i = 0; i < n; ++i) {
    const std::string base = "/sys/devices/system/cpu/cpu" + std::to_string(i);
    CpuSet c; c.id = static_cast<std::uint32_t>(i);
    c.core = static_cast<int>(readLong(base + "/topology/physical_package_id", 0) * 4096 + readLong(base + "/topology/core_id", i));
    c.efficiencyClass = static_cast<int>(readLong(base + "/cpu_capacity", 0));   // ARM big.LITTLE: a bigger capacity is a faster core
    out.push_back(c);
  }
  return out;
}
}  // namespace

CpuInfo cpu() {
  CpuInfo c; c.buildArch = buildArchName(); c.arch = c.buildArch;
  const std::string info = readFile("/proc/cpuinfo");
  for (const char* key : {"model name", "Hardware", "Model"}) {
    const std::size_t p = info.find(key); if (p == std::string::npos) continue;
    const std::size_t colon = info.find(':', p), eol = info.find('\n', p); if (colon == std::string::npos || colon > eol) continue;
    c.name = info.substr(colon + 2, eol - colon - 2); break;
  }
  c.vendor = vendorOf(c.name.empty() ? info : c.name);
  simd(c);
  c.topo = classify(cpuSets());
  return c;
}

NpuInfo npu() {
  NpuInfo r; DIR* d = opendir("/sys/class/accel"); if (!d) return r;   // the Linux accelerator subsystem: intel_vpu, amdxdna, qaic ...
  while (dirent* e = readdir(d)) {
    if (std::strncmp(e->d_name, "accel", 5) != 0) continue;
    char link[512] = {0}; const std::string p = std::string("/sys/class/accel/") + e->d_name + "/device/driver";
    const ssize_t n = readlink(p.c_str(), link, sizeof link - 1); std::string drv = n > 0 ? std::string(link, static_cast<std::size_t>(n)) : "accelerator";
    drv = drv.substr(drv.find_last_of('/') + 1);
    r.found = true; r.name = drv == "intel_vpu" ? "Intel NPU (intel_vpu)" : drv == "amdxdna" ? "AMD XDNA NPU (amdxdna)" : drv; r.vendor = vendorOf(r.name); break;
  }
  closedir(d); return r;
}

MemInfo memory() {
  MemInfo m; const std::string mi = readFile("/proc/meminfo");
  m.totalMb = static_cast<std::uint64_t>(kbField(mi, "MemTotal:")) >> 10; m.availMb = static_cast<std::uint64_t>(kbField(mi, "MemAvailable:")) >> 10;
  if (m.totalMb) m.loadPct = static_cast<int>(100 - 100 * m.availMb / m.totalMb);
  const std::string st = readFile("/proc/self/status");
  m.processMb = static_cast<std::uint64_t>(kbField(st, "VmRSS:")) >> 10; m.privateMb = static_cast<std::uint64_t>(kbField(st, "RssAnon:")) >> 10;
  return m;
}

// PROCESSOR PANEL (Linux / Android): /proc/stat, cpufreq, thermal zones, RAPL, power_supply; files that are not there or not readable are skipped
Sensors sensors() {
  static std::mutex m; std::lock_guard<std::mutex> g(m);
  static unsigned long long lastIdle = 0, lastTotal = 0, lastUj = 0; static auto lastAt = std::chrono::steady_clock::now();
  Sensors s;
  { std::istringstream st(readFile("/proc/stat")); std::string cpu; unsigned long long v[10] = {}; st >> cpu;
    if (cpu == "cpu") { for (auto& x : v) st >> x; const unsigned long long idle = v[3] + v[4]; unsigned long long total = 0; for (int k = 0; k < 8; ++k) total += v[k];
      static double lastLoad = -1;   // two readings inside one jiffy: the previous load stands
      if (lastTotal && total > lastTotal) { lastLoad = std::clamp(100.0 * (1.0 - static_cast<double>(idle - lastIdle) / static_cast<double>(total - lastTotal)), 0.0, 100.0); lastIdle = idle; lastTotal = total; }
      else if (!lastTotal) { lastIdle = idle; lastTotal = total; }
      if (lastLoad >= 0) { s.loadOk = true; s.loadPct = lastLoad; } } }
  { double sum = 0; int n = 0;
    for (int c = 0; c < 256; ++c) { const std::string f = readFile("/sys/devices/system/cpu/cpu" + std::to_string(c) + "/cpufreq/scaling_cur_freq"); if (f.empty()) { if (c > 0) break; continue; } const double khz = std::atof(f.c_str()); if (khz > 0) { sum += khz; ++n; } }
    if (n) { s.clockOk = true; s.mhz = sum / n / 1000.0; } }
  for (int z = 0; z < 64; ++z) { const std::string t = readFile("/sys/class/thermal/thermal_zone" + std::to_string(z) + "/temp"); if (t.empty()) { if (z > 8) break; continue; }
    const double c = std::atof(t.c_str()) / 1000.0; if (c > 1 && c < 130) s.tempsC.push_back(c); }
  for (double c : s.tempsC) s.maxTempC = std::max(s.maxTempC, c);
  { const std::string e = readFile("/sys/class/powercap/intel-rapl:0/energy_uj"); const auto now = std::chrono::steady_clock::now();   // the processor package's energy (often root only)
    if (!e.empty()) { const unsigned long long uj = std::strtoull(e.c_str(), nullptr, 10); const double dt = std::chrono::duration<double>(now - lastAt).count();
      if (lastUj && uj > lastUj && dt > 0.05) { s.powerOk = true; s.watts = static_cast<double>(uj - lastUj) / 1e6 / dt; s.powerFrom = "processor package"; } lastUj = uj; }
    lastAt = now; }
  if (DIR* d = opendir("/sys/class/power_supply")) {
    while (dirent* e = readdir(d)) { if (e->d_name[0] == '.') continue; const std::string b = std::string("/sys/class/power_supply/") + e->d_name + "/", type = readFile(b + "type");
      if (type.rfind("Battery", 0) == 0) { const std::string c = readFile(b + "capacity"); if (!c.empty()) s.batteryPct = std::atoi(c.c_str()); }
      else if (type.rfind("Mains", 0) == 0 || type.rfind("USB", 0) == 0) { const std::string o = readFile(b + "online"); if (!o.empty()) s.ac = std::max(s.ac, o[0] == '1' ? 1 : 0); } }
    closedir(d); }
  return s;
}

void prepareAudioTuning() {}
void tuneAudioThread() { std::lock_guard<std::mutex> g(g_noteM); g_tune.state = 4; std::snprintf(g_note, sizeof g_note, "audio on any core (the core preference is a Windows control)"); }
bool setThreadEco(bool) { return false; }
std::uint64_t trimMemory(bool) {
#if defined(__GLIBC__)
  malloc_trim(0);
#endif
  return memory().processMb;
}
#endif

std::string cleanField(const std::string& raw) {
  std::string o; for (unsigned char c : raw) { if (c == '\t' || c == '\r' || c == '\n') c = ' '; if (c >= 0x20 && c != 0x7f) o += static_cast<char>(c); }
  while (!o.empty() && o.back() == ' ') o.pop_back();
  std::size_t b = 0; while (b < o.size() && o[b] == ' ') ++b; o = o.substr(b);
  if (o.size() > 96) o.resize(96);
  return o;
}
#if defined(_WIN32)
namespace {
// one text value of a registry key under HKEY_LOCAL_MACHINE, opened for reading only
std::string regText(HKEY key, const char* name) { char buf[512]; DWORD n = sizeof(buf) - 1, type = 0; if (RegQueryValueExA(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &n) != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return {}; buf[n < sizeof(buf) ? n : sizeof(buf) - 1] = 0; return cleanField(buf); }
}  // namespace
Firmware firmware() {
  Firmware f; HKEY k;
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", 0, KEY_READ, &k) == ERROR_SUCCESS) { f.biosVendor = regText(k, "BIOSVendor"); f.biosVersion = regText(k, "BIOSVersion"); f.biosDate = regText(k, "BIOSReleaseDate");
    f.maker = regText(k, "SystemManufacturer"); f.model = regText(k, "SystemProductName"); f.board = regText(k, "BaseBoardProduct"); RegCloseKey(k); }
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &k) == ERROR_SUCCESS) { const std::string name = regText(k, "ProductName"), ver = regText(k, "DisplayVersion"), build = regText(k, "CurrentBuild");
    f.os = cleanField(name + (ver.empty() ? "" : " " + ver) + (build.empty() ? "" : " (build " + build + ")")); RegCloseKey(k); }
  return f;
}
std::vector<DriverInfo> audioDrivers() {   // the "Sound, video and game controllers" class: one key per installed driver
  std::vector<DriverInfo> out; HKEY cls;
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e96c-e325-11ce-bfc1-08002be10318}", 0, KEY_READ, &cls) != ERROR_SUCCESS) return out;
  for (DWORD i = 0; i < 256 && out.size() < 64; ++i) { char sub[64]; DWORD n = sizeof(sub); if (RegEnumKeyExA(cls, i, sub, &n, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
    HKEY k; if (RegOpenKeyExA(cls, sub, 0, KEY_READ, &k) != ERROR_SUCCESS) continue;   // "Properties" and the like cannot be opened: skipped
    DriverInfo d; d.name = regText(k, "DriverDesc"); d.provider = regText(k, "ProviderName"); d.version = regText(k, "DriverVersion"); d.date = regText(k, "DriverDate"); RegCloseKey(k);
    if (!d.name.empty()) out.push_back(d); }
  RegCloseKey(cls); return out;
}
#else
namespace {
std::string firstLine(const char* path) { std::ifstream f(path); std::string l; if (f) std::getline(f, l); return cleanField(l); }
}  // namespace
Firmware firmware() {
  Firmware f; f.biosVendor = firstLine("/sys/class/dmi/id/bios_vendor"); f.biosVersion = firstLine("/sys/class/dmi/id/bios_version"); f.biosDate = firstLine("/sys/class/dmi/id/bios_date");
  f.maker = firstLine("/sys/class/dmi/id/sys_vendor"); f.model = firstLine("/sys/class/dmi/id/product_name"); f.board = firstLine("/sys/class/dmi/id/board_name");
  std::string pretty; { std::ifstream os("/etc/os-release"); std::string l; while (std::getline(os, l)) if (l.rfind("PRETTY_NAME=", 0) == 0) { pretty = l.substr(12); if (pretty.size() >= 2 && pretty.front() == '"') pretty = pretty.substr(1, pretty.size() - 2); } }
  const std::string kernel = firstLine("/proc/sys/kernel/osrelease"); f.os = cleanField(pretty + (kernel.empty() ? "" : (pretty.empty() ? "Linux " : " (Linux ") + kernel + (pretty.empty() ? "" : ")")));
  return f;
}
std::vector<DriverInfo> audioDrivers() {   // ALSA's sound cards: " 0 [PCH            ]: HDA-Intel - HDA Intel PCH"
  std::vector<DriverInfo> out; std::ifstream f("/proc/asound/cards"); std::string l; const std::string kernel = firstLine("/proc/sys/kernel/osrelease");
  while (std::getline(f, l) && out.size() < 64) { const auto c = l.find("]: "); if (c == std::string::npos) continue; const std::string rest = l.substr(c + 3); const auto dash = rest.find(" - ");
    DriverInfo d; d.name = cleanField(dash == std::string::npos ? rest : rest.substr(dash + 3)); d.provider = cleanField(dash == std::string::npos ? "" : rest.substr(0, dash)); d.version = kernel.empty() ? "" : "Linux " + kernel; if (!d.name.empty()) out.push_back(d); }
  return out;
}
#endif

}  // namespace am::sysopt
