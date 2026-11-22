#include "vst3_host.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/vstspeaker.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace am::vst3 {

namespace {
// ---------------------------------------------------------------- small helpers
bool sameIid(const TUID a, const TUID b) { return std::memcmp(a, b, 16) == 0; }
std::string hexOf(const TUID id) { static const char* h = "0123456789ABCDEF"; std::string s; for (int i = 0; i < 16; ++i) { const unsigned char c = static_cast<unsigned char>(id[i]); s += h[c >> 4]; s += h[c & 15]; } return s; }
bool hexTo(const std::string& s, TUID out) { if (s.size() != 32) return false; for (int i = 0; i < 16; ++i) { unsigned v = 0; for (int k = 0; k < 2; ++k) { const char c = s[static_cast<std::size_t>(2 * i + k)]; v <<= 4; if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0'); else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10); else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10); else return false; } out[i] = static_cast<char>(v); } return true; }
std::string utf16(const TChar* s) { std::string o; for (int i = 0; i < 128 && s[i]; ++i) { const unsigned c = static_cast<unsigned>(s[i]) & 0xffff; if (c < 0x80) o += static_cast<char>(c); else if (c < 0x800) { o += static_cast<char>(0xC0 | (c >> 6)); o += static_cast<char>(0x80 | (c & 0x3F)); } else { o += static_cast<char>(0xE0 | (c >> 12)); o += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); o += static_cast<char>(0x80 | (c & 0x3F)); } } return o; }
void toUtf16(const char* s, String128 out) { int n = 0; while (*s && n < 127) out[n++] = static_cast<TChar>(static_cast<unsigned char>(*s++)); out[n] = 0; }
bool exists(const std::string& p) {
#ifdef _WIN32
  return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES;
#else
  struct stat st{}; return ::stat(p.c_str(), &st) == 0;
#endif
}
bool isDir(const std::string& p) {
#ifdef _WIN32
  const DWORD a = GetFileAttributesA(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
  struct stat st{}; return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}
std::vector<std::string> listDir(const std::string& d) {
  std::vector<std::string> out;
#ifdef _WIN32
  WIN32_FIND_DATAA f; HANDLE h = FindFirstFileA((d + "\\*").c_str(), &f); if (h == INVALID_HANDLE_VALUE) return out;
  do { const std::string n = f.cFileName; if (n != "." && n != "..") out.push_back(d + "/" + n); } while (FindNextFileA(h, &f)); FindClose(h);
#else
  DIR* dir = ::opendir(d.c_str()); if (!dir) return out; while (dirent* e = ::readdir(dir)) { const std::string n = e->d_name; if (n != "." && n != "..") out.push_back(d + "/" + n); } ::closedir(dir);
#endif
  std::sort(out.begin(), out.end()); return out;
}
std::string baseName(const std::string& p) { const std::size_t s = p.find_last_of("/\\"); std::string b = s == std::string::npos ? p : p.substr(s + 1); if (b.size() > 5 && b.compare(b.size() - 5, 5, ".vst3") == 0) b.resize(b.size() - 5); return b; }

// base64 for the state blob
std::string b64(const std::vector<char>& d) { static const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; std::string o; unsigned acc = 0; int bits = 0; for (char ch : d) { acc = (acc << 8) | static_cast<unsigned char>(ch); bits += 8; while (bits >= 6) { bits -= 6; o += a[(acc >> bits) & 63]; } } if (bits) o += a[(acc << (6 - bits)) & 63]; while (o.size() % 4) o += '='; return o; }
std::vector<char> unb64(const std::string& s) { std::vector<char> o; unsigned acc = 0; int bits = 0; for (char c : s) { int v; if (c >= 'A' && c <= 'Z') v = c - 'A'; else if (c >= 'a' && c <= 'z') v = c - 'a' + 26; else if (c >= '0' && c <= '9') v = c - '0' + 52; else if (c == '+' || c == '-') v = 62; else if (c == '/' || c == '_') v = 63; else continue; acc = (acc << 6) | static_cast<unsigned>(v); bits += 6; if (bits >= 8) { bits -= 8; o.push_back(static_cast<char>((acc >> bits) & 0xff)); } } return o; }

// ---------------------------------------------------------------- host-side objects (manual FUnknown)
template <class I> struct Unknown : I {
  virtual ~Unknown() = default;
  std::atomic<uint32> refs{1};
  uint32 PLUGIN_API addRef() override { return ++refs; }
  uint32 PLUGIN_API release() override { const uint32 r = --refs; if (r == 0) delete this; return r; }
};
struct HostApp final : Unknown<IHostApplication> {
  tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override { if (sameIid(iid, FUnknown_iid) || sameIid(iid, IHostApplication_iid)) { addRef(); *obj = static_cast<IHostApplication*>(this); return kResultOk; } *obj = nullptr; return kNoInterface; }
  tresult PLUGIN_API getName(String128 name) override { toUtf16("A mix OS", name); return kResultOk; }
  tresult PLUGIN_API createInstance(TUID, TUID, void** obj) override { *obj = nullptr; return kNoInterface; }   // no IMessage / IAttributeList: the console never relays messages between the two halves
};
struct Handler final : Unknown<IComponentHandler> {
  std::atomic<bool>* restart = nullptr;
  tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override { if (sameIid(iid, FUnknown_iid) || sameIid(iid, IComponentHandler_iid)) { addRef(); *obj = static_cast<IComponentHandler*>(this); return kResultOk; } *obj = nullptr; return kNoInterface; }
  tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
  tresult PLUGIN_API performEdit(ParamID, ParamValue) override { return kResultOk; }   // the plug-in's own GUI is never opened, so nothing arrives here in practice
  tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
  tresult PLUGIN_API restartComponent(int32) override { if (restart) restart->store(true); return kResultOk; }
};
struct MemStream final : Unknown<IBStream> {
  std::vector<char> d; int64 pos = 0;
  tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override { if (sameIid(iid, FUnknown_iid) || sameIid(iid, IBStream_iid)) { addRef(); *obj = static_cast<IBStream*>(this); return kResultOk; } *obj = nullptr; return kNoInterface; }
  tresult PLUGIN_API read(void* buf, int32 n, int32* got) override { const int64 avail = static_cast<int64>(d.size()) - pos; const int32 k = static_cast<int32>(std::max<int64>(0, std::min<int64>(n, avail))); if (k > 0) std::memcpy(buf, d.data() + pos, static_cast<std::size_t>(k)); pos += k; if (got) *got = k; return kResultOk; }
  tresult PLUGIN_API write(void* buf, int32 n, int32* put) override { if (n < 0) return kInvalidArgument; if (pos + n > static_cast<int64>(d.size())) d.resize(static_cast<std::size_t>(pos + n)); std::memcpy(d.data() + pos, buf, static_cast<std::size_t>(n)); pos += n; if (put) *put = n; return kResultOk; }
  tresult PLUGIN_API seek(int64 p, int32 mode, int64* res) override { const int64 base = mode == kIBSeekSet ? 0 : mode == kIBSeekCur ? pos : static_cast<int64>(d.size()); pos = std::max<int64>(0, base + p); if (res) *res = pos; return kResultOk; }
  tresult PLUGIN_API tell(int64* p) override { if (p) *p = pos; return kResultOk; }
};
struct Queue final : Unknown<IParamValueQueue> {
  ParamID id = 0; int32 offset = 0; ParamValue value = 0; bool has = false;
  tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override { if (sameIid(iid, FUnknown_iid) || sameIid(iid, IParamValueQueue_iid)) { addRef(); *obj = static_cast<IParamValueQueue*>(this); return kResultOk; } *obj = nullptr; return kNoInterface; }
  ParamID PLUGIN_API getParameterId() override { return id; }
  int32 PLUGIN_API getPointCount() override { return has ? 1 : 0; }
  tresult PLUGIN_API getPoint(int32 i, int32& off, ParamValue& v) override { if (i != 0 || !has) return kResultFalse; off = offset; v = value; return kResultOk; }
  tresult PLUGIN_API addPoint(int32 off, ParamValue v, int32& i) override { offset = off; value = v; has = true; i = 0; return kResultOk; }
};
// one block's parameter changes: a fixed set of queues, filled on the audio thread from the lock-free ring (no allocation while processing)
struct Changes final : Unknown<IParameterChanges> {
  static constexpr int kMax = 64; Queue q[kMax]; int n = 0;
  tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override { if (sameIid(iid, FUnknown_iid) || sameIid(iid, IParameterChanges_iid)) { addRef(); *obj = static_cast<IParameterChanges*>(this); return kResultOk; } *obj = nullptr; return kNoInterface; }
  int32 PLUGIN_API getParameterCount() override { return n; }
  IParamValueQueue* PLUGIN_API getParameterData(int32 i) override { return i >= 0 && i < n ? &q[i] : nullptr; }
  IParamValueQueue* PLUGIN_API addParameterData(const ParamID& id, int32& index) override { for (int i = 0; i < n; ++i) if (q[i].id == id) { index = i; return &q[i]; } if (n >= kMax) { index = -1; return nullptr; } q[n].id = id; q[n].has = false; index = n; return &q[n++]; }
  void clear() { n = 0; }
};

// ---------------------------------------------------------------- module loading
struct Module {
  void* handle = nullptr; IPluginFactory* factory = nullptr; std::string path;
  ~Module() {
    if (factory) factory->release();
    if (handle) {
#ifdef _WIN32
      if (FARPROC f = GetProcAddress(static_cast<HMODULE>(handle), "ExitDll")) { using Fn = bool (*)(); Fn exitDll = nullptr; std::memcpy(&exitDll, &f, sizeof f); exitDll(); }
      FreeLibrary(static_cast<HMODULE>(handle));
#else
      if (auto moduleExit = reinterpret_cast<bool (*)()>(dlsym(handle, "ModuleExit"))) moduleExit();
      dlclose(handle);
#endif
    }
  }
};
std::unique_ptr<Module> openModule(const std::string& modulePath, std::string& err) {
  const std::string bin = binaryOf(modulePath); if (bin.empty()) { err = "no binary for this platform inside " + modulePath; return nullptr; }
  auto m = std::make_unique<Module>(); m->path = modulePath;
#ifdef _WIN32
  m->handle = LoadLibraryA(bin.c_str()); if (!m->handle) { char b[64]; std::snprintf(b, sizeof b, " (error %lu)", GetLastError()); err = "cannot load " + bin + b; return nullptr; }
  if (FARPROC f = GetProcAddress(static_cast<HMODULE>(m->handle), "InitDll")) { using Fn = bool (*)(); Fn initDll = nullptr; std::memcpy(&initDll, &f, sizeof f); if (!initDll()) { err = "InitDll refused in " + bin; return nullptr; } }
  using GetFn = IPluginFactory* (PLUGIN_API*)(); GetFn get = nullptr; if (FARPROC f = GetProcAddress(static_cast<HMODULE>(m->handle), "GetPluginFactory")) std::memcpy(&get, &f, sizeof f);
#else
  m->handle = dlopen(bin.c_str(), RTLD_LAZY | RTLD_LOCAL); if (!m->handle) { const char* e = dlerror(); err = std::string("cannot load ") + bin + ": " + (e ? e : "?"); return nullptr; }
  if (auto moduleEntry = reinterpret_cast<bool (*)(void*)>(dlsym(m->handle, "ModuleEntry"))) if (!moduleEntry(m->handle)) { err = "ModuleEntry refused in " + bin; return nullptr; }
  auto get = reinterpret_cast<IPluginFactory* (PLUGIN_API*)()>(dlsym(m->handle, "GetPluginFactory"));
#endif
  if (!get) { err = bin + " has no GetPluginFactory: not a VST3 module"; return nullptr; }
  m->factory = get(); if (!m->factory) { err = "GetPluginFactory returned nothing in " + bin; return nullptr; }
  return m;
}
std::vector<ClassInfo> classesOf(IPluginFactory* f) {
  std::vector<ClassInfo> out; IPluginFactory2* f2 = nullptr; f->queryInterface(IPluginFactory2_iid, reinterpret_cast<void**>(&f2));
  PFactoryInfo fi{}; f->getFactoryInfo(&fi);
  for (int32 i = 0; i < f->countClasses(); ++i) {
    ClassInfo c; PClassInfo ci{};
    if (f2) { PClassInfo2 c2{}; if (f2->getClassInfo2(i, &c2) != kResultOk) continue; if (std::strcmp(c2.category, kVstAudioEffectClass) != 0) continue; c.cid = hexOf(c2.cid); c.name = c2.name; c.vendor = c2.vendor[0] ? c2.vendor : fi.vendor; c.version = c2.version; c.category = c2.category; c.subCategories = c2.subCategories; }
    else { if (f->getClassInfo(i, &ci) != kResultOk) continue; if (std::strcmp(ci.category, kVstAudioEffectClass) != 0) continue; c.cid = hexOf(ci.cid); c.name = ci.name; c.vendor = fi.vendor; c.category = ci.category; }
    out.push_back(c);
  }
  if (f2) f2->release();
  return out;
}
}  // namespace

// ---------------------------------------------------------------- public: scanning
std::string binaryOf(const std::string& p) {
  const std::string name = baseName(p);
#ifdef _WIN32
  if (!isDir(p)) return exists(p) ? p : "";                                              // a single-file .vst3 (a DLL)
  for (const char* arch : {"x86_64-win", "x86-win", "arm64-win"}) { const std::string b = p + "/Contents/" + arch + "/" + name + ".vst3"; if (exists(b)) return b; }
  return "";
#elif defined(__APPLE__)
  (void)name; return "";                                                               // macOS bundles need CFBundle loading: not yet
#else
  if (!isDir(p)) return "";
  for (const char* arch : {"x86_64-linux", "aarch64-linux", "i386-linux"}) { const std::string b = p + "/Contents/" + arch + "/" + name + ".so"; if (exists(b)) return b; }
  return "";
#endif
}
std::vector<std::string> defaultDirs() {
  std::vector<std::string> out;
#ifdef _WIN32
  if (const char* pf = std::getenv("ProgramFiles")) out.push_back(std::string(pf) + "/Common Files/VST3");
  if (const char* la = std::getenv("LOCALAPPDATA")) out.push_back(std::string(la) + "/Programs/Common/VST3");
#elif defined(__APPLE__)
  out.push_back("/Library/Audio/Plug-Ins/VST3"); if (const char* h = std::getenv("HOME")) out.push_back(std::string(h) + "/Library/Audio/Plug-Ins/VST3");
#else
  if (const char* h = std::getenv("HOME")) out.push_back(std::string(h) + "/.vst3");
  out.push_back("/usr/lib/vst3"); out.push_back("/usr/local/lib/vst3");
#endif
  std::vector<std::string> ok; for (const auto& d : out) if (isDir(d)) ok.push_back(d);
  return ok;
}
// Sub-folders are searched too (the VST3 folder is normally sorted by maker), four levels deep; a .vst3 bundle itself is not entered.
static void walk3(const std::string& d, int depth, std::vector<std::string>& out) {
  for (const auto& e : listDir(d)) {
    if (e.size() > 5 && e.compare(e.size() - 5, 5, ".vst3") == 0) { if (!binaryOf(e).empty()) out.push_back(e); continue; }
    if (depth < 4) walk3(e, depth + 1, out);
  }
}
std::vector<std::string> scan(const std::vector<std::string>& dirs) {
  std::vector<std::string> out;
  for (const auto& d : dirs) walk3(d, 0, out);
  std::sort(out.begin(), out.end()); out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}
bool inspect(const std::string& modulePath, std::vector<ClassInfo>& classes, std::string& err) {
  auto m = openModule(modulePath, err); if (!m) return false; classes = classesOf(m->factory); if (classes.empty()) { err = "no Audio Module class in " + modulePath; return false; } return true;
}

// ---------------------------------------------------------------- the plug-in instance
}  // namespace am::vst3
// The editor interface of VST3 (pluginterfaces/gui/iplugview.h is not among the interface headers kept in this repository, so the two types
// the host needs are declared here, in the order the plug-in's own table has them).
namespace Steinberg {
struct ViewRect { int32 left = 0, top = 0, right = 0, bottom = 0; };
class IPlugFrame;
class IPlugView : public FUnknown {
 public:
  virtual tresult PLUGIN_API isPlatformTypeSupported(FIDString type) = 0;
  virtual tresult PLUGIN_API attached(void* parent, FIDString type) = 0;
  virtual tresult PLUGIN_API removed() = 0;
  virtual tresult PLUGIN_API onWheel(float distance) = 0;
  virtual tresult PLUGIN_API onKeyDown(char16 key, int16 keyCode, int16 modifiers) = 0;
  virtual tresult PLUGIN_API onKeyUp(char16 key, int16 keyCode, int16 modifiers) = 0;
  virtual tresult PLUGIN_API getSize(ViewRect* size) = 0;
  virtual tresult PLUGIN_API onSize(ViewRect* newSize) = 0;
  virtual tresult PLUGIN_API onFocus(TBool state) = 0;
  virtual tresult PLUGIN_API setFrame(IPlugFrame* frame) = 0;
  virtual tresult PLUGIN_API canResize() = 0;
  virtual tresult PLUGIN_API checkSizeConstraint(ViewRect* rect) = 0;
};
}  // namespace Steinberg
namespace am::vst3 {
#ifdef _WIN32
static const char* const kEditorWindowType = "HWND";
#elif defined(__APPLE__)
static const char* const kEditorWindowType = "NSView";
#else
static const char* const kEditorWindowType = "X11EmbedWindowID";
#endif
struct Plugin::Impl {
  Steinberg::IPlugView* view = nullptr; bool viewChecked = false, viewOk = false;
  std::unique_ptr<Module> module; IComponent* comp = nullptr; IAudioProcessor* proc = nullptr; IEditController* ctrl = nullptr; bool ctrlIsComp = false;
  HostApp* host = nullptr; Handler* handler = nullptr; std::atomic<bool> restart{false};
  IConnectionPoint* cpComp = nullptr; IConnectionPoint* cpCtrl = nullptr;
  int maxBlock = 256; double rate = 48000; bool processing = false;
  // UI -> audio parameter ring (single producer, single consumer)
  struct Change { ParamID id; ParamValue v; }; static constexpr unsigned kRing = 256; Change ring[kRing]; std::atomic<unsigned> head{0}, tail{0};
  Changes* changes = nullptr; AudioBusBuffers inBus{}, outBus{}; std::vector<float*> inPtrs, outPtrs; std::vector<std::vector<float>> silence;
  std::mutex ctrlMutex;   // controller calls from the UI thread
  ~Impl() {
    if (view) { view->removed(); view->release(); view = nullptr; }
    if (proc && processing) proc->setProcessing(false);
    if (comp) comp->setActive(false);
    if (cpComp && cpCtrl) { cpComp->disconnect(cpCtrl); cpCtrl->disconnect(cpComp); }
    if (cpComp) cpComp->release();
    if (cpCtrl) cpCtrl->release();
    if (ctrl) { ctrl->setComponentHandler(nullptr); if (!ctrlIsComp) ctrl->terminate(); ctrl->release(); }
    if (proc) proc->release();
    if (comp) { comp->terminate(); comp->release(); }
    if (changes) changes->release();
    if (handler) handler->release();
    if (host) host->release();
    module.reset();
  }
};

std::unique_ptr<Plugin> Plugin::load(const std::string& modulePath, const std::string& cidHex, double rate, int maxBlock, std::string& err) {
  auto m = openModule(modulePath, err); if (!m) return nullptr;
  const std::vector<ClassInfo> classes = classesOf(m->factory); if (classes.empty()) { err = "no Audio Module class in " + modulePath; return nullptr; }
  const ClassInfo* pick = &classes[0]; if (!cidHex.empty()) { pick = nullptr; for (const auto& c : classes) if (c.cid == cidHex) pick = &c; if (!pick) { err = "class " + cidHex + " is not in " + modulePath; return nullptr; } }
  TUID cid; hexTo(pick->cid, cid);
  std::unique_ptr<Plugin> p(new Plugin()); p->impl_ = std::make_unique<Impl>(); Impl& I = *p->impl_; I.module = std::move(m); I.rate = rate; I.maxBlock = std::max(16, maxBlock); p->info_ = *pick; p->path_ = modulePath; p->hinfo_.format = "VST3"; p->hinfo_.name = pick->name; p->hinfo_.vendor = pick->vendor; p->hinfo_.version = pick->version; p->hinfo_.category = pick->category; p->hinfo_.subCategories = pick->subCategories; p->hinfo_.uid = pick->cid;
  I.host = new HostApp(); I.handler = new Handler(); I.handler->restart = &I.restart; I.changes = new Changes();
  if (I.module->factory->createInstance(cid, IComponent_iid, reinterpret_cast<void**>(&I.comp)) != kResultOk || !I.comp) { err = pick->name + ": the component could not be created"; return nullptr; }
  if (I.comp->initialize(I.host) != kResultOk) { err = pick->name + ": initialize failed"; return nullptr; }
  if (I.comp->queryInterface(IAudioProcessor_iid, reinterpret_cast<void**>(&I.proc)) != kResultOk || !I.proc) { err = pick->name + ": no IAudioProcessor"; return nullptr; }
  // the controller: inside the component (single-component plug-ins) or a second class
  if (I.comp->queryInterface(IEditController_iid, reinterpret_cast<void**>(&I.ctrl)) == kResultOk && I.ctrl) I.ctrlIsComp = true;
  else { TUID ccid; if (I.comp->getControllerClassId(ccid) == kResultOk && I.module->factory->createInstance(ccid, IEditController_iid, reinterpret_cast<void**>(&I.ctrl)) == kResultOk && I.ctrl) { if (I.ctrl->initialize(I.host) != kResultOk) { I.ctrl->release(); I.ctrl = nullptr; } } }
  if (I.ctrl) {
    I.ctrl->setComponentHandler(I.handler);
    if (!I.ctrlIsComp) {   // connect the halves; then hand the component state to the controller so its parameters show the real values
      I.comp->queryInterface(IConnectionPoint_iid, reinterpret_cast<void**>(&I.cpComp)); I.ctrl->queryInterface(IConnectionPoint_iid, reinterpret_cast<void**>(&I.cpCtrl));
      if (I.cpComp && I.cpCtrl) { I.cpComp->connect(I.cpCtrl); I.cpCtrl->connect(I.cpComp); }
      MemStream* ms = new MemStream(); if (I.comp->getState(ms) == kResultOk) { ms->pos = 0; I.ctrl->setComponentState(ms); } ms->release();
    }
  }
  // buses: the first main audio in / out, mono if the plug-in takes it, else stereo
  const int32 nIn = I.comp->getBusCount(kAudio, kInput), nOut = I.comp->getBusCount(kAudio, kOutput);
  if (nOut < 1) { err = pick->name + ": no audio output bus"; return nullptr; }
  SpeakerArrangement want = SpeakerArr::kMono; std::vector<SpeakerArrangement> ins(static_cast<std::size_t>(std::max<int32>(nIn, 0)), want), outs(static_cast<std::size_t>(nOut), want);
  auto tryArr = [&](SpeakerArrangement a) { for (auto& x : ins) x = a; for (auto& x : outs) x = a; return I.proc->setBusArrangements(ins.empty() ? nullptr : ins.data(), nIn, outs.data(), nOut) == kResultOk; };
  if (!tryArr(SpeakerArr::kMono)) tryArr(SpeakerArr::kStereo);
  SpeakerArrangement aIn = 0, aOut = 0; if (nIn > 0) I.proc->getBusArrangement(kInput, 0, aIn); I.proc->getBusArrangement(kOutput, 0, aOut);
  auto chOf = [](SpeakerArrangement a) { int n = 0; for (int b = 0; b < 64; ++b) if ((a >> b) & 1) ++n; return n; };
  p->inCh_ = nIn > 0 ? chOf(aIn) : 0; p->outCh_ = chOf(aOut);
  if (p->outCh_ < 1 || p->outCh_ > 2 || p->inCh_ > 2) { err = pick->name + ": needs " + std::to_string(p->inCh_) + " in / " + std::to_string(p->outCh_) + " out channels; the console inserts take mono or stereo"; return nullptr; }
  if (I.proc->canProcessSampleSize(kSample32) != kResultOk) { err = pick->name + ": no 32-bit float processing"; return nullptr; }
  ProcessSetup setup{}; setup.processMode = kRealtime; setup.symbolicSampleSize = kSample32; setup.maxSamplesPerBlock = I.maxBlock; setup.sampleRate = rate;
  if (I.proc->setupProcessing(setup) != kResultOk) { err = pick->name + ": setupProcessing refused " + std::to_string(static_cast<int>(rate)) + " Hz / " + std::to_string(I.maxBlock) + " frames"; return nullptr; }
  for (int32 i = 0; i < nIn; ++i) I.comp->activateBus(kAudio, kInput, i, i == 0);
  for (int32 i = 0; i < nOut; ++i) I.comp->activateBus(kAudio, kOutput, i, i == 0);
  // buffers for the other (inactive) buses must still exist: silence
  I.silence.assign(2, std::vector<float>(static_cast<std::size_t>(I.maxBlock), 0.f)); I.inPtrs.assign(2, I.silence[0].data()); I.outPtrs.assign(2, I.silence[1].data());
  if (I.comp->setActive(true) != kResultOk) { err = pick->name + ": setActive failed"; return nullptr; }
  if (I.proc->setProcessing(true) != kResultOk) { /* optional for some plug-ins */ } I.processing = true;
  p->latency_ = I.proc->getLatencySamples();
  // the bypass parameter, when the plug-in has one
  if (I.ctrl) for (int32 i = 0; i < I.ctrl->getParameterCount(); ++i) { ParameterInfo pi{}; if (I.ctrl->getParameterInfo(i, pi) == kResultOk && (pi.flags & ParameterInfo::kIsBypass)) { p->bypassId_ = pi.id; p->bypassOn_ = I.ctrl->getParamNormalized(pi.id) >= 0.5; break; } }
  return p;
}
Plugin::~Plugin() = default;

std::vector<ParamInfo> Plugin::params() const {
  std::vector<ParamInfo> out; Impl& I = *impl_; if (!I.ctrl) return out; std::lock_guard<std::mutex> g(I.ctrlMutex);
  for (int32 i = 0; i < I.ctrl->getParameterCount(); ++i) { ParameterInfo pi{}; if (I.ctrl->getParameterInfo(i, pi) != kResultOk) continue; ParamInfo x; x.id = pi.id; x.title = utf16(pi.title); x.units = utf16(pi.units); x.normalized = I.ctrl->getParamNormalized(pi.id); x.defaultNormalized = pi.defaultNormalizedValue; x.stepCount = pi.stepCount; x.automatable = pi.flags & ParameterInfo::kCanAutomate; x.readOnly = pi.flags & ParameterInfo::kIsReadOnly; x.bypass = pi.flags & ParameterInfo::kIsBypass; x.hidden = pi.flags & ParameterInfo::kIsHidden; out.push_back(x); }
  return out;
}
double Plugin::param(std::uint32_t id) const { Impl& I = *impl_; if (!I.ctrl) return 0; std::lock_guard<std::mutex> g(I.ctrlMutex); return I.ctrl->getParamNormalized(id); }
void Plugin::setParam(std::uint32_t id, double v) {
  Impl& I = *impl_; v = std::max(0.0, std::min(1.0, v));
  { std::lock_guard<std::mutex> g(I.ctrlMutex); if (I.ctrl) I.ctrl->setParamNormalized(id, v); }
  const unsigned h = I.head.load(std::memory_order_relaxed); if (h - I.tail.load(std::memory_order_acquire) >= Impl::kRing) return;   // the audio thread is not draining: drop (the controller already shows it)
  I.ring[h % Impl::kRing] = {id, v}; I.head.store(h + 1, std::memory_order_release);
  if (id == bypassId_) bypassOn_ = v >= 0.5;
}
std::string Plugin::paramText(std::uint32_t id, double v) const { Impl& I = *impl_; if (!I.ctrl) return ""; std::lock_guard<std::mutex> g(I.ctrlMutex); String128 s{}; if (I.ctrl->getParamStringByValue(id, v, s) != kResultOk) return ""; return utf16(s); }
void Plugin::setBypass(bool on) { if (hasBypass()) setParam(bypassId_, on ? 1.0 : 0.0); else bypassOn_ = on; }

void Plugin::process(const float* const* in, float* const* out, int frames) {
  Impl& I = *impl_; if (frames <= 0) return; frames = std::min(frames, I.maxBlock);
  if (!hasBypass() && bypassOn_) { for (int c = 0; c < outCh_; ++c) { const float* src = in && c < inCh_ ? in[c] : (in && inCh_ > 0 ? in[0] : nullptr); if (src) std::memcpy(out[c], src, static_cast<std::size_t>(frames) * 4); else std::memset(out[c], 0, static_cast<std::size_t>(frames) * 4); } return; }
  I.changes->clear();
  for (unsigned t = I.tail.load(std::memory_order_relaxed), h = I.head.load(std::memory_order_acquire); t != h; ++t) { const Impl::Change& c = I.ring[t % Impl::kRing]; int32 idx = 0; if (IParamValueQueue* q = I.changes->addParameterData(c.id, idx)) { int32 pi = 0; q->addPoint(0, c.v, pi); } I.tail.store(t + 1, std::memory_order_release); }
  for (int c = 0; c < inCh_; ++c) I.inPtrs[static_cast<std::size_t>(c)] = const_cast<float*>(in[c]);
  for (int c = 0; c < outCh_; ++c) I.outPtrs[static_cast<std::size_t>(c)] = out[c];
  I.inBus.numChannels = inCh_; I.inBus.channelBuffers32 = I.inPtrs.data(); I.inBus.silenceFlags = 0;
  I.outBus.numChannels = outCh_; I.outBus.channelBuffers32 = I.outPtrs.data(); I.outBus.silenceFlags = 0;
  ProcessData d; d.processMode = kRealtime; d.symbolicSampleSize = kSample32; d.numSamples = frames; d.numInputs = inCh_ > 0 ? 1 : 0; d.numOutputs = 1; d.inputs = inCh_ > 0 ? &I.inBus : nullptr; d.outputs = &I.outBus; d.inputParameterChanges = I.changes;
  if (I.proc->process(d) != kResultOk) { for (int c = 0; c < outCh_; ++c) std::memset(out[c], 0, static_cast<std::size_t>(frames) * 4); }
}

std::string Plugin::state() const {
  Impl& I = *impl_; MemStream* c = new MemStream(); MemStream* k = new MemStream();
  I.comp->getState(c); if (I.ctrl && !I.ctrlIsComp) { std::lock_guard<std::mutex> g(I.ctrlMutex); I.ctrl->getState(k); }
  std::vector<char> blob; auto put32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) blob.push_back(static_cast<char>((v >> (8 * i)) & 0xff)); };
  put32(static_cast<std::uint32_t>(c->d.size())); blob.insert(blob.end(), c->d.begin(), c->d.end()); put32(static_cast<std::uint32_t>(k->d.size())); blob.insert(blob.end(), k->d.begin(), k->d.end());
  c->release(); k->release(); return b64(blob);
}
bool Plugin::setState(const std::string& s) {
  Impl& I = *impl_; const std::vector<char> blob = unb64(s); if (blob.size() < 8) return false;
  auto get32 = [&](std::size_t at) { std::uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(static_cast<unsigned char>(blob[at + static_cast<std::size_t>(i)])) << (8 * i); return v; };
  const std::uint32_t cl = get32(0); if (4 + cl + 4 > blob.size()) return false; const std::uint32_t kl = get32(4 + cl); if (8 + cl + kl > blob.size()) return false;
  MemStream* c = new MemStream(); c->d.assign(blob.begin() + 4, blob.begin() + 4 + cl); c->pos = 0;
  bool ok = I.comp->setState(c) == kResultOk;
  if (I.ctrl) { std::lock_guard<std::mutex> g(I.ctrlMutex); c->pos = 0; I.ctrl->setComponentState(c); if (!I.ctrlIsComp && kl) { MemStream* k = new MemStream(); k->d.assign(blob.begin() + 8 + cl, blob.begin() + 8 + cl + kl); I.ctrl->setState(k); k->release(); } if (hasBypass()) bypassOn_ = I.ctrl->getParamNormalized(bypassId_) >= 0.5; }
  c->release(); return ok;
}

// the plug-in's own editor: the controller makes a view, the view is attached to the host's native window
bool Plugin::hasEditor() const {
  Impl& I = *impl_; if (I.viewChecked) return I.viewOk; I.viewChecked = true;
  if (!I.ctrl) return false;
  Steinberg::IPlugView* v = I.ctrl->createView("editor"); if (!v) return false;
  I.viewOk = v->isPlatformTypeSupported(kEditorWindowType) == Steinberg::kResultTrue; v->release(); return I.viewOk;
}
bool Plugin::editorOpen() const { return impl_->view != nullptr; }
bool Plugin::openEditor(void* parent, int& w, int& h) {
  Impl& I = *impl_; if (!parent || I.view || !hasEditor()) return false;
  Steinberg::IPlugView* v = I.ctrl->createView("editor"); if (!v) return false;
  Steinberg::ViewRect r; if (v->getSize(&r) == Steinberg::kResultOk && r.right > r.left && r.bottom > r.top) { w = r.right - r.left; h = r.bottom - r.top; }
  if (v->attached(parent, kEditorWindowType) != Steinberg::kResultOk) { v->release(); return false; }
  if (v->getSize(&r) == Steinberg::kResultOk && r.right > r.left && r.bottom > r.top) { w = r.right - r.left; h = r.bottom - r.top; }
  I.view = v; return true;
}
void Plugin::closeEditor() { Impl& I = *impl_; if (I.view) { I.view->removed(); I.view->release(); I.view = nullptr; } }


// ---------------------------------------------------------------- FabFilter quick-load (matching by vendor and name only)
namespace {
std::string lower(std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a'); return s; }
}  // namespace
bool isFabFilter(const std::string& vendor) { return lower(vendor).find("fabfilter") != std::string::npos; }
int proQVersion(const std::string& vendor, const std::string& name) {
  if (!isFabFilter(vendor)) return 0; std::string n = lower(name);
  if (n.rfind("fabfilter", 0) == 0) { n.erase(0, 9); while (!n.empty() && (n[0] == ' ' || n[0] == '-' || n[0] == '_')) n.erase(0, 1); }   // "FabFilter Pro-Q 3"
  if (n.rfind("pro-q", 0) != 0 && n.rfind("pro q", 0) != 0) return 0;
  std::size_t i = 5; while (i < n.size() && n[i] == ' ') ++i; int v = 0; while (i < n.size() && n[i] >= '0' && n[i] <= '9' && v < 1000) v = v * 10 + (n[i++] - '0');
  if (i < n.size() && n[i] >= 'a' && n[i] <= 'z' && v == 0) return 0;   // "Pro-Qx" is some other name
  return v > 0 ? v : 1;
}
int bestProQ(const std::vector<ClassInfo>& classes) {
  int best = -1, bestV = 0; for (std::size_t i = 0; i < classes.size(); ++i) { const int v = proQVersion(classes[i].vendor, classes[i].name); if (v > bestV) { bestV = v; best = static_cast<int>(i); } }
  return best;
}

}  // namespace am::vst3
