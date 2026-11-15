// JNI over the C SDK (sdk/c/include/amix.h) for com.amfb.amixos.Engine: an offline mixer the Kotlin side feeds with float blocks
// (a microphone through AudioRecord, a file, or a test tone) and reads back as stereo + meters. Audio I/O stays in Kotlin for now.
#include <jni.h>

#include <algorithm>
#include <string>
#include <vector>

#include "amix.h"

namespace {
amix_mixer* mixerOf(jlong h) { return reinterpret_cast<amix_mixer*>(h); }
jstring str(JNIEnv* env, char* s) { jstring j = env->NewStringUTF(s ? s : ""); amix_free(s); return j; }
}  // namespace

extern "C" {
JNIEXPORT jstring JNICALL Java_com_amfb_amixos_Engine_version(JNIEnv* env, jclass) { return env->NewStringUTF(amix_version()); }
JNIEXPORT void JNICALL Java_com_amfb_amixos_Engine_setDirs(JNIEnv* env, jclass, jstring d, jstring c, jstring t) { const char* a = env->GetStringUTFChars(d, nullptr); const char* b = env->GetStringUTFChars(c, nullptr); const char* e = env->GetStringUTFChars(t, nullptr); amix_set_dirs(a, b, e); env->ReleaseStringUTFChars(d, a); env->ReleaseStringUTFChars(c, b); env->ReleaseStringUTFChars(t, e); }
JNIEXPORT jstring JNICALL Java_com_amfb_amixos_Engine_pathsJson(JNIEnv* env, jclass) { return str(env, amix_paths_json()); }
JNIEXPORT jstring JNICALL Java_com_amfb_amixos_Engine_devicesJson(JNIEnv* env, jclass) { return str(env, amix_devices_json()); }
JNIEXPORT jstring JNICALL Java_com_amfb_amixos_Engine_licenseJson(JNIEnv* env, jclass) { return str(env, amix_license_json()); }
JNIEXPORT jlong JNICALL Java_com_amfb_amixos_Engine_create(JNIEnv*, jclass, jint strips, jdouble rate) { return reinterpret_cast<jlong>(amix_mixer_create(strips, rate)); }
JNIEXPORT void JNICALL Java_com_amfb_amixos_Engine_destroy(JNIEnv*, jclass, jlong h) { amix_mixer_destroy(mixerOf(h)); }
JNIEXPORT void JNICALL Java_com_amfb_amixos_Engine_setGain(JNIEnv*, jclass, jlong h, jint s, jdouble db) { amix_mixer_set_gain(mixerOf(h), s, db); }
JNIEXPORT void JNICALL Java_com_amfb_amixos_Engine_setPan(JNIEnv*, jclass, jlong h, jint s, jdouble pan) { amix_mixer_set_pan(mixerOf(h), s, pan); }
JNIEXPORT void JNICALL Java_com_amfb_amixos_Engine_setMute(JNIEnv*, jclass, jlong h, jint s, jboolean on) { amix_mixer_set_mute(mixerOf(h), s, on ? 1 : 0); }
JNIEXPORT void JNICALL Java_com_amfb_amixos_Engine_setSolo(JNIEnv*, jclass, jlong h, jint s, jboolean on) { amix_mixer_set_solo(mixerOf(h), s, on ? 1 : 0); }
JNIEXPORT void JNICALL Java_com_amfb_amixos_Engine_setMaster(JNIEnv*, jclass, jlong h, jdouble db) { amix_mixer_set_master(mixerOf(h), db); }
JNIEXPORT void JNICALL Java_com_amfb_amixos_Engine_setLimiter(JNIEnv*, jclass, jlong h, jboolean on, jdouble ceil, jdouble rel) { amix_mixer_set_limiter(mixerOf(h), on ? 1 : 0, ceil, rel); }
// inputs: one FloatArray per strip (null = silence); out: a FloatArray of 2 * frames, interleaved L R. Returns frames processed.
JNIEXPORT jint JNICALL Java_com_amfb_amixos_Engine_process(JNIEnv* env, jclass, jlong h, jobjectArray inputs, jint frames, jfloatArray out) {
  amix_mixer* m = mixerOf(h); if (!m || frames <= 0) return 0;
  const int n = amix_mixer_strips(m); const int given = inputs ? env->GetArrayLength(inputs) : 0;
  std::vector<std::vector<float>> bufs(static_cast<std::size_t>(n), std::vector<float>(static_cast<std::size_t>(frames), 0.f)); std::vector<const float*> ptrs(static_cast<std::size_t>(n));
  for (int s = 0; s < n; ++s) {
    if (s < given) { jfloatArray a = static_cast<jfloatArray>(env->GetObjectArrayElement(inputs, s)); if (a) { const jsize len = env->GetArrayLength(a); env->GetFloatArrayRegion(a, 0, len < frames ? len : frames, bufs[static_cast<std::size_t>(s)].data()); env->DeleteLocalRef(a); } }
    ptrs[static_cast<std::size_t>(s)] = bufs[static_cast<std::size_t>(s)].data();
  }
  std::vector<float> L(static_cast<std::size_t>(frames)), R(static_cast<std::size_t>(frames)), inter(static_cast<std::size_t>(frames) * 2);
  const int done = amix_mixer_process(m, ptrs.data(), frames, L.data(), R.data());
  for (int i = 0; i < done; ++i) { inter[static_cast<std::size_t>(2 * i)] = L[static_cast<std::size_t>(i)]; inter[static_cast<std::size_t>(2 * i + 1)] = R[static_cast<std::size_t>(i)]; }
  if (out && env->GetArrayLength(out) >= 2 * done) env->SetFloatArrayRegion(out, 0, 2 * done, inter.data());
  return done;
}
// peaks: strips (n) then master L, R, as linear values into a FloatArray of at least n + 2
JNIEXPORT void JNICALL Java_com_amfb_amixos_Engine_peaks(JNIEnv* env, jclass, jlong h, jfloatArray out) {
  amix_mixer* m = mixerOf(h); if (!m || !out) return; amix_levels lv; amix_mixer_levels(m, &lv); const int n = amix_mixer_strips(m);
  std::vector<float> v(static_cast<std::size_t>(n) + 2); for (int s = 0; s < n; ++s) v[static_cast<std::size_t>(s)] = static_cast<float>(lv.strips[s].peak); v[static_cast<std::size_t>(n)] = static_cast<float>(lv.master[0].peak); v[static_cast<std::size_t>(n) + 1] = static_cast<float>(lv.master[1].peak);
  if (env->GetArrayLength(out) >= n + 2) env->SetFloatArrayRegion(out, 0, n + 2, v.data());
}
}

