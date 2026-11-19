// SYSTEM OPTIMIZATION: what the computer is (processor, its core types, an AI accelerator, memory) and the operating-system controls that keep
// the sound steady while the rest of the console uses less.
//   FAST CORES FOR AUDIO: hybrid processors (Intel Core 12th gen and later / Core Ultra, AMD Ryzen with Zen 5c cores, some Snapdragon) mix
//     performance and efficiency cores; Windows may run the audio thread on an efficiency core, and on battery it may slow that core down
//     (EcoQoS): the block then takes too long and drops out. Each driver's audio thread calls tuneAudioThread() once it starts: it is marked
//     "never throttle" (HighQoS) and, on a hybrid processor, asks to run on the fastest core class (SetThreadSelectedCpuSets: a preference the
//     scheduler honours, not a hard affinity). The audio thread already runs under MMCSS "Pro Audio".
//   ECO IN THE BACKGROUND: the console's interface thread is marked EcoQoS while the window is behind another program or minimised; Windows
//     then runs it on efficiency cores at a lower clock. The sound is unaffected (its own thread is HighQoS).
//   MEMORY: trimMemory() gives the heap's free blocks back to Windows and, when no sound runs in this process, trims the working set: the pages
//     go to Windows' own memory compression and come back on use. Only this program's own memory is touched, never another program's.
//   AI ACCELERATOR: an NPU (Intel AI Boost, AMD Ryzen AI / XDNA, Qualcomm Hexagon) is found and shown. Nothing in the console runs on it yet.
// Windows calls are loaded at run time, so an older Windows simply does without them. Elsewhere the same functions report what they can.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace am::sysopt {

/** one logical processor as Windows describes it (GetSystemCpuSetInformation) */
struct CpuSet { std::uint32_t id = 0; int core = 0; int efficiencyClass = 0; bool parked = false; };

/** the processor's core types, worked out from its CPU sets (pure: testable without the hardware) */
struct Topology {
  int logical = 0;            // logical processors
  int classes = 0;            // core types: 1 = all alike, 2 = performance + efficiency, 3 = + low-power efficiency
  int perfCores = 0, effCores = 0, lowPowerCores = 0;   // physical cores of each type (with one type: all are perfCores)
  std::vector<std::uint32_t> fastIds;   // the CPU sets of the fastest class (empty when all cores are alike: no preference is needed)
};
Topology classify(const std::vector<CpuSet>& sets);

/** "intel", "amd", "qualcomm", "apple", "arm" or "other", from a processor or device name */
std::string vendorOf(const std::string& name);

/** a device name that is an AI accelerator: Intel AI Boost, AMD Ryzen AI / XDNA / "NPU Compute Accelerator", Qualcomm Hexagon NPU ("NPU" as a
 *  word: the "NPU" inside "INPUT" does not count) */
bool isNpuName(const std::string& name);

struct CpuInfo {
  std::string name, vendor;
  std::string arch;          // the processor's own: "x64", "arm64", "x86"
  std::string buildArch;     // this program's: "x64", "arm64"
  bool emulated = false;     // an x64 program on an ARM64 processor (Windows on Snapdragon runs it through Prism emulation)
  bool avx2 = false, fma = false, avx512 = false, neon = false;
  Topology topo;
};
CpuInfo cpu();

struct NpuInfo { bool found = false; std::string name, vendor; };
NpuInfo npu();

struct MemInfo {
  std::uint64_t totalMb = 0, availMb = 0; int loadPct = 0;   // the whole computer
  std::uint64_t processMb = 0, privateMb = 0;               // this program: in RAM now (working set) / committed
};
MemInfo memory();

/** PROCESSOR PANEL: what the computer itself exposes, read on demand (call it about once a second; rates are measured between calls).
 *  Windows: load from GetSystemTimes; clock, performance and temperatures from the system's performance counters (PDH, loaded at run time:
 *  "Processor Information", "Thermal Zone Information", "Power Meter"); AC / battery from GetSystemPowerStatus. Linux / Android: /proc/stat,
 *  cpufreq, thermal zones, the RAPL energy counter, power_supply. Nothing is written and no driver is touched; a value the computer does not
 *  expose stays "not available" (many desktops expose no temperature to programs, most expose no watts). */
struct Sensors {
  bool loadOk = false; double loadPct = 0;          // the whole computer, every core
  bool clockOk = false; double mhz = 0;             // the processor's clock now
  double perfPct = 0;                               // Windows: % of the base clock (over 100 = turbo); 0 when not known
  std::vector<double> tempsC; double maxTempC = 0;  // every thermal zone the system exposes (empty: none)
  bool powerOk = false; double watts = 0; std::string powerFrom;   // measured power and where it comes from ("power meter", "processor package")
  int ac = -1, batteryPct = -1; bool saver = false; // on mains (1) / battery (0) / unknown (-1); battery charge; battery saver
};
Sensors sensors();

/** on/off for the audio-thread preference (default on); read by tuneAudioThread */
void setFastCoresForAudio(bool on);
bool fastCoresForAudio();
/** works out the core types once (allocates): call before a driver whose first callback is the audio thread (ASIO) */
void prepareAudioTuning();
/** call on a driver's audio thread when it starts: HighQoS, and the fast cores on a hybrid processor */
void tuneAudioThread();
/** what the last tuneAudioThread did, for the SYSTEM panel and the log */
std::string audioThreadNote();
/** the same as a state: 0 not started, 1 on the performance cores, 2 any core (FAST CORES off), 3 any core (all cores alike), 4 any core (not
 *  Windows); `highQoS`: never slowed for power */
struct AudioTune { int state = 0; bool highQoS = false; int perfCores = 0; };
AudioTune audioThreadState();

/** EcoQoS on (true) or off (false) for the calling thread; false when the system has no such control */
bool setThreadEco(bool eco);

/** give memory back: the heap's free blocks, and the working set too when `workingSet`; returns the MB this program has in RAM afterwards */
std::uint64_t trimMemory(bool workingSet);

/** SYSTEM INFORMATION: read only. What the computer says about itself: its maker and model, the firmware (BIOS / UEFI) with its version
 *  and date, the operating system, and the sound drivers installed with their provider, version and date. Nothing here changes a
 *  driver or the firmware, and nothing is sent anywhere. A field the computer does not expose stays empty. */
struct Firmware { std::string biosVendor, biosVersion, biosDate, maker, model, board, os; };
Firmware firmware();
struct DriverInfo { std::string name, provider, version, date; };
std::vector<DriverInfo> audioDrivers();
/** a field as read from the system, made safe to show: printable characters only, spaces trimmed, at most 96 characters */
std::string cleanField(const std::string& raw);

}  // namespace am::sysopt
