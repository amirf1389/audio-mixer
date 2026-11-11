#include "vst2_host.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "vst2_abi.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>
#endif

namespace am::vst2 {
using namespace abi;

namespace {
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
bool endsWith(const std::string& s, const char* suf) { const std::size_t n = std::strlen(suf); return s.size() >= n && s.compare(s.size() - n, n, suf) == 0; }
std::string baseName(const std::string& p) { const std::size_t s = p.find_last_of("/\\"); std::string b = s == std::string::npos ? p : p.substr(s + 1); const std::size_t dot = b.find_last_of('.'); return dot == std::string::npos ? b : b.substr(0, dot); }
std::string b64(const std::vector<char>& d) { static const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; std::string o; unsigned acc = 0; int bits = 0; for (char ch : d) { acc = (acc << 8) | static_cast<unsigned char>(ch); bits += 8; while (bits >= 6) { bits -= 6; o += a[(acc >> bits) & 63]; } } if (bits) o += a[(acc << (6 - bits)) & 63]; while (o.size() % 4) o += '='; return o; }
std::vector<char> unb64(const std::string& s) { std::vector<char> o; unsigned acc = 0; int bits = 0; for (char c : s) { int v; if (c >= 'A' && c <= 'Z') v = c - 'A'; else if (c >= 'a' && c <= 'z') v = c - 'a' + 26; else if (c >= '0' && c <= '9') v = c - '0' + 52; else if (c == '+' || c == '-') v = 62; else if (c == '/' || c == '_') v = 63; else continue; acc = (acc << 6) | static_cast<unsigned>(v); bits += 6; if (bits >= 8) { bits -= 8; o.push_back(static_cast<char>((acc >> bits) & 0xff)); } } return o; }

struct Module {
  void* handle = nullptr; EntryPoint entry = nullptr; std::string path;
  ~Module() {
    if (handle) {
#ifdef _WIN32
      FreeLibrary(static_cast<HMODULE>(handle));
#else
      dlclose(handle);
#endif
    }
  }
};
std::unique_ptr<Module> openModule(const std::string& path, std::string& err) {
  if (!exists(path) || isDir(path)) { err = "no such file: " + path; return nullptr; }
  auto m = std::make_unique<Module>(); m->path = path;
#ifdef _WIN32
  m->handle = LoadLibraryA(path.c_str()); if (!m->handle) { char b[64]; std::snprintf(b, sizeof b, " (error %lu)", GetLastError()); err = "cannot load " + path + b; return nullptr; }
  for (const char* n : {"VSTPluginMain", "main"}) { if (FARPROC f = GetProcAddress(static_cast<HMODULE>(m->handle), n)) { std::memcpy(&m->entry, &f, sizeof f); break; } }
#else
  m->handle = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL); if (!m->handle) { const char* e = dlerror(); err = std::string("cannot load ") + path + ": " + (e ? e : "?"); return nullptr; }
  for (const char* n : {"VSTPluginMain", "main_plugin", "main"}) { if (void* f = dlsym(m->handle, n)) { std::memcpy(&m->entry, &f, sizeof f); break; } }
#endif
  if (!m->entry) { err = path + " has no VSTPluginMain: not a VST2 plug-in"; return nullptr; }
  return m;
}

// the host callback: the plug-in asks about its host. `user` of the AEffect points at the Impl once created; before that we answer statically.
struct Plugin_Impl;
std::intptr_t hostCallback(AEffect* e, std::int32_t opcode, std::int32_t, std::intptr_t, void* ptr, float);
}  // namespace

struct Plugin::Impl {
  std::unique_ptr<Module> module; AEffect* fx = nullptr; double rate = 48000; int maxBlock = 256; bool started = false; bool editing = false;
  struct Change { std::int32_t index; float v; }; static constexpr unsigned kRing = 256; Change ring[kRing]; std::atomic<unsigned> head{0}, tail{0};
  std::vector<float*> inPtrs, outPtrs; std::vector<std::vector<float>> scratch;   // extra channels the plug-in has beyond our mono / stereo
  std::mutex m;
  std::intptr_t call(std::int32_t op, std::int32_t idx = 0, std::intptr_t val = 0, void* ptr = nullptr, float opt = 0) { return fx ? fx->dispatcher(fx, op, idx, val, ptr, opt) : 0; }
  std::string str(std::int32_t op, std::int32_t idx = 0) { char b[kStringMax] = {0}; call(op, idx, 0, b); b[kStringMax - 1] = 0; return b; }
  bool canDo(const char* what) { return call(effCanDo, 0, 0, const_cast<char*>(what)) > 0; }
  ~Impl() {
    if (fx) { if (editing) { call(effEditClose); editing = false; } if (started) { call(effStopProcess); call(effMainsChanged, 0, 0); } call(effClose); fx = nullptr; }
    module.reset();
  }
};