// ---- AAUDIO: the native low-latency path (Android 8+, the NDK's recommended API; OpenSL ES is not used). One OUTPUT stream with a data
// callback (low-latency performance mode, float, stereo) drives everything: inside it the INPUT stream (mono microphone) is read
// without blocking, the AmF X1 mixer runs, and the stereo result goes straight to the speaker / headphones. No audio crosses into Java.
// If the device changes (headphones plugged in or out) the stream is disconnected: the error callback marks it so Java restarts it.
#include <aaudio/AAudio.h>
#include <dlfcn.h>
#include <atomic>
namespace {
struct NativeLoop {
  amix_mixer* m = nullptr; AAudioStream* in = nullptr; AAudioStream* out = nullptr;
  std::vector<float> mic, L, R; std::vector<const float*> ptrs; std::atomic<bool> disconnected{false}; std::atomic<int> underruns{0};
};
NativeLoop* g_loop = nullptr;
aaudio_data_callback_result_t onOut(AAudioStream*, void* user, void* audio, int32_t frames) {
  NativeLoop* l = static_cast<NativeLoop*>(user); float* o = static_cast<float*>(audio);
  if (frames > static_cast<int32_t>(l->L.size())) { std::fill(o, o + 2 * frames, 0.f); return AAUDIO_CALLBACK_RESULT_CONTINUE; }   // never allocate here
  int32_t got = 0;
  if (l->in) { got = AAudioStream_read(l->in, l->mic.data(), frames, 0); if (got < 0) got = 0; }   // non-blocking: what has arrived
  if (got < frames) std::fill(l->mic.begin() + got, l->mic.begin() + frames, 0.f);
  const int n = static_cast<int>(l->ptrs.size()); for (int s = 0; s < n; ++s) l->ptrs[static_cast<std::size_t>(s)] = s == 0 ? l->mic.data() : nullptr;   // the microphone on strip 1
  const int done = amix_mixer_process(l->m, l->ptrs.data(), frames, l->L.data(), l->R.data());
  for (int i = 0; i < frames; ++i) { o[2 * i] = i < done ? l->L[static_cast<std::size_t>(i)] : 0.f; o[2 * i + 1] = i < done ? l->R[static_cast<std::size_t>(i)] : 0.f; }
  return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
void onErr(AAudioStream*, void* user, aaudio_result_t e) { if (e == AAUDIO_ERROR_DISCONNECTED) static_cast<NativeLoop*>(user)->disconnected = true; }
AAudioStream* openStream(aaudio_direction_t dir, int rate, int ch, NativeLoop* l, bool callback, int deviceId) {
  AAudioStreamBuilder* b = nullptr; if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return nullptr;
  AAudioStreamBuilder_setDirection(b, dir); if (deviceId > 0) AAudioStreamBuilder_setDeviceId(b, deviceId);   // a chosen interface (USB, headset ...); 0 = the system's route
  AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT); AAudioStreamBuilder_setChannelCount(b, ch);
  AAudioStreamBuilder_setSampleRate(b, rate); AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
  AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);   // shared: survives other apps and device changes; exclusive gains little and disconnects more
  if (dir == AAUDIO_DIRECTION_INPUT) {   // Android 9+: no voice processing on the mic (looked up at run time: the app also runs on Android 8)
    using SetPreset = void (*)(AAudioStreamBuilder*, int32_t);
    static const auto setPreset = reinterpret_cast<SetPreset>(dlsym(RTLD_DEFAULT, "AAudioStreamBuilder_setInputPreset"));
    if (setPreset) setPreset(b, 9 /* AAUDIO_INPUT_PRESET_UNPROCESSED */); }
  if (callback) { AAudioStreamBuilder_setDataCallback(b, onOut, l); AAudioStreamBuilder_setErrorCallback(b, onErr, l); }
  AAudioStream* s = nullptr; const aaudio_result_t r = AAudioStreamBuilder_openStream(b, &s); AAudioStreamBuilder_delete(b);
  return r == AAUDIO_OK ? s : nullptr;
}
void closeLoop() {
  if (!g_loop) return;
  if (g_loop->out) { AAudioStream_requestStop(g_loop->out); AAudioStream_close(g_loop->out); }   // the callback stops before close returns
  if (g_loop->in) { AAudioStream_requestStop(g_loop->in); AAudioStream_close(g_loop->in); }
  delete g_loop; g_loop = nullptr;
}
}  // namespace
extern "C" {
// starts the native loop on mixer h at `rate` (the output may run at the device's own rate: it is returned), mic = open the microphone too.
// Returns the rate in use, 0 = AAudio could not open (Java falls back to its AudioRecord / AudioTrack loop).
JNIEXPORT jint JNICALL Java_com_amfb_amixos_Engine_aaudioStart(JNIEnv*, jclass, jlong h, jint rate, jboolean mic, jint outDev, jint inDev, jint bursts) {
  closeLoop(); amix_mixer* m = mixerOf(h); if (!m) return 0;
  NativeLoop* l = new NativeLoop(); l->m = m; const std::size_t cap = 8192; l->mic.assign(cap, 0.f); l->L.assign(cap, 0.f); l->R.assign(cap, 0.f); l->ptrs.assign(static_cast<std::size_t>(amix_mixer_strips(m)), nullptr);
  l->out = openStream(AAUDIO_DIRECTION_OUTPUT, rate, 2, l, true, outDev);
  if (!l->out && outDev > 0) l->out = openStream(AAUDIO_DIRECTION_OUTPUT, rate, 2, l, true, 0);   // the chosen output is gone: the system route
  if (!l->out) { delete l; return 0; }
  const int real = AAudioStream_getSampleRate(l->out);
  if (mic) { l->in = openStream(AAUDIO_DIRECTION_INPUT, real, 1, l, false, inDev); if (!l->in && inDev > 0) l->in = openStream(AAUDIO_DIRECTION_INPUT, real, 1, l, false, 0); }   // the same rate as the output: no resampling in the callback
  AAudioStream_setBufferSizeInFrames(l->out, AAudioStream_getFramesPerBurst(l->out) * (bursts >= 1 && bursts <= 8 ? bursts : 2));   // bursts of headroom (2 = the guide's low-latency start; the brand profile may ask for more)
  g_loop = l;
  if (l->in) AAudioStream_requestStart(l->in);
  if (AAudioStream_requestStart(l->out) != AAUDIO_OK) { closeLoop(); return 0; }
  return real;
}
JNIEXPORT void JNICALL Java_com_amfb_amixos_Engine_aaudioStop(JNIEnv*, jclass) { closeLoop(); }
// [running, disconnected, framesPerBurst, bufferFrames, xruns, inputOpen, outDeviceId, inDeviceId, sampleRate]
JNIEXPORT void JNICALL Java_com_amfb_amixos_Engine_aaudioState(JNIEnv* env, jclass, jintArray out) {
  jint v[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  if (g_loop && g_loop->out) { v[0] = AAudioStream_getState(g_loop->out) == AAUDIO_STREAM_STATE_STARTED; v[1] = g_loop->disconnected.load(); v[2] = AAudioStream_getFramesPerBurst(g_loop->out);
    v[3] = AAudioStream_getBufferSizeInFrames(g_loop->out); v[4] = AAudioStream_getXRunCount(g_loop->out); v[5] = g_loop->in != nullptr;
    v[6] = AAudioStream_getDeviceId(g_loop->out); v[7] = g_loop->in ? AAudioStream_getDeviceId(g_loop->in) : 0; v[8] = AAudioStream_getSampleRate(g_loop->out); }
  if (out && env->GetArrayLength(out) >= 9) env->SetIntArrayRegion(out, 0, 9, v);
}
}

// ---- AUDIO HAL PROBE: what this phone's audio HAL really grants, through the public AAudio API only. Each direction is opened once
// asking for the best (low latency, exclusive), the GRANTED values are read and the stream is closed at once: nothing is played or
// recorded. out[16]: per direction (output 0..7, input 8..15): opened, performanceMode, sharingMode, framesPerBurst, sampleRate,
// channelCount, bufferCapacity, format. performanceMode 12 = LOW_LATENCY, sharingMode 0 = EXCLUSIVE (the MMAP path) / 1 = SHARED.
extern "C" JNIEXPORT void JNICALL Java_com_amfb_amixos_Engine_aaudioProbe(JNIEnv* env, jclass, jintArray out) {
  jint v[16] = {0};
  for (int k = 0; k < 2; ++k) {
    AAudioStreamBuilder* b = nullptr; if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) continue;
    AAudioStreamBuilder_setDirection(b, k == 0 ? AAUDIO_DIRECTION_OUTPUT : AAUDIO_DIRECTION_INPUT);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY); AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_EXCLUSIVE);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStream* s = nullptr; const bool ok = AAudioStreamBuilder_openStream(b, &s) == AAUDIO_OK && s; AAudioStreamBuilder_delete(b);
    if (!ok) continue;
    jint* d = v + k * 8;
    d[0] = 1; d[1] = AAudioStream_getPerformanceMode(s); d[2] = AAudioStream_getSharingMode(s); d[3] = AAudioStream_getFramesPerBurst(s);
    d[4] = AAudioStream_getSampleRate(s); d[5] = AAudioStream_getChannelCount(s); d[6] = AAudioStream_getBufferCapacityInFrames(s); d[7] = AAudioStream_getFormat(s);
    AAudioStream_close(s);
  }
  if (out && env->GetArrayLength(out) >= 16) env->SetIntArrayRegion(out, 0, 16, v);
}
