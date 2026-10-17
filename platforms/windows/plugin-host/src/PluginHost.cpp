// PluginHost - runs ONE VST 2.x effect plugin (.dll on Windows, .vst bundle on macOS, .so / .vst on Linux) in its own process.
// The Audio Mixer bridge starts it, sends audio blocks on stdin and reads the processed blocks from stdout (binary frames, below).
// A crashing plugin takes down only this process, never the mixer. The plugin is loaded from the path given on the command line; the
// bridge only ever passes paths of plugins its own scan found. VST 3 (.vst3) is hosted too: see Vst3Host.h for what is and is not supported.
//
//   PluginHost <plugin file> [sampleRate=48000] [blockSize=512] [VST3 class name]
//
// Frame (both directions): u8 type, 3 bytes zero, u32 little-endian payload length, payload.
//   host -> bridge  'H' hello JSON {"ok":true,"name":..,"vendor":..,"inputs":n,"outputs":n,"params":n,"latency":frames} or {"ok":false,"error":..}
//   bridge -> host  'A' interleaved stereo float32 -> host replies 'A' with the processed interleaved stereo float32 (same length)
//   bridge -> host  'S' u32 index + float32 value (set parameter, no reply)
//   bridge -> host  'G' (get parameters) -> host replies 'G' JSON [{"i":0,"name":"Gain","label":"dB","display":"0.0","value":0.5},...]
//   bridge -> host  'Q' quit
//
// VST 2.x is declared here from the public binary interface (struct layout and opcodes); no Steinberg code is included. The same goes for VST 3 (Vst3Host.h).
//
// Build:  g++ -O2 -std=c++11 -o PluginHost PluginHost.cpp -ldl                                   (Linux, macOS)
//         x86_64-w64-mingw32-g++ -O2 -std=c++11 -static -s -o ../x64/PluginHost.exe PluginHost.cpp   (Windows, 64-bit plugins)
//         i686-w64-mingw32-g++   -O2 -std=c++11 -static -s -o ../x86/PluginHost.exe PluginHost.cpp   (Windows, 32-bit plugins)
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <dlfcn.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

typedef intptr_t VstIntPtr;
struct AEffect;
typedef VstIntPtr (*AudioMasterCallback)(AEffect*, int32_t, int32_t, VstIntPtr, void*, float);
typedef VstIntPtr (*DispatcherProc)(AEffect*, int32_t, int32_t, VstIntPtr, void*, float);
struct AEffect {
  int32_t magic;                                   // 'VstP'
  DispatcherProc dispatcher;
  void (*process)(AEffect*, float**, float**, int32_t);
  void (*setParameter)(AEffect*, int32_t, float);
  float (*getParameter)(AEffect*, int32_t);
  int32_t numPrograms, numParams, numInputs, numOutputs, flags;
  VstIntPtr resvd1, resvd2;
  int32_t initialDelay, realQualities, offQualities;
  float ioRatio;
  void* object; void* user;
  int32_t uniqueID, version;
  void (*processReplacing)(AEffect*, float**, float**, int32_t);
  void (*processDoubleReplacing)(AEffect*, double**, double**, int32_t);
  char future[56];
};
enum { kEffectMagic = 0x56737450, effOpen = 0, effClose = 1, effGetParamLabel = 6, effGetParamDisplay = 7, effGetParamName = 8,
       effSetSampleRate = 10, effSetBlockSize = 11, effMainsChanged = 12, effGetEffectName = 45, effGetVendorString = 47,
       effStartProcess = 71, effStopProcess = 72,
       effFlagsCanReplacing = 1 << 4, effFlagsIsSynth = 1 << 8,
       audioMasterAutomate = 0, audioMasterVersion = 1, audioMasterCurrentId = 2, audioMasterIdle = 3, audioMasterGetSampleRate = 16,
       audioMasterGetBlockSize = 17, audioMasterGetVendorString = 32, audioMasterGetProductString = 33, audioMasterGetVendorVersion = 34,
       audioMasterCanDo = 37, audioMasterGetLanguage = 38 };

