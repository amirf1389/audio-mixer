// A tiny VST3 plug-in written for the host test (not from the SDK samples): "AMIX Test Gain", one Audio Module class that is component,
// processor and controller in one. Parameters: GAIN (0..2, default 1 at normalized 0.5), BYPASS (kIsBypass). State: the gain as a float.
// Mono or stereo in / out. Built as the bundle amixtest.vst3/Contents/<arch>/amixtest.so|.vst3 by the engine's CMake for the test only.
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstunits.h"
#include "pluginterfaces/vst/vstspeaker.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {
bool same(const TUID a, const TUID b) { return std::memcmp(a, b, 16) == 0; }
void str16(const char* s, String128 out) { int n = 0; while (*s && n < 127) out[n++] = static_cast<TChar>(static_cast<unsigned char>(*s++)); out[n] = 0; }
static const TUID kTestCid = INLINE_UID(0xA3C1F00D, 0x5E7A4B21, 0x9C0D1E2F, 0x30415263);
constexpr ParamID kGain = 100, kBypass = 101;

class TestGain : public IComponent, public IAudioProcessor, public IEditController {
 public:
  virtual ~TestGain() = default;
  tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
    if (same(iid, FUnknown_iid) || same(iid, IPluginBase_iid) || same(iid, IComponent_iid)) { addRef(); *obj = static_cast<IComponent*>(this); return kResultOk; }
    if (same(iid, IAudioProcessor_iid)) { addRef(); *obj = static_cast<IAudioProcessor*>(this); return kResultOk; }
    if (same(iid, IEditController_iid)) { addRef(); *obj = static_cast<IEditController*>(this); return kResultOk; }
    *obj = nullptr; return kNoInterface;
  }
  uint32 PLUGIN_API addRef() override { return ++refs_; }
  uint32 PLUGIN_API release() override { const uint32 r = --refs_; if (!r) delete this; return r; }
  // IPluginBase
  tresult PLUGIN_API initialize(FUnknown*) override { return kResultOk; }
  tresult PLUGIN_API terminate() override { return kResultOk; }
  // IComponent
  tresult PLUGIN_API getControllerClassId(TUID) override { return kResultFalse; }   // the controller is this object
  tresult PLUGIN_API setIoMode(IoMode) override { return kResultOk; }
  int32 PLUGIN_API getBusCount(MediaType t, BusDirection) override { return t == kAudio ? 1 : 0; }
  tresult PLUGIN_API getBusInfo(MediaType t, BusDirection d, int32 i, BusInfo& b) override { if (t != kAudio || i != 0) return kResultFalse; b.mediaType = kAudio; b.direction = d; b.channelCount = channels_; str16(d == kInput ? "In" : "Out", b.name); b.busType = kMain; b.flags = BusInfo::kDefaultActive; return kResultOk; }
  tresult PLUGIN_API getRoutingInfo(RoutingInfo&, RoutingInfo&) override { return kResultFalse; }
  tresult PLUGIN_API activateBus(MediaType, BusDirection, int32, TBool) override { return kResultOk; }
  tresult PLUGIN_API setActive(TBool) override { return kResultOk; }
  tresult PLUGIN_API setState(IBStream* s) override { float g = 1; int32 n = 0; if (s->read(&g, 4, &n) == kResultOk && n == 4) gain_ = g; return kResultOk; }
  tresult PLUGIN_API getState(IBStream* s) override { float g = gain_.load(); return s->write(&g, 4, nullptr); }   // one override serves IComponent and IEditController (same signature)
  // IAudioProcessor
  tresult PLUGIN_API setBusArrangements(SpeakerArrangement* in, int32 nIn, SpeakerArrangement* out, int32 nOut) override { if (nIn != 1 || nOut != 1) return kResultFalse; if (in[0] == SpeakerArr::kMono && out[0] == SpeakerArr::kMono) { channels_ = 1; return kResultOk; } if (in[0] == SpeakerArr::kStereo && out[0] == SpeakerArr::kStereo) { channels_ = 2; return kResultOk; } return kResultFalse; }
  tresult PLUGIN_API getBusArrangement(BusDirection, int32 i, SpeakerArrangement& a) override { if (i != 0) return kResultFalse; a = channels_ == 1 ? SpeakerArr::kMono : SpeakerArr::kStereo; return kResultOk; }
  tresult PLUGIN_API canProcessSampleSize(int32 s) override { return s == kSample32 ? kResultOk : kResultFalse; }
  uint32 PLUGIN_API getLatencySamples() override { return 0; }
  tresult PLUGIN_API setupProcessing(ProcessSetup& s) override { rate_ = s.sampleRate; return kResultOk; }
  tresult PLUGIN_API setProcessing(TBool) override { return kResultOk; }
  tresult PLUGIN_API process(ProcessData& d) override {
    if (d.inputParameterChanges) for (int32 i = 0; i < d.inputParameterChanges->getParameterCount(); ++i) { IParamValueQueue* q = d.inputParameterChanges->getParameterData(i); if (!q || q->getPointCount() < 1) continue; int32 off = 0; ParamValue v = 0; q->getPoint(q->getPointCount() - 1, off, v); if (q->getParameterId() == kGain) gain_ = static_cast<float>(v * 2); else if (q->getParameterId() == kBypass) bypass_ = v >= 0.5; }
    if (d.numOutputs < 1 || !d.outputs) return kResultOk;
    const float g = bypass_ ? 1.f : gain_.load();
    for (int32 c = 0; c < d.outputs[0].numChannels; ++c) { float* o = d.outputs[0].channelBuffers32[c]; const float* in = d.numInputs > 0 && d.inputs && c < d.inputs[0].numChannels ? d.inputs[0].channelBuffers32[c] : nullptr; for (int32 i = 0; i < d.numSamples; ++i) o[i] = in ? in[i] * g : 0.f; }
    return kResultOk;
  }
  uint32 PLUGIN_API getTailSamples() override { return 0; }
  // IEditController
  tresult PLUGIN_API setComponentState(IBStream* s) override { return setState(s); }
  int32 PLUGIN_API getParameterCount() override { return 2; }
  tresult PLUGIN_API getParameterInfo(int32 i, ParameterInfo& p) override {
    if (i == 0) { p.id = kGain; str16("Gain", p.title); str16("Gain", p.shortTitle); str16("x", p.units); p.stepCount = 0; p.defaultNormalizedValue = 0.5; p.unitId = kRootUnitId; p.flags = ParameterInfo::kCanAutomate; return kResultOk; }
    if (i == 1) { p.id = kBypass; str16("Bypass", p.title); str16("Byp", p.shortTitle); p.units[0] = 0; p.stepCount = 1; p.defaultNormalizedValue = 0; p.unitId = kRootUnitId; p.flags = ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass; return kResultOk; }
    return kResultFalse;
  }
  tresult PLUGIN_API getParamStringByValue(ParamID id, ParamValue v, String128 s) override { char b[32]; if (id == kGain) std::snprintf(b, sizeof b, "%.2f x", v * 2); else std::snprintf(b, sizeof b, "%s", v >= 0.5 ? "on" : "off"); str16(b, s); return kResultOk; }
  tresult PLUGIN_API getParamValueByString(ParamID, TChar*, ParamValue&) override { return kResultFalse; }
  ParamValue PLUGIN_API normalizedParamToPlain(ParamID id, ParamValue v) override { return id == kGain ? v * 2 : v; }
  ParamValue PLUGIN_API plainParamToNormalized(ParamID id, ParamValue v) override { return id == kGain ? v / 2 : v; }
  ParamValue PLUGIN_API getParamNormalized(ParamID id) override { return id == kGain ? gain_ / 2 : id == kBypass ? (bypass_ ? 1.0 : 0.0) : 0; }
  tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue v) override { if (id == kGain) gain_ = static_cast<float>(v * 2); else if (id == kBypass) bypass_ = v >= 0.5; return kResultOk; }
  tresult PLUGIN_API setComponentHandler(IComponentHandler*) override { return kResultOk; }
  IPlugView* PLUGIN_API createView(FIDString) override { return nullptr; }
 private:
  std::atomic<uint32> refs_{1}; std::atomic<float> gain_{1.f}; std::atomic<bool> bypass_{false}; int channels_ = 2; double rate_ = 48000;
};

