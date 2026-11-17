// SYSTEM OPTIMIZATION: the core types of real processors (as Windows lists their CPU sets), vendor and NPU names, and the live calls on this machine.
#include "audiomixer/sysopt.hpp"

#include <cstdio>
#include <array>
#include <thread>

using namespace am::sysopt;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

// cores: {count, threads per core, efficiency class}
static std::vector<CpuSet> chip(std::initializer_list<std::array<int, 3>> groups) {
  std::vector<CpuSet> v; std::uint32_t id = 256; int core = 0;
  for (const auto& g : groups) for (int c = 0; c < g[0]; ++c, ++core) for (int t = 0; t < g[1]; ++t) v.push_back({id++, core, g[2], false});
  return v;
}

int main() {
  { const Topology t = classify(chip({{6, 2, 2}, {8, 1, 1}, {2, 1, 0}}));   // Intel Core Ultra 7 155H (Meteor Lake): 6 P + 8 E + 2 LP-E
    CHECK(t.logical == 22); CHECK(t.classes == 3); CHECK(t.perfCores == 6); CHECK(t.effCores == 8); CHECK(t.lowPowerCores == 2); CHECK(t.fastIds.size() == 12); CHECK(t.fastIds.front() == 256); }
  { const Topology t = classify(chip({{6, 2, 1}, {8, 1, 0}}));               // Intel Core i7-12700H (Alder Lake): 6 P + 8 E
    CHECK(t.classes == 2); CHECK(t.perfCores == 6); CHECK(t.effCores == 8); CHECK(t.lowPowerCores == 0); CHECK(t.fastIds.size() == 12); }
  { const Topology t = classify(chip({{4, 1, 1}, {4, 1, 0}}));               // Intel Core Ultra 7 258V (Lunar Lake): 4 P + 4 LP-E, no SMT
    CHECK(t.classes == 2); CHECK(t.perfCores == 4); CHECK(t.effCores == 4); CHECK(t.fastIds.size() == 4); }
  { const Topology t = classify(chip({{4, 2, 1}, {8, 2, 0}}));               // AMD Ryzen AI 9 HX 370: 4 Zen 5 + 8 Zen 5c
    CHECK(t.perfCores == 4); CHECK(t.effCores == 8); CHECK(t.fastIds.size() == 8); }
  { const Topology t = classify(chip({{16, 2, 0}}));                         // AMD Ryzen 9 7950X: all alike
    CHECK(t.classes == 1); CHECK(t.perfCores == 16); CHECK(t.logical == 32); CHECK(t.fastIds.empty()); }
  { const Topology t = classify(chip({{12, 1, 0}}));                         // Snapdragon X Elite X1E-78-100: 12 Oryon cores, all alike
    CHECK(t.classes == 1); CHECK(t.perfCores == 12); CHECK(t.fastIds.empty()); }
  { const Topology t = classify({}); CHECK(t.logical == 0); CHECK(t.fastIds.empty()); }

  CHECK(vendorOf("Intel(R) Core(TM) Ultra 7 155H") == "intel");
  CHECK(vendorOf("AMD Ryzen AI 9 HX 370 w/ Radeon 890M") == "amd");
  CHECK(vendorOf("Snapdragon(R) X Elite - X1E78100 - Qualcomm(R) Oryon(TM) CPU") == "qualcomm");
  CHECK(vendorOf("Apple M3") == "apple");
  CHECK(vendorOf("Some CPU") == "other");
  CHECK(isNpuName("Intel(R) AI Boost")); CHECK(isNpuName("NPU Compute Accelerator Device")); CHECK(isNpuName("AMD IPU Device - XDNA"));
  CHECK(isNpuName("Snapdragon(R) X Elite - X1E78100 - Qualcomm(R) Hexagon(TM) NPU"));
  CHECK(!isNpuName("Microphone Input (USB Audio CODEC)")); CHECK(!isNpuName("HID-compliant INPUT device")); CHECK(!isNpuName("Realtek Audio"));
  CHECK(vendorOf("Intel(R) AI Boost") == "intel"); CHECK(vendorOf("Qualcomm(R) Hexagon(TM) NPU") == "qualcomm");

  // live on this machine: the calls work and say something sensible
  const CpuInfo c = cpu(); const MemInfo m = memory(); const NpuInfo n = npu();
  CHECK(c.topo.logical > 0); CHECK(!c.buildArch.empty()); CHECK(!c.arch.empty()); CHECK(m.totalMb > 0); CHECK(m.processMb > 0);
  std::thread([] { prepareAudioTuning(); tuneAudioThread(); }).join();
  CHECK(audioThreadNote() != "not started yet"); CHECK(audioThreadState().state >= 1);
  setFastCoresForAudio(false); CHECK(!fastCoresForAudio()); setFastCoresForAudio(true);
  const std::uint64_t after = trimMemory(false); CHECK(after > 0);
  std::printf("sysopt: %s (%s, %s%s), %d logical, %d types: %d perf / %d eff / %d low-power; AVX2 %d FMA %d NEON %d\n", c.name.c_str(), c.vendor.c_str(), c.arch.c_str(),
              c.emulated ? ", emulated" : "", c.topo.logical, c.topo.classes, c.topo.perfCores, c.topo.effCores, c.topo.lowPowerCores, c.avx2, c.fma, c.neon);
  std::printf("sysopt: RAM %llu MB, %llu free (%d %% used), this program %llu MB; NPU %s; audio: %s; eco %d\n", (unsigned long long)m.totalMb, (unsigned long long)m.availMb, m.loadPct,
              (unsigned long long)m.processMb, n.found ? n.name.c_str() : "none", audioThreadNote().c_str(), int(setThreadEco(true)));
  setThreadEco(false);
  std::printf(fails ? "sysopt: %d FAILED\n" : "sysopt: all passed\n", fails);
  return fails ? 1 : 0;
}
