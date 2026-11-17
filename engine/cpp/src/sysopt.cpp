#include "audiomixer/sysopt.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <malloc.h>
#  include <setupapi.h>
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

}  // namespace am::sysopt
