// THE AMFB PLUG-IN SHELL: one VST3 binary that becomes whatever design sits in its bundle (Contents/Resources/design.json): a chain of
// AMFB effect blocks with the design's fixed settings, and the design's exposed knobs as automatable parameters, plus BYPASS. Without a
// design (the shell as shipped) it is "AMFB Plugin Shell", a clean pass-through. Written against the VST3 interfaces only (as the host's
// test plug-in): one class is component, processor and controller; the DAW shows its generic parameter editor.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "fx/fx.hpp"
#include "plugin_shell/design.hpp"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstunits.h"
#include "pluginterfaces/vst/vstspeaker.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {
bool same(const TUID a, const TUID b) { return std::memcmp(a, b, 16) == 0; }
void str16(const std::string& s, String128 out) { int n = 0; for (unsigned char c : s) { if (n >= 127) break; out[n++] = static_cast<TChar>(c); } out[n] = 0; }   // ASCII names (the builder keeps them so)
constexpr ParamID kBypassId = 1000;

// where this binary sits: <bundle>/Contents/<arch>/<name>.<ext>  ->  <bundle>/Contents/Resources/design.json
std::string modulePath() {
#ifdef _WIN32
  HMODULE h = nullptr; wchar_t buf[MAX_PATH] = {};
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&modulePath), &h)) return {};
  const DWORD n = GetModuleFileNameW(h, buf, MAX_PATH); std::string s; for (DWORD i = 0; i < n; ++i) s += buf[i] < 128 ? static_cast<char>(buf[i]) : '?';
  for (char& c : s) if (c == '\\') c = '/'; return s;
#else
  Dl_info di{}; return dladdr(reinterpret_cast<void*>(&modulePath), &di) && di.dli_fname ? std::string(di.dli_fname) : std::string();
#endif
}
const am::shell::Design& design() {
  static am::shell::Design d; static std::once_flag once;
  std::call_once(once, [] {
    std::string p = modulePath(); for (int up = 0; up < 2; ++up) { const auto s = p.find_last_of('/'); if (s != std::string::npos) p.resize(s); }   // -> <bundle>/Contents
    std::ifstream f(p + "/Resources/design.json", std::ios::binary); std::stringstream ss; ss << f.rdbuf(); std::string err;
    if (!f || !am::shell::parse(ss.str(), d, err)) { d = am::shell::Design{}; d.name = "AMFB Plugin Shell"; d.uid = "a3c1f00d5e7a4b219c0d1e2f30415299"; d.category = "Fx|Tools"; }
  });
  return d;
}
void uidOf(const am::shell::Design& d, TUID out) { for (int i = 0; i < 16; ++i) { unsigned v = 0; std::sscanf(d.uid.c_str() + 2 * i, "%2x", &v); out[i] = static_cast<char>(v); } }