static double gSampleRate = 48000; static int gBlock = 512;

static VstIntPtr hostCallback(AEffect*, int32_t opcode, int32_t, VstIntPtr, void* ptr, float) {
  switch (opcode) {
    case audioMasterVersion: return 2400;
    case audioMasterGetSampleRate: return (VstIntPtr)gSampleRate;
    case audioMasterGetBlockSize: return gBlock;
    case audioMasterGetVendorString: if (ptr) strcpy((char*)ptr, "Audio Mixer"); return 1;
    case audioMasterGetProductString: if (ptr) strcpy((char*)ptr, "Audio Mixer plugin host"); return 1;
    case audioMasterGetVendorVersion: return 1000;
    case audioMasterGetLanguage: return 1;                       // English
    case audioMasterCanDo: {                                      // only what this host really provides
      const char* s = (const char*)ptr;
      return (s && (!strcmp(s, "sendVstEvents") || !strcmp(s, "supplyIdle"))) ? 0 : 0;
    }
    default: return 0;                                            // automate, idle, time info ...: nothing to do
  }
}

// ── framing ──
static bool readAll(void* buf, size_t n) { size_t got = 0; while (got < n) { size_t r = fread((char*)buf + got, 1, n - got, stdin); if (r == 0) return false; got += r; } return true; }
static void writeFrame(char type, const void* data, uint32_t len) {
  unsigned char h[8] = { (unsigned char)type, 0, 0, 0, (unsigned char)(len & 255), (unsigned char)((len >> 8) & 255), (unsigned char)((len >> 16) & 255), (unsigned char)((len >> 24) & 255) };
  fwrite(h, 1, 8, stdout); if (len) fwrite(data, 1, len, stdout); fflush(stdout);
}
static std::string jsonEsc(const std::string& in) {
  std::string o; for (unsigned char c : in) { if (c == '"' || c == '\\') { o += '\\'; o += (char)c; } else if (c < 0x20) o += ' '; else o += (char)c; } return o;
}
static void hello(bool ok, const std::string& text, const std::string& extra = "") {
  std::string j = ok ? "{\"ok\":true" + extra + "}" : "{\"ok\":false,\"error\":\"" + jsonEsc(text) + "\"}";
  writeFrame('H', j.data(), (uint32_t)j.size());
}

// ── loading ──
typedef AEffect* (*VstMain)(AudioMasterCallback);
#ifndef _WIN32
static std::string macBundleBinary(const std::string& dir) {
  std::string mac = dir + "/Contents/MacOS"; DIR* d = opendir(mac.c_str()); if (!d) return "";
  std::string found; while (dirent* e = readdir(d)) { if (e->d_name[0] != '.') { found = mac + "/" + e->d_name; break; } } closedir(d); return found;
}
#endif
static void* openLibrary(const std::string& path, std::string& err) {
#ifdef _WIN32
  int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0); std::vector<wchar_t> w(n > 0 ? n : 1);
  MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, w.data(), n);
  HMODULE m = LoadLibraryW(w.data()); if (!m) { char b[64]; snprintf(b, sizeof b, "LoadLibrary failed (error %lu)", GetLastError()); err = b; } return (void*)m;
#else
  std::string p = path; struct stat st; if (stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) { std::string b = macBundleBinary(p); if (!b.empty()) p = b; }
  void* m = dlopen(p.c_str(), RTLD_NOW | RTLD_LOCAL); if (!m) { const char* e = dlerror(); err = e ? e : "dlopen failed"; } return m;
#endif
}
static void* symbol(void* lib, const char* name) {
#ifdef _WIN32
  return (void*)GetProcAddress((HMODULE)lib, name);
#else
  return dlsym(lib, name);
#endif
}

#include "Vst3Host.h"

