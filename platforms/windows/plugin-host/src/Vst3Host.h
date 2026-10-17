// Vst3Host.h - the VST 3 half of PluginHost: loads ONE VST 3 effect (.vst3 bundle or file) and moves the same framed audio through it.
// Included by PluginHost.cpp after the framing helpers (readAll, writeFrame, jsonEsc, hello, openLibrary, symbol, gSampleRate, gBlock).
//
// The VST 3 interfaces are declared here from the public binary layout (method order, struct layout, interface ids): no Steinberg code is included.
// What is hosted: an effect with an audio input and an audio output bus (stereo or mono), its parameters (normalised values through the controller and the
// process call's parameter changes), one class of the module (the first effect class, or the one named on the command line). Not hosted: instruments, MIDI /
// event buses, side-chain inputs (left inactive), the plugin's own window, state save / restore, sample-accurate automation, the message channel between
// the plugin's processor and controller (IConnectionPoint).
//
// Method calling convention: stdcall on 32-bit Windows (what the COM-style interfaces use there), the platform default everywhere else.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32) && !defined(_WIN64)
#define V3CALL __stdcall
#else
#define V3CALL
#endif

namespace v3 {

typedef int32_t tresult;
typedef char TUID[16];
typedef char16_t TChar;
typedef TChar String128[128];

// interface ids: Windows uses the COM layout of the four words, every other system plain big-endian words
#ifdef _WIN32
#define V3UID(l1, l2, l3, l4) { (char)((l1) & 0xFF), (char)(((l1) >> 8) & 0xFF), (char)(((l1) >> 16) & 0xFF), (char)(((l1) >> 24) & 0xFF), \
  (char)(((l2) >> 16) & 0xFF), (char)(((l2) >> 24) & 0xFF), (char)((l2) & 0xFF), (char)(((l2) >> 8) & 0xFF), \
  (char)(((l3) >> 24) & 0xFF), (char)(((l3) >> 16) & 0xFF), (char)(((l3) >> 8) & 0xFF), (char)((l3) & 0xFF), \
  (char)(((l4) >> 24) & 0xFF), (char)(((l4) >> 16) & 0xFF), (char)(((l4) >> 8) & 0xFF), (char)((l4) & 0xFF) }
#else
#define V3UID(l1, l2, l3, l4) { (char)(((l1) >> 24) & 0xFF), (char)(((l1) >> 16) & 0xFF), (char)(((l1) >> 8) & 0xFF), (char)((l1) & 0xFF), \
  (char)(((l2) >> 24) & 0xFF), (char)(((l2) >> 16) & 0xFF), (char)(((l2) >> 8) & 0xFF), (char)((l2) & 0xFF), \
  (char)(((l3) >> 24) & 0xFF), (char)(((l3) >> 16) & 0xFF), (char)(((l3) >> 8) & 0xFF), (char)((l3) & 0xFF), \
  (char)(((l4) >> 24) & 0xFF), (char)(((l4) >> 16) & 0xFF), (char)(((l4) >> 8) & 0xFF), (char)((l4) & 0xFF) }
#endif
static const TUID kFUnknown = V3UID(0x00000000, 0x00000000, 0xC0000000, 0x00000046);
static const TUID kPluginFactory2 = V3UID(0x0007B650, 0xF24B4C0B, 0xA464EDB9, 0xF00B2ABB);
static const TUID kComponent = V3UID(0xE831FF31, 0xF2D54301, 0x928EBBEE, 0x25697802);
static const TUID kAudioProcessor = V3UID(0x42043F99, 0xB7DA453C, 0xA569E79D, 0x9AAEC33D);
static const TUID kEditController = V3UID(0xDCD7BBE3, 0x7742448D, 0xA874AACC, 0x979C759E);
static const TUID kHostApplication = V3UID(0x58E595CC, 0xDB2D4969, 0x8B6AAF8C, 0x36A664E5);
static const TUID kComponentHandler = V3UID(0x93A0BEA3, 0x0BD045DB, 0x8E890B0C, 0xC1E46AC6);
static const TUID kParameterChanges = V3UID(0xA4779663, 0x0BB64A56, 0xB44384A8, 0x466FEB9D);