namespace {
Plugin::Impl* implOf(AEffect* e) { return e && e->user ? static_cast<Plugin::Impl*>(e->user) : nullptr; }
std::intptr_t hostCallback(AEffect* e, std::int32_t opcode, std::int32_t, std::intptr_t, void* ptr, float) {
  Plugin::Impl* I = implOf(e);
  switch (opcode) {
    case audioMasterVersion: return kVstVersion;
    case audioMasterCurrentId: return e ? e->uniqueID : 0;
    case audioMasterGetSampleRate: return I ? static_cast<std::intptr_t>(I->rate) : 48000;
    case audioMasterGetBlockSize: return I ? I->maxBlock : 256;
    case audioMasterGetCurrentProcessLevel: return 2;   // kVstProcessLevelRealtime
    case audioMasterGetAutomationState: return 1;       // off
    case audioMasterGetVendorString: if (ptr) std::strncpy(static_cast<char*>(ptr), "AMFB Audio", 63); return 1;
    case audioMasterGetProductString: if (ptr) std::strncpy(static_cast<char*>(ptr), "A mix OS", 63); return 1;
    case audioMasterGetVendorVersion: return 100;
    case audioMasterGetLanguage: return 1;               // English
    case audioMasterCanDo: { const char* w = static_cast<const char*>(ptr); if (!w) return 0; for (const char* ok : {"sendVstEvents", "sizeWindow", "supplyIdle", "startStopProcess", "shellCategory"}) if (!std::strcmp(w, ok)) return 1; return 0; }
    case audioMasterIdle: case audioMasterUpdateDisplay: case audioMasterBeginEdit: case audioMasterEndEdit: case audioMasterAutomate: case audioMasterIOChanged: case audioMasterSizeWindow: return 1;
    default: return 0;
  }
}
}  // namespace

std::vector<std::string> defaultDirs() {
  std::vector<std::string> out;
#ifdef _WIN32
  if (const char* pf = std::getenv("ProgramFiles")) { out.push_back(std::string(pf) + "/VSTPlugins"); out.push_back(std::string(pf) + "/Steinberg/VSTPlugins"); out.push_back(std::string(pf) + "/Common Files/VST2"); }
#elif defined(__APPLE__)
  out.push_back("/Library/Audio/Plug-Ins/VST"); if (const char* h = std::getenv("HOME")) out.push_back(std::string(h) + "/Library/Audio/Plug-Ins/VST");
#else
  if (const char* h = std::getenv("HOME")) out.push_back(std::string(h) + "/.vst");
  out.push_back("/usr/lib/vst"); out.push_back("/usr/local/lib/vst");
#endif
  std::vector<std::string> ok; for (const auto& d : out) if (isDir(d)) ok.push_back(d); return ok;
}
std::vector<std::string> scan(const std::vector<std::string>& dirs) {
  std::vector<std::string> out;
  for (const auto& d : dirs) for (const auto& e : listDir(d)) {
#ifdef _WIN32
    if (endsWith(e, ".dll") || endsWith(e, ".DLL")) out.push_back(e);
#else
    if (endsWith(e, ".so")) out.push_back(e);
#endif
  }
  return out;
}
bool inspect(const std::string& path, host::Info& info, std::string& err) {
  auto p = Plugin::load(path, 48000, 256, err); if (!p) return false; info = p->info(); return true;
}

