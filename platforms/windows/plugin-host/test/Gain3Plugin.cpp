// Gain3Plugin - a tiny VST 3 effect (stereo in / stereo out, one parameter "Gain": 0.5 = unity, 1.0 = +6 dB) used to test PluginHost's VST 3 support.
// Written from the same public interface layout as the host (../src/Vst3Host.h): the host is exercised against a plugin that was not written by the host's author's loader.
//   Linux:    g++ -shared -fPIC -std=c++11 -o gain3.so Gain3Plugin.cpp           (put it in  gain3.vst3/Contents/x86_64-linux/gain3.so  or pass the .so itself)
//   Windows:  x86_64-w64-mingw32-g++ -shared -static -std=c++11 -o gain3.vst3 Gain3Plugin.cpp
#include "../src/Vst3Host.h"
#include <cmath>
using namespace v3;

static const TUID kPluginBase = V3UID(0x22888DDB, 0x156E45AE, 0x8358B348, 0x08190625);
static const TUID kPluginFactoryId = V3UID(0x7A4D811C, 0x52114A1F, 0xAED9D2EE, 0x0B43BF9F);
static const char kClassId[16] = { 'G', 'a', 'i', 'n', '3', 'T', 'e', 's', 't', 'P', 'l', 'u', 'g', 'i', 'n', '!' };

struct Gain3 : IComponent, IAudioProcessor, IEditController {
  double gain = 0.5; bool active = false;
  uint32_t V3CALL addRef() override { return 1; }
  uint32_t V3CALL release() override { return 1; }
  tresult V3CALL queryInterface(const char* iid, void** obj) override {
    if (same(iid, kFUnknown) || same(iid, kPluginBase) || same(iid, kComponent)) { *obj = static_cast<IComponent*>(this); return kOk; }
    if (same(iid, kAudioProcessor)) { *obj = static_cast<IAudioProcessor*>(this); return kOk; }
    if (same(iid, kEditController)) { *obj = static_cast<IEditController*>(this); return kOk; }
    *obj = nullptr; return kNoInterface;
  }
  // IPluginBase (the same two methods answer for the component and the controller)
  tresult V3CALL initialize(FUnknown*) override { return kOk; }
  tresult V3CALL terminate() override { return kOk; }
  // IComponent
  tresult V3CALL getControllerClassId(char*) override { return kNotImplemented; }           // the component is its own controller
  tresult V3CALL setIoMode(int32_t) override { return kOk; }
  int32_t V3CALL getBusCount(int32_t type, int32_t) override { return type == 0 ? 1 : 0; }
  tresult V3CALL getBusInfo(int32_t type, int32_t dir, int32_t index, BusInfo& bus) override {
    if (type != 0 || index != 0) return kNoInterface;
    memset(&bus, 0, sizeof bus); bus.mediaType = 0; bus.direction = dir; bus.channelCount = 2; bus.busType = 0; bus.flags = 1;
    const char* n = dir == 0 ? "Input" : "Output"; for (int i = 0; n[i]; i++) bus.name[i] = (TChar)n[i];
    return kOk;
  }
  tresult V3CALL getRoutingInfo(RoutingInfo&, RoutingInfo&) override { return kNotImplemented; }
  tresult V3CALL activateBus(int32_t, int32_t, int32_t, uint8_t) override { return kOk; }
  tresult V3CALL setActive(uint8_t s) override { active = s != 0; return kOk; }
  tresult V3CALL setState(IBStream*) override { return kOk; }
  tresult V3CALL getState(IBStream*) override { return kOk; }
  // IAudioProcessor
  tresult V3CALL setBusArrangements(uint64_t* in, int32_t nIn, uint64_t* out, int32_t nOut) override { return nIn == 1 && nOut == 1 && in[0] == 3 && out[0] == 3 ? kOk : 1; }
  tresult V3CALL getBusArrangement(int32_t, int32_t, uint64_t& arr) override { arr = 3; return kOk; }
  tresult V3CALL canProcessSampleSize(int32_t s) override { return s == 0 ? kOk : kNoInterface; }
  uint32_t V3CALL getLatencySamples() override { return 0; }
  tresult V3CALL setupProcessing(ProcessSetup&) override { return kOk; }
  tresult V3CALL setProcessing(uint8_t) override { return kOk; }
  tresult V3CALL process(ProcessData& d) override {
    if (d.inputParameterChanges) for (int32_t i = 0; i < d.inputParameterChanges->getParameterCount(); i++) {
      IParamValueQueue* q = d.inputParameterChanges->getParameterData(i);
      if (q && q->getParameterId() == 7 && q->getPointCount() > 0) { int32_t off; double v; if (q->getPoint(q->getPointCount() - 1, off, v) == kOk) gain = v; }
    }
    if (d.numInputs < 1 || d.numOutputs < 1 || !d.inputs[0].channelBuffers32 || !d.outputs[0].channelBuffers32) return kOk;
    for (int c = 0; c < 2 && c < d.inputs[0].numChannels && c < d.outputs[0].numChannels; c++)
      for (int i = 0; i < d.numSamples; i++) d.outputs[0].channelBuffers32[c][i] = (float)(d.inputs[0].channelBuffers32[c][i] * gain * 2.0);
    return kOk;
  }
  uint32_t V3CALL getTailSamples() override { return 0; }
  // IEditController
  tresult V3CALL setComponentState(IBStream*) override { return kOk; }
  int32_t V3CALL getParameterCount() override { return 1; }
  tresult V3CALL getParameterInfo(int32_t i, ParameterInfo& p) override {
    if (i != 0) return kNoInterface;
    memset(&p, 0, sizeof p); p.id = 7; const char* t = "Gain"; for (int k = 0; t[k]; k++) { p.title[k] = (TChar)t[k]; p.shortTitle[k] = (TChar)t[k]; } p.units[0] = 'x'; p.defaultNormalizedValue = 0.5; p.flags = 1;
    return kOk;
  }
  tresult V3CALL getParamStringByValue(uint32_t, double v, TChar* s) override { char b[32]; snprintf(b, sizeof b, "%.2f", v * 2); int k = 0; for (; b[k]; k++) s[k] = (TChar)b[k]; s[k] = 0; return kOk; }
  tresult V3CALL getParamValueByString(uint32_t, TChar*, double&) override { return kNotImplemented; }
  double V3CALL normalizedParamToPlain(uint32_t, double v) override { return v * 2; }
  double V3CALL plainParamToNormalized(uint32_t, double v) override { return v / 2; }
  double V3CALL getParamNormalized(uint32_t) override { return gain; }
  tresult V3CALL setParamNormalized(uint32_t, double v) override { gain = v; return kOk; }
  tresult V3CALL setComponentHandler(IComponentHandler*) override { return kOk; }
  void* V3CALL createView(const char*) override { return nullptr; }
};

