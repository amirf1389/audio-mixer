// JNI over the C SDK (sdk/c/include/amix.h) for com.amfb.amixos.Engine: an offline mixer the Kotlin side feeds with float blocks
// (a microphone through AudioRecord, a file, or a test tone) and reads back as stereo + meters. Audio I/O stays in Kotlin for now.
#include <jni.h>

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