// ── VST 3 ──
static bool endsWith(const std::string& s, const char* e) { size_t n = strlen(e); if (s.size() < n) return false; for (size_t i = 0; i < n; i++) { char a = s[s.size() - n + i], b = e[i]; if (a >= 'A' && a <= 'Z') a += 32; if (a != b) return false; } return true; }

// the module inside a .vst3 bundle (Contents/<arch>-<os>/<binary>) or the file itself
static std::string vst3Binary(const std::string& path) {
#ifdef _WIN32
  DWORD attr = GetFileAttributesA(path.c_str()); if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) return path;
#if defined(_WIN64)
  const char* dirs[] = { "x86_64-win", "arm64-win", "arm64ec-win" };
#else
  const char* dirs[] = { "x86-win" };
#endif
  for (const char* d : dirs) {
    std::string pat = path + "\\Contents\\" + d + "\\*"; WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) continue;
    do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && endsWith(fd.cFileName, ".vst3")) { std::string f = path + "\\Contents\\" + d + "\\" + fd.cFileName; FindClose(h); return f; } } while (FindNextFileA(h, &fd));
    FindClose(h);
  }
  return "";
#else
  struct stat st; if (stat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return path;
#if defined(__APPLE__)
  return macBundleBinary(path);
#else
#if defined(__aarch64__)
  const char* arch = "aarch64-linux";
#elif defined(__arm__)
  const char* arch = "armv7l-linux";
#elif defined(__i386__)
  const char* arch = "i386-linux";
#else
  const char* arch = "x86_64-linux";
#endif
  std::string dir = path + "/Contents/" + arch; DIR* d = opendir(dir.c_str()); if (!d) return "";
  std::string found; while (dirent* e = readdir(d)) { std::string n = e->d_name; if (n != "." && n != ".." && endsWith(n, ".so")) { found = dir + "/" + n; break; } } closedir(d); return found;
#endif
#endif
}

static std::string cstr(const char* s, size_t max) { size_t n = 0; while (n < max && s[n]) n++; return std::string(s, n); }