static inline bool same(const char* a, const char* b) { return memcmp(a, b, 16) == 0; }

struct FUnknown {
  virtual tresult V3CALL queryInterface(const char* iid, void** obj) = 0;
  virtual uint32_t V3CALL addRef() = 0;
  virtual uint32_t V3CALL release() = 0;
};
struct PFactoryInfo { char vendor[64]; char url[256]; char email[128]; int32_t flags; };
struct PClassInfo { char cid[16]; int32_t cardinality; char category[32]; char name[64]; };
struct PClassInfo2 { char cid[16]; int32_t cardinality; char category[32]; char name[64]; uint32_t classFlags; char subCategories[128]; char vendor[64]; char version[64]; char sdkVersion[64]; };
struct IPluginFactory : FUnknown {
  virtual tresult V3CALL getFactoryInfo(PFactoryInfo* info) = 0;
  virtual int32_t V3CALL countClasses() = 0;
  virtual tresult V3CALL getClassInfo(int32_t index, PClassInfo* info) = 0;
  virtual tresult V3CALL createInstance(const char* cid, const char* iid, void** obj) = 0;
};
struct IPluginFactory2 : IPluginFactory { virtual tresult V3CALL getClassInfo2(int32_t index, PClassInfo2* info) = 0; };
struct IPluginBase : FUnknown {
  virtual tresult V3CALL initialize(FUnknown* context) = 0;
  virtual tresult V3CALL terminate() = 0;
};
struct BusInfo { int32_t mediaType; int32_t direction; int32_t channelCount; String128 name; int32_t busType; uint32_t flags; };
struct RoutingInfo { int32_t mediaType; int32_t busIndex; int32_t channel; };
struct IBStream;
struct IComponent : IPluginBase {
  virtual tresult V3CALL getControllerClassId(char* classId) = 0;
  virtual tresult V3CALL setIoMode(int32_t mode) = 0;
  virtual int32_t V3CALL getBusCount(int32_t type, int32_t dir) = 0;
  virtual tresult V3CALL getBusInfo(int32_t type, int32_t dir, int32_t index, BusInfo& bus) = 0;
  virtual tresult V3CALL getRoutingInfo(RoutingInfo& inInfo, RoutingInfo& outInfo) = 0;
  virtual tresult V3CALL activateBus(int32_t type, int32_t dir, int32_t index, uint8_t state) = 0;
  virtual tresult V3CALL setActive(uint8_t state) = 0;
  virtual tresult V3CALL setState(IBStream* state) = 0;
  virtual tresult V3CALL getState(IBStream* state) = 0;
};
struct ProcessSetup { int32_t processMode; int32_t symbolicSampleSize; int32_t maxSamplesPerBlock; double sampleRate; };
struct AudioBusBuffers { int32_t numChannels; uint64_t silenceFlags; float** channelBuffers32; };
struct ProcessContext {
  uint32_t state; double sampleRate; int64_t projectTimeSamples; int64_t systemTime; int64_t continousTimeSamples;
  double projectTimeMusic; double barPositionMusic; double cycleStartMusic; double cycleEndMusic; double tempo;
  int32_t timeSigNumerator; int32_t timeSigDenominator; uint8_t chordKey; uint8_t chordRoot; int16_t chordMask;
  int32_t smpteOffsetSubframes; uint32_t framesPerSecond; uint32_t frameFlags; int32_t samplesToNextClock;
};
struct IParamValueQueue : FUnknown {
  virtual uint32_t V3CALL getParameterId() = 0;
  virtual int32_t V3CALL getPointCount() = 0;
  virtual tresult V3CALL getPoint(int32_t index, int32_t& sampleOffset, double& value) = 0;
  virtual tresult V3CALL addPoint(int32_t sampleOffset, double value, int32_t& index) = 0;
};
struct IParameterChanges : FUnknown {
  virtual int32_t V3CALL getParameterCount() = 0;
  virtual IParamValueQueue* V3CALL getParameterData(int32_t index) = 0;
  virtual IParamValueQueue* V3CALL addParameterData(const uint32_t& id, int32_t& index) = 0;
};
struct IEventList : FUnknown {
  virtual int32_t V3CALL getEventCount() = 0;
  virtual tresult V3CALL getEvent(int32_t index, void* e) = 0;
  virtual tresult V3CALL addEvent(void* e) = 0;
};
struct ProcessData {
  int32_t processMode; int32_t symbolicSampleSize; int32_t numSamples; int32_t numInputs; int32_t numOutputs;
  AudioBusBuffers* inputs; AudioBusBuffers* outputs;
  IParameterChanges* inputParameterChanges; IParameterChanges* outputParameterChanges;
  IEventList* inputEvents; IEventList* outputEvents; ProcessContext* processContext;
};
struct IAudioProcessor : FUnknown {
  virtual tresult V3CALL setBusArrangements(uint64_t* inputs, int32_t numIns, uint64_t* outputs, int32_t numOuts) = 0;
  virtual tresult V3CALL getBusArrangement(int32_t dir, int32_t index, uint64_t& arr) = 0;
  virtual tresult V3CALL canProcessSampleSize(int32_t symbolicSampleSize) = 0;
  virtual uint32_t V3CALL getLatencySamples() = 0;
  virtual tresult V3CALL setupProcessing(ProcessSetup& setup) = 0;
  virtual tresult V3CALL setProcessing(uint8_t state) = 0;
  virtual tresult V3CALL process(ProcessData& data) = 0;
  virtual uint32_t V3CALL getTailSamples() = 0;
};
struct ParameterInfo { uint32_t id; String128 title; String128 shortTitle; String128 units; int32_t stepCount; double defaultNormalizedValue; int32_t unitId; int32_t flags; };
struct IComponentHandler : FUnknown {
  virtual tresult V3CALL beginEdit(uint32_t id) = 0;
  virtual tresult V3CALL performEdit(uint32_t id, double valueNormalized) = 0;
  virtual tresult V3CALL endEdit(uint32_t id) = 0;
  virtual tresult V3CALL restartComponent(int32_t flags) = 0;
};
struct IEditController : IPluginBase {
  virtual tresult V3CALL setComponentState(IBStream* state) = 0;
  virtual tresult V3CALL setState(IBStream* state) = 0;
  virtual tresult V3CALL getState(IBStream* state) = 0;
  virtual int32_t V3CALL getParameterCount() = 0;
  virtual tresult V3CALL getParameterInfo(int32_t paramIndex, ParameterInfo& info) = 0;
  virtual tresult V3CALL getParamStringByValue(uint32_t id, double valueNormalized, TChar* string) = 0;
  virtual tresult V3CALL getParamValueByString(uint32_t id, TChar* string, double& valueNormalized) = 0;
  virtual double V3CALL normalizedParamToPlain(uint32_t id, double valueNormalized) = 0;
  virtual double V3CALL plainParamToNormalized(uint32_t id, double plainValue) = 0;
  virtual double V3CALL getParamNormalized(uint32_t id) = 0;
  virtual tresult V3CALL setParamNormalized(uint32_t id, double value) = 0;
  virtual tresult V3CALL setComponentHandler(IComponentHandler* handler) = 0;
  virtual void* V3CALL createView(const char* name) = 0;
};
struct IHostApplication : FUnknown {
  virtual tresult V3CALL getName(TChar* name) = 0;
  virtual tresult V3CALL createInstance(const char* cid, const char* iid, void** obj) = 0;
};