class Factory : public IPluginFactory2 {
 public:
  virtual ~Factory() = default;
  tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override { if (same(iid, FUnknown_iid) || same(iid, IPluginFactory_iid) || same(iid, IPluginFactory2_iid)) { addRef(); *obj = static_cast<IPluginFactory2*>(this); return kResultOk; } *obj = nullptr; return kNoInterface; }
  uint32 PLUGIN_API addRef() override { return ++refs_; }
  uint32 PLUGIN_API release() override { const uint32 r = --refs_; if (!r) delete this; return r; }
  tresult PLUGIN_API getFactoryInfo(PFactoryInfo* i) override { *i = PFactoryInfo(); std::strcpy(i->vendor, "AMFB Audio (test)"); std::strcpy(i->url, "https://example.invalid"); std::strcpy(i->email, "test@example.invalid"); i->flags = PFactoryInfo::kUnicode; return kResultOk; }
  int32 PLUGIN_API countClasses() override { return 1; }
  tresult PLUGIN_API getClassInfo(int32 idx, PClassInfo* i) override { if (idx != 0) return kResultFalse; *i = PClassInfo(); std::memcpy(i->cid, kTestCid, 16); i->cardinality = PClassInfo::kManyInstances; std::strcpy(i->category, kVstAudioEffectClass); std::strcpy(i->name, "AMIX Test Gain"); return kResultOk; }
  tresult PLUGIN_API getClassInfo2(int32 idx, PClassInfo2* i) override { if (idx != 0) return kResultFalse; *i = PClassInfo2(); std::memcpy(i->cid, kTestCid, 16); i->cardinality = PClassInfo::kManyInstances; std::strcpy(i->category, kVstAudioEffectClass); std::strcpy(i->name, "AMIX Test Gain"); i->classFlags = 0; std::strcpy(i->subCategories, "Fx|Tools"); std::strcpy(i->vendor, "AMFB Audio (test)"); std::strcpy(i->version, "1.0.0"); std::strcpy(i->sdkVersion, kVstVersionString); return kResultOk; }
  tresult PLUGIN_API createInstance(FIDString cid, FIDString iid, void** obj) override { if (!same(cid, kTestCid)) { *obj = nullptr; return kNoInterface; } TestGain* t = new TestGain(); const tresult r = t->queryInterface(iid, obj); t->release(); return r; }
 private:
  std::atomic<uint32> refs_{1};
};
}  // namespace

extern "C" {
SMTG_EXPORT_SYMBOL IPluginFactory* PLUGIN_API GetPluginFactory() { return new Factory(); }
#ifdef _WIN32
SMTG_EXPORT_SYMBOL bool InitDll() { return true; }
SMTG_EXPORT_SYMBOL bool ExitDll() { return true; }
#else
SMTG_EXPORT_SYMBOL bool ModuleEntry(void*) { return true; }
SMTG_EXPORT_SYMBOL bool ModuleExit() { return true; }
#endif
}