static int runVst3(const std::string& path, const std::string& wantClass) {
  using namespace v3;
  std::string bin = vst3Binary(path); if (bin.empty()) { hello(false, "no binary for this system inside the .vst3 bundle"); return 3; }
  std::string err; void* lib = openLibrary(bin, err); if (!lib) { hello(false, "could not load the plugin: " + err); return 3; }
#ifdef _WIN32
  typedef bool (V3CALL *InitFn)(); InitFn init = (InitFn)symbol(lib, "InitDll"); if (init) init();
#elif !defined(__APPLE__)
  typedef bool (*EntryFn)(void*); EntryFn entry = (EntryFn)symbol(lib, "ModuleEntry"); if (entry) entry(lib);
#endif
  typedef IPluginFactory* (V3CALL *FactoryFn)(); FactoryFn getFactory = (FactoryFn)symbol(lib, "GetPluginFactory");
#if defined(_WIN32) && !defined(_WIN64)
  if (!getFactory) getFactory = (FactoryFn)symbol(lib, "_GetPluginFactory@0");                       // a 32-bit stdcall export that was not undecorated by a .def file
  if (!getFactory) getFactory = (FactoryFn)symbol(lib, "GetPluginFactory@0");
#endif
  if (!getFactory) { hello(false, "no GetPluginFactory export: not a VST3 plugin"); return 3; }
  IPluginFactory* factory = getFactory(); if (!factory) { hello(false, "the plugin has no factory"); return 3; }
  PFactoryInfo fi; memset(&fi, 0, sizeof fi); factory->getFactoryInfo(&fi);
  IPluginFactory2* f2 = nullptr; factory->queryInterface(kPluginFactory2, (void**)&f2);

  // the class to use: the one asked for by name, else the first audio class that is not an instrument
  int chosen = -1; char cid[16]; std::string name;
  for (int i = 0, n = factory->countClasses(); i < n && chosen < 0; i++) {
    PClassInfo2 c2; memset(&c2, 0, sizeof c2); PClassInfo c1; memset(&c1, 0, sizeof c1);
    bool ok2 = f2 && f2->getClassInfo2(i, &c2) == kOk;
    if (!ok2 && factory->getClassInfo(i, &c1) != kOk) continue;
    const char* cat = ok2 ? c2.category : c1.category; if (strcmp(cat, "Audio Module Class") != 0) continue;
    std::string nm = cstr(ok2 ? c2.name : c1.name, 64), sub = ok2 ? cstr(c2.subCategories, 128) : "";
    if (!wantClass.empty() ? nm != wantClass : sub.find("Instrument") != std::string::npos) continue;
    chosen = i; memcpy(cid, ok2 ? c2.cid : c1.cid, 16); name = nm;
  }
  if (chosen < 0) { hello(false, wantClass.empty() ? "no audio effect class in this VST3 (instruments are not supported)" : "no audio class named \"" + wantClass + "\" in this VST3"); return 3; }

  static Host host; static Changes changes, outChanges; static NoEvents events, outEvents;
  IComponent* comp = nullptr;
  if (factory->createInstance(cid, kComponent, (void**)&comp) != kOk || !comp) { hello(false, "the plugin would not create its audio component"); return 3; }
  if (comp->initialize(static_cast<IHostApplication*>(&host)) != kOk) { hello(false, "the plugin refused to initialise"); return 3; }
  IAudioProcessor* proc = nullptr; comp->queryInterface(kAudioProcessor, (void**)&proc);
  if (!proc) { hello(false, "the plugin has no audio processor"); return 3; }
  IEditController* ctrl = nullptr; bool ctrlSeparate = false;
  if (comp->queryInterface(kEditController, (void**)&ctrl) != kOk || !ctrl) {
    ctrl = nullptr; char ccid[16]; memset(ccid, 0, 16);
    if (comp->getControllerClassId(ccid) == kOk && factory->createInstance(ccid, kEditController, (void**)&ctrl) == kOk && ctrl) { ctrlSeparate = true; ctrl->initialize(static_cast<IHostApplication*>(&host)); }
  }
  if (ctrl) ctrl->setComponentHandler(static_cast<IComponentHandler*>(&host));

  // buses: the first audio input and output carry the signal, every other audio bus stays inactive
  const int nIn = comp->getBusCount(0, 0), nOut = comp->getBusCount(0, 1);
  if (nIn < 1 || nOut < 1) { hello(false, "the plugin is not an effect (it needs an audio input and an audio output)"); return 3; }
  // the plugin's own arrangement for every bus, the main buses asked for stereo (a refusal is fine: what the plugin keeps is read back)
  std::vector<uint64_t> aIn(nIn, 3), aOut(nOut, 3);
  for (int i = 0; i < nIn; i++) { uint64_t a = 0; if (proc->getBusArrangement(0, i, a) == kOk && a) aIn[i] = a; }
  for (int i = 0; i < nOut; i++) { uint64_t a = 0; if (proc->getBusArrangement(1, i, a) == kOk && a) aOut[i] = a; }
  aIn[0] = 3; aOut[0] = 3;
  proc->setBusArrangements(aIn.data(), nIn, aOut.data(), nOut);
  for (int i = 0; i < nIn; i++) { uint64_t a = 0; if (proc->getBusArrangement(0, i, a) == kOk && a) aIn[i] = a; }
  for (int i = 0; i < nOut; i++) { uint64_t a = 0; if (proc->getBusArrangement(1, i, a) == kOk && a) aOut[i] = a; }
  auto chans = [](uint64_t a) { int c = 0; while (a) { c += (int)(a & 1); a >>= 1; } return c < 1 ? 2 : c; };
  const int chIn = chans(aIn[0]), chOut = chans(aOut[0]);
  if (chIn > 8 || chOut > 8) { hello(false, "only stereo and mono effects are supported"); return 3; }
  if (proc->canProcessSampleSize(0) != kOk) { hello(false, "the plugin cannot process 32-bit float audio"); return 3; }
  // the first bus carries the signal; further buses the plugin switches on by default (a side-chain, a second input) are fed silence / their output is dropped
  std::vector<int> inCh(nIn, 0), outCh(nOut, 0);
  for (int i = 0; i < nIn; i++) { BusInfo bi; memset(&bi, 0, sizeof bi); bool on = i == 0 || (comp->getBusInfo(0, 0, i, bi) == kOk && (bi.flags & 1)); comp->activateBus(0, 0, i, on); if (on) inCh[i] = i == 0 ? chIn : chans(aIn[i]); }
  for (int i = 0; i < nOut; i++) { BusInfo bi; memset(&bi, 0, sizeof bi); bool on = i == 0 || (comp->getBusInfo(0, 1, i, bi) == kOk && (bi.flags & 1)); comp->activateBus(0, 1, i, on); if (on) outCh[i] = i == 0 ? chOut : chans(aOut[i]); }
  ProcessSetup setup; setup.processMode = 0; setup.symbolicSampleSize = 0; setup.maxSamplesPerBlock = gBlock; setup.sampleRate = gSampleRate;
  if (proc->setupProcessing(setup) != kOk) { hello(false, "the plugin refused the sample rate / block size"); return 3; }
  comp->setActive(1); proc->setProcessing(1);

  // parameters: the visible, writable ones, in the controller's order
  struct Par { uint32_t id; };
  std::vector<Par> pars;
  if (ctrl) for (int i = 0, n = ctrl->getParameterCount(); i < n && i < 256; i++) { ParameterInfo pi; memset(&pi, 0, sizeof pi); if (ctrl->getParameterInfo(i, pi) == kOk && !(pi.flags & ((1 << 4) | (1 << 1)))) pars.push_back({ pi.id }); }
  char extra[600]; snprintf(extra, sizeof extra, ",\"name\":\"%s\",\"vendor\":\"%s\",\"inputs\":%d,\"outputs\":%d,\"params\":%d,\"latency\":%u,\"format\":\"VST3\"",
    jsonEsc(name).c_str(), jsonEsc(cstr(fi.vendor, 64)).c_str(), chIn, chOut, (int)pars.size(), (unsigned)proc->getLatencySamples());
  hello(true, "", extra);

  std::vector<std::vector<float> > in(chIn, std::vector<float>(gBlock)), out(chOut, std::vector<float>(gBlock));
  std::vector<float*> pin(chIn), pout(chOut);
  for (int c = 0; c < chIn; c++) pin[c] = in[c].data();
  for (int c = 0; c < chOut; c++) pout[c] = out[c].data();
  std::vector<AudioBusBuffers> ib(nIn), ob(nOut);
  std::vector<std::vector<std::vector<float> > > spareIn(nIn), spareOut(nOut);       // buffers of the extra buses
  std::vector<std::vector<float*> > spareInP(nIn), spareOutP(nOut);
  for (int i = 0; i < nIn; i++) {
    ib[i].numChannels = inCh[i]; ib[i].silenceFlags = 0; ib[i].channelBuffers32 = nullptr;
    if (i > 0 && inCh[i] > 0) { spareIn[i].assign(inCh[i], std::vector<float>(gBlock, 0.0f)); for (auto& v : spareIn[i]) spareInP[i].push_back(v.data()); ib[i].channelBuffers32 = spareInP[i].data(); ib[i].silenceFlags = inCh[i] >= 64 ? ~0ull : ((1ull << inCh[i]) - 1); }
  }
  for (int i = 0; i < nOut; i++) {
    ob[i].numChannels = outCh[i]; ob[i].silenceFlags = 0; ob[i].channelBuffers32 = nullptr;
    if (i > 0 && outCh[i] > 0) { spareOut[i].assign(outCh[i], std::vector<float>(gBlock, 0.0f)); for (auto& v : spareOut[i]) spareOutP[i].push_back(v.data()); ob[i].channelBuffers32 = spareOutP[i].data(); }
  }
  ib[0].channelBuffers32 = pin.data(); ob[0].channelBuffers32 = pout.data();
  ProcessContext pc; memset(&pc, 0, sizeof pc); pc.sampleRate = gSampleRate; pc.tempo = 120; pc.timeSigNumerator = 4; pc.timeSigDenominator = 4;
  std::vector<std::pair<uint32_t, double> > pending;

  for (;;) {
    unsigned char h[8]; if (!readAll(h, 8)) break;
    uint32_t len = h[4] | (h[5] << 8) | (h[6] << 16) | ((uint32_t)h[7] << 24);
    if (len > (16u << 20)) break;
    std::vector<unsigned char> body(len); if (len && !readAll(body.data(), len)) break;
    if (h[0] == 'Q') break;
    if (h[0] == 'A') {
      const int frames = (int)(len / 8); const float* src = (const float*)body.data(); std::vector<float> dst((size_t)frames * 2);
      for (int pos = 0; pos < frames; pos += gBlock) {
        const int n = frames - pos < gBlock ? frames - pos : gBlock;
        for (int c = 0; c < chIn; c++) std::fill(in[c].begin(), in[c].end(), 0.0f);
        for (int i = 0; i < n; i++) { in[0][i] = src[(size_t)(pos + i) * 2]; if (chIn > 1) in[1][i] = src[(size_t)(pos + i) * 2 + 1]; }
        if (chIn == 1) for (int i = 0; i < n; i++) in[0][i] = 0.5f * (src[(size_t)(pos + i) * 2] + src[(size_t)(pos + i) * 2 + 1]);
        for (int c = 0; c < chOut; c++) std::fill(out[c].begin(), out[c].end(), 0.0f);
        changes.n = 0;                                                                              // parameter changes made since the last block go in with this one
        for (size_t k = 0; k < pending.size() && changes.n < (int32_t)changes.q.size(); k++) { changes.q[changes.n].id = pending[k].first; changes.q[changes.n].value = pending[k].second; changes.n++; }
        pending.clear();
        ProcessData pd; memset(&pd, 0, sizeof pd);
        pd.processMode = 0; pd.symbolicSampleSize = 0; pd.numSamples = n; pd.numInputs = nIn; pd.numOutputs = nOut; pd.inputs = ib.data(); pd.outputs = ob.data();
        outChanges.n = 0;
        pd.inputParameterChanges = &changes; pd.outputParameterChanges = &outChanges; pd.inputEvents = &events; pd.outputEvents = &outEvents; pd.processContext = &pc;
        proc->process(pd);
        pc.projectTimeSamples += n; pc.continousTimeSamples += n;
        for (int i = 0; i < n; i++) { dst[(size_t)(pos + i) * 2] = out[0][i]; dst[(size_t)(pos + i) * 2 + 1] = out[chOut > 1 ? 1 : 0][i]; }
      }
      writeFrame('A', dst.data(), (uint32_t)(dst.size() * sizeof(float)));
    } else if (h[0] == 'S' && len >= 8) {
      uint32_t idx; float v; memcpy(&idx, body.data(), 4); memcpy(&v, body.data() + 4, 4);
      if (idx < pars.size() && ctrl) { double nv = v < 0 ? 0 : v > 1 ? 1 : v; ctrl->setParamNormalized(pars[idx].id, nv); pending.push_back({ pars[idx].id, nv }); }
    } else if (h[0] == 'G') {
      std::string j = "[";
      for (size_t i = 0; i < pars.size(); i++) {
        ParameterInfo pi; memset(&pi, 0, sizeof pi); int32_t pidx = -1;
        for (int k = 0, n = ctrl->getParameterCount(); k < n; k++) { ParameterInfo t; memset(&t, 0, sizeof t); if (ctrl->getParameterInfo(k, t) == kOk && t.id == pars[i].id) { pi = t; pidx = k; break; } }
        if (pidx < 0) continue;
        double v = ctrl->getParamNormalized(pi.id); String128 disp; memset(disp, 0, sizeof disp); ctrl->getParamStringByValue(pi.id, v, disp);
        char b[64]; snprintf(b, sizeof b, "%s{\"i\":%d,\"value\":%.6f,\"name\":\"", j.size() > 1 ? "," : "", (int)i, v);
        j += b; j += jsonEsc(utf8(pi.title, 128)); j += "\",\"label\":\""; j += jsonEsc(utf8(pi.units, 128)); j += "\",\"display\":\""; j += jsonEsc(utf8(disp, 128)); j += "\"}";
      }
      j += "]"; writeFrame('G', j.data(), (uint32_t)j.size());
    }
  }
  proc->setProcessing(0); comp->setActive(0);
  if (ctrl && ctrlSeparate) ctrl->terminate();
  comp->terminate();
  fflush(stdout);
#ifdef _WIN32
  ExitProcess(0);                                                                                   // no unloading: some plugins crash in their static destructors
#else
  _exit(0);
#endif
  return 0;
}