class Shell : public IComponent, public IAudioProcessor, public IEditController {
 public:
  Shell() : d_(design()) { nv_ = d_.params.size(); values_.reset(new std::atomic<double>[nv_ ? nv_ : 1]); for (std::size_t i = 0; i < d_.params.size(); ++i) values_[i] = d_.params[i].def; build(48000); }
  virtual ~Shell() = default;
  tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
    if (same(iid, FUnknown_iid) || same(iid, IPluginBase_iid) || same(iid, IComponent_iid)) { addRef(); *obj = static_cast<IComponent*>(this); return kResultOk; }
    if (same(iid, IAudioProcessor_iid)) { addRef(); *obj = static_cast<IAudioProcessor*>(this); return kResultOk; }
    if (same(iid, IEditController_iid)) { addRef(); *obj = static_cast<IEditController*>(this); return kResultOk; }
    *obj = nullptr; return kNoInterface;
  }
  uint32 PLUGIN_API addRef() override { return ++refs_; }
  uint32 PLUGIN_API release() override { const uint32 r = --refs_; if (!r) delete this; return r; }
  tresult PLUGIN_API initialize(FUnknown*) override { return kResultOk; }
  tresult PLUGIN_API terminate() override { return kResultOk; }
  tresult PLUGIN_API getControllerClassId(TUID) override { return kResultFalse; }
  tresult PLUGIN_API setIoMode(IoMode) override { return kResultOk; }
  int32 PLUGIN_API getBusCount(MediaType t, BusDirection) override { return t == kAudio ? 1 : 0; }
  tresult PLUGIN_API getBusInfo(MediaType t, BusDirection dir, int32 i, BusInfo& b) override { if (t != kAudio || i != 0) return kResultFalse; b.mediaType = kAudio; b.direction = dir; b.channelCount = channels_; str16(dir == kInput ? "In" : "Out", b.name); b.busType = kMain; b.flags = BusInfo::kDefaultActive; return kResultOk; }
  tresult PLUGIN_API getRoutingInfo(RoutingInfo&, RoutingInfo&) override { return kResultFalse; }
  tresult PLUGIN_API activateBus(MediaType, BusDirection, int32, TBool) override { return kResultOk; }
  tresult PLUGIN_API setActive(TBool on) override { if (on) for (auto& e : fx_) e->reset(); return kResultOk; }
  // state: the exposed knobs and BYPASS, as floats
  tresult PLUGIN_API setState(IBStream* s) override {
    for (std::size_t i = 0; i < nv_; ++i) { float v = 0; int32 n = 0; if (s->read(&v, 4, &n) != kResultOk || n != 4) break; values_[i] = std::clamp(static_cast<double>(v), 0.0, 1.0); push(static_cast<int>(i)); }
    float b = 0; int32 n = 0; if (s->read(&b, 4, &n) == kResultOk && n == 4) bypass_ = b >= 0.5f; return kResultOk;
  }
  tresult PLUGIN_API getState(IBStream* s) override { for (std::size_t i = 0; i < nv_; ++i) { float f = static_cast<float>(values_[i].load()); s->write(&f, 4, nullptr); } float b = bypass_ ? 1.f : 0.f; return s->write(&b, 4, nullptr); }
  tresult PLUGIN_API setBusArrangements(SpeakerArrangement* in, int32 nIn, SpeakerArrangement* out, int32 nOut) override {
    if (nIn != 1 || nOut != 1 || in[0] != out[0]) return kResultFalse;
    if (in[0] == SpeakerArr::kStereo) { channels_ = 2; return kResultOk; } if (in[0] == SpeakerArr::kMono) { channels_ = 1; return kResultOk; } return kResultFalse;
  }
  tresult PLUGIN_API getBusArrangement(BusDirection, int32 i, SpeakerArrangement& a) override { if (i != 0) return kResultFalse; a = channels_ == 1 ? SpeakerArr::kMono : SpeakerArr::kStereo; return kResultOk; }
  tresult PLUGIN_API canProcessSampleSize(int32 s) override { return s == kSample32 ? kResultOk : kResultFalse; }
  uint32 PLUGIN_API getLatencySamples() override { return 0; }
  tresult PLUGIN_API setupProcessing(ProcessSetup& s) override { build(s.sampleRate); scratch_.assign(2 * static_cast<std::size_t>(std::max(1, s.maxSamplesPerBlock)), 0.f); return kResultOk; }   // allocation here, never in process()
  tresult PLUGIN_API setProcessing(TBool) override { return kResultOk; }
  tresult PLUGIN_API process(ProcessData& data) override {
    if (data.inputParameterChanges) for (int32 i = 0; i < data.inputParameterChanges->getParameterCount(); ++i) {
      IParamValueQueue* q = data.inputParameterChanges->getParameterData(i); if (!q || q->getPointCount() < 1) continue;
      int32 off = 0; ParamValue v = 0; q->getPoint(q->getPointCount() - 1, off, v); setParamNormalized(q->getParameterId(), v); }
    if (data.numOutputs < 1 || !data.outputs || data.numSamples <= 0) return kResultOk;
    const int32 n = data.numSamples, ch = data.outputs[0].numChannels;
    float* L = data.outputs[0].channelBuffers32[0]; float* R = ch > 1 ? data.outputs[0].channelBuffers32[1] : nullptr;
    const float* inL = data.numInputs > 0 && data.inputs && data.inputs[0].numChannels > 0 ? data.inputs[0].channelBuffers32[0] : nullptr;
    const float* inR = data.numInputs > 0 && data.inputs && data.inputs[0].numChannels > 1 ? data.inputs[0].channelBuffers32[1] : inL;
    for (int32 i = 0; i < n; ++i) { L[i] = inL ? inL[i] : 0.f; if (R) R[i] = inR ? inR[i] : 0.f; }
    if (bypass_) return kResultOk;
    float* r = R; if (!r) { if (scratch_.size() < static_cast<std::size_t>(n)) return kResultOk; r = scratch_.data(); std::memcpy(r, L, sizeof(float) * static_cast<std::size_t>(n)); }   // mono: the chain runs on a copy as the right side
    for (auto& e : fx_) e->process(L, r, static_cast<std::size_t>(n));
    return kResultOk;
  }
  uint32 PLUGIN_API getTailSamples() override { return kInfiniteTail; }   // reverbs and delays ring on
  tresult PLUGIN_API setComponentState(IBStream* s) override { return setState(s); }
  int32 PLUGIN_API getParameterCount() override { return static_cast<int32>(d_.params.size()) + 1; }
  tresult PLUGIN_API getParameterInfo(int32 i, ParameterInfo& p) override {
    if (i == static_cast<int32>(d_.params.size())) { p.id = kBypassId; str16("Bypass", p.title); str16("Byp", p.shortTitle); p.units[0] = 0; p.stepCount = 1; p.defaultNormalizedValue = 0; p.unitId = kRootUnitId; p.flags = ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass; return kResultOk; }
    if (i < 0 || i >= static_cast<int32>(d_.params.size())) return kResultFalse;
    const auto& pa = d_.params[static_cast<std::size_t>(i)]; p.id = static_cast<ParamID>(i); str16(pa.title, p.title); str16(pa.title.substr(0, 8), p.shortTitle); p.units[0] = 0;
    p.stepCount = 0; p.defaultNormalizedValue = pa.def; p.unitId = kRootUnitId; p.flags = ParameterInfo::kCanAutomate; return kResultOk;
  }
  tresult PLUGIN_API getParamStringByValue(ParamID id, ParamValue v, String128 s) override {
    if (id == kBypassId) { str16(v >= 0.5 ? "on" : "off", s); return kResultOk; }
    if (id >= d_.params.size()) return kResultFalse; const auto& pa = d_.params[id];
    str16(fx_.empty() ? std::to_string(v) : fx_[static_cast<std::size_t>(pa.block)]->text(pa.id, v), s); return kResultOk;   // the block's own wording: "250 ms", "+6.0 dB"
  }
  tresult PLUGIN_API getParamValueByString(ParamID, TChar*, ParamValue&) override { return kResultFalse; }
  ParamValue PLUGIN_API normalizedParamToPlain(ParamID, ParamValue v) override { return v; }
  ParamValue PLUGIN_API plainParamToNormalized(ParamID, ParamValue v) override { return v; }
  ParamValue PLUGIN_API getParamNormalized(ParamID id) override { if (id == kBypassId) return bypass_ ? 1.0 : 0.0; return id < nv_ ? values_[id].load() : 0.0; }
  tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue v) override {
    if (id == kBypassId) { bypass_ = v >= 0.5; return kResultOk; } if (id >= nv_) return kResultFalse;
    values_[id] = std::clamp(v, 0.0, 1.0); push(static_cast<int>(id)); return kResultOk;
  }
  tresult PLUGIN_API setComponentHandler(IComponentHandler*) override { return kResultOk; }
  IPlugView* PLUGIN_API createView(FIDString) override { return nullptr; }   // the DAW's own parameter editor shows the knobs
 private:
  void build(double fs) {   // the chain at this rate, the fixed settings, then the exposed knobs
    fx_.clear();
    for (const auto& b : d_.blocks) { auto e = am::fx::make(b.kind, fs); if (!e) continue; for (const auto& [k, v] : b.set) e->set(k, v); fx_.push_back(std::move(e)); }
    for (std::size_t i = 0; i < d_.params.size(); ++i) push(static_cast<int>(i));
  }
  void push(int i) { const auto& pa = d_.params[static_cast<std::size_t>(i)]; if (pa.block < static_cast<int>(fx_.size())) fx_[static_cast<std::size_t>(pa.block)]->set(pa.id, values_[static_cast<std::size_t>(i)].load()); }   // Effect::set is lock-free
  const am::shell::Design& d_; std::vector<std::unique_ptr<am::fx::Effect>> fx_; std::unique_ptr<std::atomic<double>[]> values_; std::size_t nv_ = 0;   // atomics: the DAW sets them from its threads, the audio thread reads
  std::vector<float> scratch_; std::atomic<uint32> refs_{1}; std::atomic<bool> bypass_{false}; int channels_ = 2;
};