std::unique_ptr<Plugin> Plugin::load(const std::string& path, double rate, int maxBlock, std::string& err) {
  auto m = openModule(path, err); if (!m) return nullptr;
  std::unique_ptr<Plugin> p(new Plugin()); p->impl_ = std::make_unique<Impl>(); Impl& I = *p->impl_; I.module = std::move(m); I.rate = rate; I.maxBlock = std::max(16, maxBlock); p->path_ = path;
  AEffect* fx = I.module->entry(&hostCallback);
  if (!fx || fx->magic != kMagic) { err = path + ": the plug-in did not return a valid effect"; return nullptr; }
  fx->user = &I; I.fx = fx;
  if (!(fx->flags & kCanReplacing) || !fx->processReplacing) { err = path + ": the plug-in has no processReplacing (too old for this host)"; return nullptr; }
  I.call(effOpen);
  I.call(effSetSampleRate, 0, 0, nullptr, static_cast<float>(rate)); I.call(effSetBlockSize, 0, I.maxBlock);
  I.call(effMainsChanged, 0, 1); I.call(effStartProcess); I.started = true;
  p->info_.format = "VST2"; p->info_.name = I.str(effGetEffectName); if (p->info_.name.empty()) p->info_.name = baseName(path); p->info_.vendor = I.str(effGetVendorString); p->info_.category = (fx->flags & kIsSynth) ? "Instrument" : "Fx";
  { char b[32]; std::snprintf(b, sizeof b, "%08X", static_cast<unsigned>(fx->uniqueID)); p->info_.uid = b; std::snprintf(b, sizeof b, "%d", fx->version); p->info_.version = b; }
  p->inCh_ = std::min<std::int32_t>(fx->numInputs, 2); p->outCh_ = std::min<std::int32_t>(std::max<std::int32_t>(fx->numOutputs, 0), 2);
  if (p->outCh_ < 1) { err = p->info_.name + ": no audio outputs"; return nullptr; }
  p->latency_ = static_cast<std::uint32_t>(std::max<std::int32_t>(fx->initialDelay, 0));
  p->pluginBypass_ = I.canDo("bypass");
  // pointers for every channel the plug-in declares; the ones beyond our 2 are silent scratch
  I.scratch.assign(static_cast<std::size_t>(std::max<std::int32_t>(fx->numInputs, 0) + std::max<std::int32_t>(fx->numOutputs, 0)), std::vector<float>(static_cast<std::size_t>(I.maxBlock), 0.f));
  I.inPtrs.assign(static_cast<std::size_t>(std::max<std::int32_t>(fx->numInputs, 0)), nullptr); I.outPtrs.assign(static_cast<std::size_t>(std::max<std::int32_t>(fx->numOutputs, 0)), nullptr);
  return p;
}
Plugin::~Plugin() = default;
// the plug-in's own editor: effFlagsHasEditor is bit 0 of the flags; the rectangle is { top, left, bottom, right } as 16-bit numbers
bool Plugin::hasEditor() const { return impl_->fx && (impl_->fx->flags & 1) != 0; }
bool Plugin::editorOpen() const { return impl_->editing; }
bool Plugin::openEditor(void* parent, int& w, int& h) {
  if (!hasEditor() || impl_->editing || !parent) return false;
  struct Rect { std::int16_t top, left, bottom, right; }; Rect* r = nullptr;
  impl_->call(effEditGetRect, 0, 0, &r);                                 // some editors only know their size after this first question
  if (impl_->call(effEditOpen, 0, 0, parent) == 0) return false;
  impl_->editing = true; r = nullptr; impl_->call(effEditGetRect, 0, 0, &r);
  if (r && r->right > r->left && r->bottom > r->top) { w = r->right - r->left; h = r->bottom - r->top; }
  return true;
}
void Plugin::closeEditor() { if (impl_->editing) { impl_->call(effEditClose); impl_->editing = false; } }
void Plugin::editorIdle() { if (impl_->editing) impl_->call(effEditIdle); }