// ── what the host gives the plugin ──
static const tresult kOk = 0, kNoInterface = -1;
#ifdef _WIN32
static const tresult kNotImplemented = (tresult)0x80004001;
#else
static const tresult kNotImplemented = 3;
#endif

struct Host : IHostApplication, IComponentHandler {
  // two interfaces on one object: each answers for its own id (the pointer handed back is the matching base)
  uint32_t V3CALL addRef() override { return 1; }
  uint32_t V3CALL release() override { return 1; }
  tresult V3CALL queryInterface(const char* iid, void** obj) override {
    if (same(iid, kFUnknown) || same(iid, kHostApplication)) { *obj = static_cast<IHostApplication*>(this); return kOk; }
    if (same(iid, kComponentHandler)) { *obj = static_cast<IComponentHandler*>(this); return kOk; }
    *obj = nullptr; return kNoInterface;
  }
  tresult V3CALL getName(TChar* name) override { const char* n = "Audio Mixer plugin host"; int i = 0; for (; n[i] && i < 127; i++) name[i] = (TChar)n[i]; name[i] = 0; return kOk; }
  tresult V3CALL createInstance(const char*, const char*, void** obj) override { *obj = nullptr; return kNotImplemented; }       // no IMessage / IAttributeList
  tresult V3CALL beginEdit(uint32_t) override { return kOk; }
  tresult V3CALL performEdit(uint32_t, double) override { return kOk; }
  tresult V3CALL endEdit(uint32_t) override { return kOk; }
  tresult V3CALL restartComponent(int32_t) override { return kOk; }
};
// FUnknown is a base of both Host bases: the compiler needs one final overrider for each, the three methods above serve both

