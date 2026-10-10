// A tiny VST 2.x effect used by the tests: one parameter "Gain" (0..1, 0.5 = unity, 1 = +6 dB), stereo in / stereo out.
// Build:  g++ -shared -fPIC -std=c++11 -o gain.so GainPlugin.cpp        (Windows: x86_64-w64-mingw32-g++ -shared -o Gain.dll GainPlugin.cpp)
#include <cstdint>
#include <cstring>
#include <cstdio>
typedef intptr_t VstIntPtr;
struct AEffect;
typedef VstIntPtr (*AudioMasterCallback)(AEffect*, int32_t, int32_t, VstIntPtr, void*, float);
struct AEffect {
  int32_t magic; VstIntPtr (*dispatcher)(AEffect*, int32_t, int32_t, VstIntPtr, void*, float);
  void (*process)(AEffect*, float**, float**, int32_t); void (*setParameter)(AEffect*, int32_t, float); float (*getParameter)(AEffect*, int32_t);
  int32_t numPrograms, numParams, numInputs, numOutputs, flags; VstIntPtr resvd1, resvd2; int32_t initialDelay, realQualities, offQualities; float ioRatio;
  void* object; void* user; int32_t uniqueID, version;
  void (*processReplacing)(AEffect*, float**, float**, int32_t); void (*processDoubleReplacing)(AEffect*, double**, double**, int32_t); char future[56];
};
static float gain = 0.5f; static AEffect effect;
static VstIntPtr dispatch(AEffect*, int32_t op, int32_t idx, VstIntPtr, void* ptr, float) {
  char* s = (char*)ptr;
  switch (op) {
    case 45: strcpy(s, "Test Gain"); return 1;       // effGetEffectName
    case 47: strcpy(s, "Audio Mixer tests"); return 1; // effGetVendorString
    case 8: strcpy(s, "Gain"); return 1;             // effGetParamName
    case 6: strcpy(s, "x"); return 1;                // effGetParamLabel
    case 7: snprintf(s, 24, "%.2f", gain * 2); return 1; // effGetParamDisplay
    default: return 0;
  }
}
static void setParam(AEffect*, int32_t i, float v) { if (i == 0) gain = v; }
static float getParam(AEffect*, int32_t i) { return i == 0 ? gain : 0; }
static void processReplacing(AEffect*, float** in, float** out, int32_t n) { for (int c = 0; c < 2; c++) for (int i = 0; i < n; i++) out[c][i] = in[c][i] * gain * 2.0f; }
static void processOld(AEffect*, float** in, float** out, int32_t n) { for (int c = 0; c < 2; c++) for (int i = 0; i < n; i++) out[c][i] += in[c][i] * gain * 2.0f; }
extern "C" __attribute__((visibility("default"))) AEffect* VSTPluginMain(AudioMasterCallback cb) {
  if (!cb) return nullptr;
  memset(&effect, 0, sizeof effect);
  effect.magic = 0x56737450; effect.dispatcher = dispatch; effect.process = processOld; effect.setParameter = setParam; effect.getParameter = getParam;
  effect.numPrograms = 1; effect.numParams = 1; effect.numInputs = 2; effect.numOutputs = 2; effect.flags = 1 << 4; effect.uniqueID = 0x47414e31; effect.version = 1000;
  effect.processReplacing = processReplacing;
  return &effect;
}
