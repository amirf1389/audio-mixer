// A tiny VST2 plug-in written for the host test against the public ABI (app/vst2/vst2_abi.hpp): "AMIX Test Gain 2", one parameter GAIN
// (0..2, default 1 at 0.5), canDo "bypass", stereo in / out, state as a chunk {gain}. Built as amixtest2.so / .dll next to the test.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "vst2/vst2_abi.hpp"

using namespace am::vst2::abi;

namespace {
struct Gain2 { AEffect fx{}; float gain = 1.f; bool bypass = false; float chunk = 1.f; };
Gain2* of(AEffect* e) { return static_cast<Gain2*>(e->object); }

std::intptr_t dispatch(AEffect* e, std::int32_t op, std::int32_t idx, std::intptr_t val, void* ptr, float) {
  Gain2* g = of(e);
  switch (op) {
    case effOpen: case effSetSampleRate: case effSetBlockSize: case effMainsChanged: case effStartProcess: case effStopProcess: return 0;
    case effClose: delete g; return 0;
    case effGetParamName: if (idx == 0 && ptr) std::strcpy(static_cast<char*>(ptr), "Gain"); return 0;
    case effGetParamLabel: if (idx == 0 && ptr) std::strcpy(static_cast<char*>(ptr), "x"); return 0;
    case effGetParamDisplay: if (idx == 0 && ptr) std::snprintf(static_cast<char*>(ptr), 24, "%.2f", g->gain); return 0;
    case effCanBeAutomated: return 1;
    case effGetEffectName: if (ptr) std::strcpy(static_cast<char*>(ptr), "AMIX Test Gain 2"); return 1;
    case effGetVendorString: if (ptr) std::strcpy(static_cast<char*>(ptr), "AMFB Audio (test)"); return 1;
    case effGetProductString: if (ptr) std::strcpy(static_cast<char*>(ptr), "AMIX Test Gain 2"); return 1;
    case effGetVendorVersion: return 100;
    case effGetVstVersion: return kVstVersion;
    case effCanDo: { const char* w = static_cast<const char*>(ptr); return w && (!std::strcmp(w, "bypass") || !std::strcmp(w, "1in1out") || !std::strcmp(w, "2in2out")) ? 1 : 0; }
    case effSetBypass: g->bypass = val != 0; return 1;
    case effGetChunk: { g->chunk = g->gain; if (ptr) *static_cast<void**>(ptr) = &g->chunk; return sizeof(float); }
    case effSetChunk: if (ptr && val >= static_cast<std::intptr_t>(sizeof(float))) std::memcpy(&g->gain, ptr, sizeof(float)); return 1;
    default: return 0;
  }
}
void setParam(AEffect* e, std::int32_t idx, float v) { if (idx == 0) of(e)->gain = v * 2; }
float getParam(AEffect* e, std::int32_t idx) { return idx == 0 ? of(e)->gain / 2 : 0; }
void processReplacing(AEffect* e, float** in, float** out, std::int32_t n) { Gain2* g = of(e); const float k = g->bypass ? 1.f : g->gain; for (int c = 0; c < 2; ++c) for (std::int32_t i = 0; i < n; ++i) out[c][i] = in[c][i] * k; }
}  // namespace

extern "C" {
#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
AEffect* VSTPluginMain(HostCallback host) {
  if (host && host(nullptr, audioMasterVersion, 0, 0, nullptr, 0) < 2) return nullptr;
  Gain2* g = new Gain2(); AEffect& e = g->fx; e.magic = kMagic; e.dispatcher = dispatch; e.setParameter = setParam; e.getParameter = getParam; e.processReplacing = processReplacing;
  e.numPrograms = 1; e.numParams = 1; e.numInputs = 2; e.numOutputs = 2; e.flags = kCanReplacing | kProgramChunks; e.uniqueID = 0x414D5832; e.version = 1000; e.object = g;
  return &e;
}
}