class Factory : public IPluginFactory2 {
 public:
  virtual ~Factory() = default;
  tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override { if (same(iid, FUnknown_iid) || same(iid, IPluginFactory_iid) || same(iid, IPluginFactory2_iid)) { addRef(); *obj = static_cast<IPluginFactory2*>(this); return kResultOk; } *obj = nullptr; return kNoInterface; }
  uint32 PLUGIN_API addRef() override { return ++refs_; }
  uint32 PLUGIN_API release() override { const uint32 r = --refs_; if (!r) delete this; return r; }
  tresult PLUGIN_API getFactoryInfo(PFactoryInfo* i) override { *i = PFactoryInfo(); std::snprintf(i->vendor, sizeof i->vendor, "%s", design().vendor.c_str()); std::strcpy(i->url, "https://amfb.audio"); std::strcpy(i->email, "plugins@amfb.audio"); i->flags = PFactoryInfo::kUnicode; return kResultOk; }
  int32 PLUGIN_API countClasses() override { return 1; }
  tresult PLUGIN_API getClassInfo(int32 idx, PClassInfo* i) override { if (idx != 0) return kResultFalse; *i = PClassInfo(); uidOf(design(), i->cid); i->cardinality = PClassInfo::kManyInstances; std::strcpy(i->category, kVstAudioEffectClass); std::snprintf(i->name, sizeof i->name, "%s", design().name.c_str()); return kResultOk; }
  tresult PLUGIN_API getClassInfo2(int32 idx, PClassInfo2* i) override {
    if (idx != 0) return kResultFalse; const auto& d = design(); *i = PClassInfo2(); uidOf(d, i->cid); i->cardinality = PClassInfo::kManyInstances; std::strcpy(i->category, kVstAudioEffectClass);
    std::snprintf(i->name, sizeof i->name, "%s", d.name.c_str()); i->classFlags = 0; std::snprintf(i->subCategories, sizeof i->subCategories, "%s", d.category.c_str());
    std::snprintf(i->vendor, sizeof i->vendor, "%s", d.vendor.c_str()); std::snprintf(i->version, sizeof i->version, "%s", d.version.c_str()); std::strcpy(i->sdkVersion, kVstVersionString); return kResultOk; }
  tresult PLUGIN_API createInstance(FIDString cid, FIDString iid, void** obj) override { TUID mine; uidOf(design(), mine); if (!same(cid, mine)) { *obj = nullptr; return kNoInterface; } Shell* s = new Shell(); const tresult r = s->queryInterface(iid, obj); s->release(); return r; }
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
