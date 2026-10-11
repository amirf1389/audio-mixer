// PluginHost - runs ONE VST 2.x effect plugin (.dll on Windows, .vst bundle on macOS, .so / .vst on Linux) in its own process.
// The Audio Mixer bridge starts it, sends audio blocks on stdin and reads the processed blocks from stdout (binary frames, below).
// A crashing plugin takes down only this process, never the mixer. The plugin is loaded from the path given on the command line; the
// bridge only ever passes paths of plugins its own scan found. VST3 is not supported by this host (the SDK is a separate piece of work).
//
//   PluginHost <plugin file> [sampleRate=48000] [blockSize=512]
//
// Frame (both directions): u8 type, 3 bytes zero, u32 little-endian payload length, payload.
//   host -> bridge  'H' hello JSON {"ok":true,"name":..,"vendor":..,"inputs":n,"outputs":n,"params":n,"latency":frames} or {"ok":false,"error":..}
//   bridge -> host  'A' interleaved stereo float32 -> host replies 'A' with the processed interleaved stereo float32 (same length)
//   bridge -> host  'S' u32 index + float32 value (set parameter, no reply)
//   bridge -> host  'G' (get parameters) -> host replies 'G' JSON [{"i":0,"name":"Gain","label":"dB","display":"0.0","value":0.5},...]
//   bridge -> host  'Q' quit
//
// VST 2.x is declared here from the public binary interface (struct layout and opcodes); no Steinberg code is included.
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
  if (path.size() > 5 && path.compare(path.size() - 5, 5, ".vst3") == 0) { hello(false, "VST3 plugins are not supported by this host yet (VST2 .dll / .vst only)"); return 3; }
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