struct Queue : IParamValueQueue {
  uint32_t id = 0; double value = 0;
  uint32_t V3CALL addRef() override { return 1; }
  uint32_t V3CALL release() override { return 1; }
  tresult V3CALL queryInterface(const char*, void** obj) override { *obj = nullptr; return kNoInterface; }
  uint32_t V3CALL getParameterId() override { return id; }
  int32_t V3CALL getPointCount() override { return 1; }
  tresult V3CALL getPoint(int32_t index, int32_t& sampleOffset, double& v) override { if (index != 0) return kNoInterface; sampleOffset = 0; v = value; return kOk; }
  tresult V3CALL addPoint(int32_t, double, int32_t& index) override { index = 0; return kOk; }                     // the plugin's own output values are taken and dropped
};
struct Changes : IParameterChanges {
  std::vector<Queue> q; int32_t n = 0;
  Changes() { q.resize(64); }
  uint32_t V3CALL addRef() override { return 1; }
  uint32_t V3CALL release() override { return 1; }
  tresult V3CALL queryInterface(const char* iid, void** obj) override { if (same(iid, kFUnknown) || same(iid, kParameterChanges)) { *obj = this; return kOk; } *obj = nullptr; return kNoInterface; }
  int32_t V3CALL getParameterCount() override { return n; }
  IParamValueQueue* V3CALL getParameterData(int32_t i) override { return i >= 0 && i < n ? &q[i] : nullptr; }
  IParamValueQueue* V3CALL addParameterData(const uint32_t& id, int32_t& index) override {                     // output side: the plugin reports its own parameters here (meters); they are dropped
    if (n >= (int32_t)q.size()) { index = 0; return &q[0]; }
    index = n; q[n].id = id; return &q[n++];
  }
};
struct NoEvents : IEventList {
  uint32_t V3CALL addRef() override { return 1; }
  uint32_t V3CALL release() override { return 1; }
  tresult V3CALL queryInterface(const char*, void** obj) override { *obj = nullptr; return kNoInterface; }
  int32_t V3CALL getEventCount() override { return 0; }
  tresult V3CALL getEvent(int32_t, void*) override { return kNoInterface; }
  tresult V3CALL addEvent(void*) override { return kNotImplemented; }
};

static inline std::string utf8(const TChar* s, int max) {
  std::string o;
  for (int i = 0; i < max && s[i]; i++) {
    uint32_t c = s[i];
    if (c >= 0xD800 && c < 0xDC00 && i + 1 < max && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00); i++; }
    if (c < 0x80) o += (char)c;
    else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 63)); }
    else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 63)); o += (char)(0x80 | (c & 63)); }
    else { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 63)); o += (char)(0x80 | ((c >> 6) & 63)); o += (char)(0x80 | (c & 63)); }
  }
  return o;
}

}  // namespace v3