static std::string str(AEffect* fx, int op, int idx) { char buf[256]; memset(buf, 0, sizeof buf); fx->dispatcher(fx, op, idx, 0, buf, 0); buf[255] = 0; return buf; }

int main(int argc, char** argv) {
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY); _setmode(_fileno(stdout), _O_BINARY);
#endif
  setvbuf(stdout, nullptr, _IOFBF, 1 << 16);
  if (argc < 2) { hello(false, "usage: PluginHost <plugin file> [sampleRate] [blockSize]"); return 2; }
  std::string path = argv[1];
  if (argc > 2) gSampleRate = atof(argv[2]) > 1000 ? atof(argv[2]) : 48000;
  if (argc > 3) gBlock = atoi(argv[3]) >= 32 && atoi(argv[3]) <= 8192 ? atoi(argv[3]) : 512;
  if (endsWith(path, ".vst3")) return runVst3(path, argc > 4 ? argv[4] : "");
  std::string err; void* lib = openLibrary(path, err);
  if (!lib) { hello(false, "could not load the plugin: " + err); return 3; }
  VstMain entry = (VstMain)symbol(lib, "VSTPluginMain"); if (!entry) entry = (VstMain)symbol(lib, "main"); if (!entry) entry = (VstMain)symbol(lib, "main_macho");
  if (!entry) { hello(false, "no VSTPluginMain / main export: not a VST2 plugin"); return 3; }
  AEffect* fx = entry(hostCallback);
  if (!fx || fx->magic != kEffectMagic) { hello(false, "the plugin did not return a VST effect"); return 3; }
  if (fx->flags & effFlagsIsSynth) { hello(false, "instrument plugins are not supported: only effects"); return 3; }
  if (!(fx->flags & effFlagsCanReplacing) || !fx->processReplacing) { hello(false, "the plugin has no processReplacing (very old VST 1 effect)"); return 3; }
  fx->dispatcher(fx, effOpen, 0, 0, nullptr, 0);
  fx->dispatcher(fx, effSetSampleRate, 0, 0, nullptr, (float)gSampleRate);
  fx->dispatcher(fx, effSetBlockSize, 0, gBlock, nullptr, 0);
  fx->dispatcher(fx, effMainsChanged, 0, 1, nullptr, 0);
  fx->dispatcher(fx, effStartProcess, 0, 0, nullptr, 0);

  const int nIn = fx->numInputs > 0 ? fx->numInputs : 0, nOut = fx->numOutputs > 0 ? fx->numOutputs : 0;
  if (nOut < 1) { hello(false, "the plugin has no audio outputs"); return 3; }
  char extra[600]; std::string name = str(fx, effGetEffectName, 0), vendor = str(fx, effGetVendorString, 0);
  snprintf(extra, sizeof extra, ",\"name\":\"%s\",\"vendor\":\"%s\",\"inputs\":%d,\"outputs\":%d,\"params\":%d,\"latency\":%d", jsonEsc(name).c_str(), jsonEsc(vendor).c_str(), nIn, nOut, fx->numParams, fx->initialDelay);
  hello(true, "", extra);

  const int chIn = nIn > 2 ? nIn : 2, chOut = nOut > 2 ? nOut : 2;
  std::vector<std::vector<float> > in(chIn, std::vector<float>(gBlock)), out(chOut, std::vector<float>(gBlock));
  std::vector<float*> pin(chIn), pout(chOut);
  for (int c = 0; c < chIn; c++) pin[c] = in[c].data();
  for (int c = 0; c < chOut; c++) pout[c] = out[c].data();

  for (;;) {
    unsigned char h[8]; if (!readAll(h, 8)) break;
    uint32_t len = h[4] | (h[5] << 8) | (h[6] << 16) | ((uint32_t)h[7] << 24);
    if (len > (16u << 20)) break;                                   // refuse absurd frames
    std::vector<unsigned char> body(len); if (len && !readAll(body.data(), len)) break;
    if (h[0] == 'Q') break;
    if (h[0] == 'A') {
      const int frames = (int)(len / 8); const float* src = (const float*)body.data(); std::vector<float> dst((size_t)frames * 2);
      for (int pos = 0; pos < frames; pos += gBlock) {
        const int n = frames - pos < gBlock ? frames - pos : gBlock;
        for (int c = 0; c < chIn; c++) std::fill(in[c].begin(), in[c].end(), 0.0f);
        for (int i = 0; i < n; i++) { in[0][i] = src[(size_t)(pos + i) * 2]; in[1][i] = src[(size_t)(pos + i) * 2 + 1]; }
        if (nIn == 1) for (int i = 0; i < n; i++) in[0][i] = 0.5f * (in[0][i] + in[1][i]);      // mono plugin: sum the stereo input
        for (int c = 0; c < chOut; c++) std::fill(out[c].begin(), out[c].end(), 0.0f);
        fx->processReplacing(fx, pin.data(), pout.data(), n);
        for (int i = 0; i < n; i++) { dst[(size_t)(pos + i) * 2] = out[0][i]; dst[(size_t)(pos + i) * 2 + 1] = out[nOut > 1 ? 1 : 0][i]; }
      }
      writeFrame('A', dst.data(), (uint32_t)(dst.size() * sizeof(float)));
    } else if (h[0] == 'S' && len >= 8) {
      uint32_t idx; float v; memcpy(&idx, body.data(), 4); memcpy(&v, body.data() + 4, 4);
      if ((int32_t)idx >= 0 && (int32_t)idx < fx->numParams && fx->setParameter) fx->setParameter(fx, (int32_t)idx, v < 0 ? 0 : v > 1 ? 1 : v);
    } else if (h[0] == 'G') {
      std::string j = "[";
      for (int i = 0; i < fx->numParams && i < 256; i++) {
        char b[160]; float v = fx->getParameter ? fx->getParameter(fx, i) : 0;
        snprintf(b, sizeof b, "%s{\"i\":%d,\"value\":%.6f,\"name\":\"", i ? "," : "", i, v);
        j += b; j += jsonEsc(str(fx, effGetParamName, i)); j += "\",\"label\":\""; j += jsonEsc(str(fx, effGetParamLabel, i));
        j += "\",\"display\":\""; j += jsonEsc(str(fx, effGetParamDisplay, i)); j += "\"}";
      }
      j += "]"; writeFrame('G', j.data(), (uint32_t)j.size());
    }
  }
  fx->dispatcher(fx, effStopProcess, 0, 0, nullptr, 0);
  fx->dispatcher(fx, effMainsChanged, 0, 0, nullptr, 0);
  fx->dispatcher(fx, effClose, 0, 0, nullptr, 0);
  return 0;
}