struct Factory : IPluginFactory2 {
  uint32_t V3CALL addRef() override { return 1; }
  uint32_t V3CALL release() override { return 1; }
  tresult V3CALL queryInterface(const char* iid, void** obj) override {
    if (same(iid, kFUnknown) || same(iid, kPluginFactoryId) || same(iid, kPluginFactory2)) { *obj = this; return kOk; }
    *obj = nullptr; return kNoInterface;
  }
  tresult V3CALL getFactoryInfo(PFactoryInfo* i) override { memset(i, 0, sizeof *i); strcpy(i->vendor, "Audio Mixer tests"); return kOk; }
  int32_t V3CALL countClasses() override { return 2; }
  tresult V3CALL getClassInfo(int32_t index, PClassInfo* c) override {
    if (index < 0 || index > 1) return kNoInterface;
    memset(c, 0, sizeof *c); memcpy(c->cid, kClassId, 16); c->cardinality = 0x7FFFFFFF; strcpy(c->category, "Audio Module Class"); strcpy(c->name, index == 0 ? "Test Gain Synth" : "Test Gain 3"); return kOk;
  }
  tresult V3CALL getClassInfo2(int32_t index, PClassInfo2* c) override {
    if (index < 0 || index > 1) return kNoInterface;
    memset(c, 0, sizeof *c); memcpy(c->cid, kClassId, 16); if (index == 0) c->cid[15] = '?'; c->cardinality = 0x7FFFFFFF; strcpy(c->category, "Audio Module Class");
    strcpy(c->name, index == 0 ? "Test Gain Synth" : "Test Gain 3"); strcpy(c->subCategories, index == 0 ? "Instrument|Synth" : "Fx"); strcpy(c->vendor, "Audio Mixer tests"); return kOk;
  }
  tresult V3CALL createInstance(const char* cid, const char* iid, void** obj) override {
    static Gain3 plug;
    if (memcmp(cid, kClassId, 16) != 0) { *obj = nullptr; return kNoInterface; }
    return plug.queryInterface(iid, obj);
  }
};

#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C" __attribute__((visibility("default")))
#endif
EXPORT IPluginFactory* V3CALL GetPluginFactory() { static Factory f; return &f; }
EXPORT bool ModuleEntry(void*) { return true; }
EXPORT bool ModuleExit() { return true; }