std::vector<host::ParamInfo> Plugin::params() const {
  std::vector<host::ParamInfo> out; Impl& I = *impl_; std::lock_guard<std::mutex> g(I.m);
  for (std::int32_t i = 0; i < I.fx->numParams; ++i) { host::ParamInfo x; x.id = static_cast<std::uint32_t>(i); x.title = I.str(effGetParamName, i); x.units = I.str(effGetParamLabel, i); x.normalized = I.fx->getParameter(I.fx, i); x.defaultNormalized = x.normalized; x.automatable = I.call(effCanBeAutomated, i) != 0; out.push_back(x); }
  return out;
}
double Plugin::param(std::uint32_t id) const { Impl& I = *impl_; if (static_cast<std::int32_t>(id) >= I.fx->numParams) return 0; std::lock_guard<std::mutex> g(I.m); return I.fx->getParameter(I.fx, static_cast<std::int32_t>(id)); }
void Plugin::setParam(std::uint32_t id, double v) {
  Impl& I = *impl_; if (static_cast<std::int32_t>(id) >= I.fx->numParams) return; v = std::max(0.0, std::min(1.0, v));
  // VST2 setParameter is defined as callable from any thread; most plug-ins want it off the audio thread, so set it here and let process() see it
  { std::lock_guard<std::mutex> g(I.m); I.fx->setParameter(I.fx, static_cast<std::int32_t>(id), static_cast<float>(v)); }
}
std::string Plugin::paramText(std::uint32_t id, double v) const {
  Impl& I = *impl_; if (static_cast<std::int32_t>(id) >= I.fx->numParams) return ""; std::lock_guard<std::mutex> g(I.m);
  const float cur = I.fx->getParameter(I.fx, static_cast<std::int32_t>(id)); const bool differs = std::fabs(cur - static_cast<float>(v)) > 1e-6f;
  if (differs) I.fx->setParameter(I.fx, static_cast<std::int32_t>(id), static_cast<float>(v));
  std::string s = I.str(effGetParamDisplay, static_cast<std::int32_t>(id));
  if (differs) I.fx->setParameter(I.fx, static_cast<std::int32_t>(id), cur);
  return s;
}
void Plugin::setBypass(bool on) { Impl& I = *impl_; bypassOn_ = on; if (pluginBypass_) { std::lock_guard<std::mutex> g(I.m); I.call(effSetBypass, 0, on ? 1 : 0); } }

void Plugin::process(const float* const* in, float* const* out, int frames) {
  Impl& I = *impl_; if (frames <= 0) return; frames = std::min(frames, I.maxBlock);
  if (bypassOn_.load() && !pluginBypass_) { for (int c = 0; c < outCh_; ++c) { const float* src = in && c < inCh_ ? in[c] : (in && inCh_ > 0 ? in[0] : nullptr); if (src) std::memcpy(out[c], src, static_cast<std::size_t>(frames) * 4); else std::memset(out[c], 0, static_cast<std::size_t>(frames) * 4); } return; }
  const std::int32_t nIn = I.fx->numInputs, nOut = I.fx->numOutputs;
  for (std::int32_t c = 0; c < nIn; ++c) I.inPtrs[static_cast<std::size_t>(c)] = (in && c < inCh_) ? const_cast<float*>(in[c]) : (in && inCh_ > 0 && c < 2 ? const_cast<float*>(in[0]) : I.scratch[static_cast<std::size_t>(c)].data());
  for (std::int32_t c = 0; c < nOut; ++c) I.outPtrs[static_cast<std::size_t>(c)] = c < outCh_ ? out[c] : I.scratch[static_cast<std::size_t>(nIn + c)].data();
  I.fx->processReplacing(I.fx, I.inPtrs.data(), I.outPtrs.data(), frames);
}

std::string Plugin::state() const {
  Impl& I = *impl_; std::lock_guard<std::mutex> g(I.m); std::vector<char> blob;
  if (I.fx->flags & kProgramChunks) { void* data = nullptr; const std::intptr_t n = I.call(effGetChunk, 0, 0, &data); if (n > 0 && data) { blob.push_back('C'); blob.insert(blob.end(), static_cast<char*>(data), static_cast<char*>(data) + n); return b64(blob); } }
  blob.push_back('P'); for (std::int32_t i = 0; i < I.fx->numParams; ++i) { const float v = I.fx->getParameter(I.fx, i); const char* b = reinterpret_cast<const char*>(&v); blob.insert(blob.end(), b, b + 4); }
  return b64(blob);
}
bool Plugin::setState(const std::string& s) {
  Impl& I = *impl_; const std::vector<char> blob = unb64(s); if (blob.empty()) return false; std::lock_guard<std::mutex> g(I.m);
  if (blob[0] == 'C') { if (!(I.fx->flags & kProgramChunks)) return false; return I.call(effSetChunk, 0, static_cast<std::intptr_t>(blob.size() - 1), const_cast<char*>(blob.data() + 1)) != 0 || true; }
  if (blob[0] != 'P' || (blob.size() - 1) % 4) return false;
  const std::int32_t n = std::min<std::int32_t>(I.fx->numParams, static_cast<std::int32_t>((blob.size() - 1) / 4));
  for (std::int32_t i = 0; i < n; ++i) { float v; std::memcpy(&v, blob.data() + 1 + 4 * i, 4); I.fx->setParameter(I.fx, i, v); }
  return true;
}

}  // namespace am::vst2
