// Node.js addon (N-API) of the Audio Mixer C++ engine: the bridge's Int16 level meter, the loudness meter, the scope sweep and the mixer, as classes of JavaScript.
// Build: node scripts/build-engine.js --node   (g++ and the Node.js headers; no node-gyp, no packages). Optional: the bridge works without it.
#include <node_api.h>

#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include "audiomixer/engine.hpp"

namespace {

#define CHECK(call) do { if ((call) != napi_ok) { napi_throw_error(env, nullptr, "N-API call failed: " #call); return nullptr; } } while (0)

napi_value num(napi_env env, double v) { napi_value r; napi_create_double(env, v, &r); return r; }
void setNum(napi_env env, napi_value o, const char* k, double v) { napi_set_named_property(env, o, k, num(env, v)); }
void setBool(napi_env env, napi_value o, const char* k, bool v) { napi_value b; napi_get_boolean(env, v, &b); napi_set_named_property(env, o, k, b); }
double getNum(napi_env env, napi_value o, const char* k, double dflt) {
  napi_value v; napi_valuetype t; if (napi_get_named_property(env, o, k, &v) != napi_ok || napi_typeof(env, v, &t) != napi_ok || t != napi_number) return dflt;
  double d = dflt; napi_get_value_double(env, v, &d); return d;
}
bool floats(napi_env env, napi_value v, float** p, size_t* n) {
  bool ok = false; napi_is_typedarray(env, v, &ok); if (!ok) return false;
  napi_typedarray_type t; napi_value ab; size_t len, off; void* data;
  if (napi_get_typedarray_info(env, v, &t, &len, &data, &ab, &off) != napi_ok || t != napi_float32_array) return false;
  *p = static_cast<float*>(data); *n = len; return true;
}
double dbRound(double v) { if (v <= -90.0) return -90.0; const double r = std::copysign(std::floor(std::fabs(v) * 10.0 + 0.5) / 10.0, v); return r == 0 ? 0.0 : std::fmax(-90.0, r); }   // one decimal, never "-0"

template <class T> void finalize(napi_env, void* data, void*) { delete static_cast<T*>(data); }
template <class T> T* unwrap(napi_env env, napi_callback_info info, napi_value* argv, size_t* argc) {
  napi_value self; void* p = nullptr;
  if (napi_get_cb_info(env, info, argc, argv, &self, nullptr) != napi_ok || napi_unwrap(env, self, &p) != napi_ok) { napi_throw_error(env, nullptr, "not an engine object"); return nullptr; }
  return static_cast<T*>(p);
}
template <class T, class... A> napi_value construct(napi_env env, napi_callback_info info, A... a) {
  napi_value self; napi_get_cb_info(env, info, nullptr, nullptr, &self, nullptr);
  napi_wrap(env, self, new T(a...), finalize<T>, nullptr, nullptr);
  return self;
}

// ── Meter: interleaved Int16 PCM, the numbers of bridge/levels.js ──
napi_value MeterNew(napi_env env, napi_callback_info info) {
  napi_value argv[2]; size_t argc = 2; napi_value self; napi_get_cb_info(env, info, &argc, argv, &self, nullptr);
  double ch = 2, db = -0.1; if (argc > 0) napi_get_value_double(env, argv[0], &ch); if (argc > 1) napi_get_value_double(env, argv[1], &db);
  napi_wrap(env, self, new am::S16Meter(static_cast<int>(std::fmax(1, std::fmin(ch, 64))), db), finalize<am::S16Meter>, nullptr, nullptr);
  return self;
}
napi_value MeterPush(napi_env env, napi_callback_info info) {
  napi_value argv[1]; size_t argc = 1; auto* m = unwrap<am::S16Meter>(env, info, argv, &argc); if (!m || argc < 1) return nullptr;
  void* data; size_t len; if (napi_get_buffer_info(env, argv[0], &data, &len) != napi_ok) { napi_throw_type_error(env, nullptr, "push(buffer): a Buffer is expected"); return nullptr; }
  std::vector<std::int16_t> s(len / 2); std::memcpy(s.data(), data, s.size() * 2);               // a Buffer may start at an odd address: copy
  m->push(s.data(), s.size());
  return nullptr;
}
napi_value MeterSetClip(napi_env env, napi_callback_info info) {
  napi_value argv[1]; size_t argc = 1; auto* m = unwrap<am::S16Meter>(env, info, argv, &argc); if (!m || argc < 1) return nullptr;
  double db; napi_get_value_double(env, argv[0], &db); m->setClipDb(db); return nullptr;
}
napi_value MeterSnapshot(napi_env env, napi_callback_info info) {
  size_t argc = 0; auto* m = unwrap<am::S16Meter>(env, info, nullptr, &argc); if (!m) return nullptr;
  const am::S16Snapshot s = m->snapshot(); napi_value o, peak, rms, ch; napi_create_object(env, &o); napi_create_array(env, &peak); napi_create_array(env, &rms); napi_create_array(env, &ch);
  for (size_t c = 0; c < s.peakDb.size(); ++c) { napi_set_element(env, peak, static_cast<uint32_t>(c), num(env, dbRound(s.peakDb[c]))); napi_set_element(env, rms, static_cast<uint32_t>(c), num(env, dbRound(s.rmsDb[c]))); napi_value b; napi_get_boolean(env, s.clipCh[c], &b); napi_set_element(env, ch, static_cast<uint32_t>(c), b); }
  napi_set_named_property(env, o, "peak", peak); napi_set_named_property(env, o, "rms", rms); napi_set_named_property(env, o, "clipCh", ch);
  setNum(env, o, "clip", s.clip); setBool(env, o, "clipping", s.clipping); setNum(env, o, "clipRuns", s.clipRuns); setNum(env, o, "frames", s.frames);
  return o;
}

// ── Loudness ──
napi_value LoudNew(napi_env env, napi_callback_info info) {
  napi_value argv[1]; size_t argc = 1; napi_value self; napi_get_cb_info(env, info, &argc, argv, &self, nullptr); double fs = 48000; if (argc > 0) napi_get_value_double(env, argv[0], &fs);
  napi_wrap(env, self, new am::Loudness(fs), finalize<am::Loudness>, nullptr, nullptr); return self;
}
napi_value LoudFeed(napi_env env, napi_callback_info info) {
  napi_value argv[2]; size_t argc = 2; auto* m = unwrap<am::Loudness>(env, info, argv, &argc); if (!m || argc < 2) return nullptr;
  float *l, *r; size_t nl, nr; if (!floats(env, argv[0], &l, &nl) || !floats(env, argv[1], &r, &nr)) { napi_throw_type_error(env, nullptr, "feed(left, right): two Float32Arrays"); return nullptr; }
  m->feed(l, r, nl < nr ? nl : nr); return nullptr;
}
napi_value LoudReset(napi_env env, napi_callback_info info) { size_t argc = 0; auto* m = unwrap<am::Loudness>(env, info, nullptr, &argc); if (m) m->reset(); return nullptr; }
napi_value LoudValues(napi_env env, napi_callback_info info) {
  size_t argc = 0; auto* m = unwrap<am::Loudness>(env, info, nullptr, &argc); if (!m) return nullptr; napi_value o; napi_create_object(env, &o);
  setNum(env, o, "momentary", m->momentary()); setNum(env, o, "shortTerm", m->shortTerm()); setNum(env, o, "integrated", m->integrated()); setNum(env, o, "lra", m->range()); setNum(env, o, "truePeak", m->truePeak());
  return o;
}

// ── scope sweep ──
napi_value Scope(napi_env env, napi_callback_info info) {
  napi_value argv[3]; size_t argc = 3; napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr); float* a; size_t n;
  if (argc < 3 || !floats(env, argv[0], &a, &n)) { napi_throw_type_error(env, nullptr, "scope(samples: Float32Array, sampleRate, { timebase, triggerLevel, triggerEdge })"); return nullptr; }
  double fs; napi_get_value_double(env, argv[1], &fs); am::ScopeConfig c; c.timebaseMs = getNum(env, argv[2], "timebase", 1.0); c.triggerDb = getNum(env, argv[2], "triggerLevel", -12.0);
  napi_value e; bool fall = false; if (napi_get_named_property(env, argv[2], "triggerEdge", &e) == napi_ok) { char buf[8] = {0}; size_t len; if (napi_get_value_string_utf8(env, e, buf, sizeof buf, &len) == napi_ok) fall = std::strcmp(buf, "fall") == 0; } c.rising = !fall;
  const am::ScopeResult r = am::scopeSweep(a, n, fs, c); napi_value o; napi_create_object(env, &o);
  setNum(env, o, "N", static_cast<double>(r.n)); setNum(env, o, "start", static_cast<double>(r.start)); setBool(env, o, "triggered", r.triggered); setNum(env, o, "p2p", r.p2p); setNum(env, o, "rms", r.rms); setNum(env, o, "crest", r.crestDb); setNum(env, o, "freq", r.freq);
  return o;
}

// ── Mixer ──
napi_value MixNew(napi_env env, napi_callback_info info) {
  napi_value argv[2]; size_t argc = 2; napi_value self; napi_get_cb_info(env, info, &argc, argv, &self, nullptr); double n = 8, fs = 48000; if (argc > 0) napi_get_value_double(env, argv[0], &n); if (argc > 1) napi_get_value_double(env, argv[1], &fs);
  napi_wrap(env, self, new am::Mixer(static_cast<int>(n), fs), finalize<am::Mixer>, nullptr, nullptr); return self;
}
napi_value MixStrip(napi_env env, napi_callback_info info) {   // setStrip(i, { gainDb, pan, mute, solo })
  napi_value argv[2]; size_t argc = 2; auto* m = unwrap<am::Mixer>(env, info, argv, &argc); if (!m || argc < 2) return nullptr; double i; napi_get_value_double(env, argv[0], &i); const int s = static_cast<int>(i);
  napi_value v; bool b;
  if (napi_has_named_property(env, argv[1], "gainDb", &b) == napi_ok && b) m->setGainDb(s, getNum(env, argv[1], "gainDb", 0));
  if (napi_has_named_property(env, argv[1], "pan", &b) == napi_ok && b) m->setPan(s, getNum(env, argv[1], "pan", 0));
  if (napi_has_named_property(env, argv[1], "mute", &b) == napi_ok && b) { napi_get_named_property(env, argv[1], "mute", &v); napi_get_value_bool(env, v, &b); m->setMute(s, b); }
  if (napi_has_named_property(env, argv[1], "solo", &b) == napi_ok && b) { napi_get_named_property(env, argv[1], "solo", &v); napi_get_value_bool(env, v, &b); m->setSolo(s, b); }
  return nullptr;
}
napi_value MixMaster(napi_env env, napi_callback_info info) {   // setMaster({ gainDb, limiter, ceilingDb, releaseMs, clipDb })
  napi_value argv[1]; size_t argc = 1; auto* m = unwrap<am::Mixer>(env, info, argv, &argc); if (!m || argc < 1) return nullptr; bool has;
  if (napi_has_named_property(env, argv[0], "gainDb", &has) == napi_ok && has) m->setMasterDb(getNum(env, argv[0], "gainDb", 0));
  if (napi_has_named_property(env, argv[0], "clipDb", &has) == napi_ok && has) m->setClipDb(getNum(env, argv[0], "clipDb", -0.1));
  if (napi_has_named_property(env, argv[0], "limiter", &has) == napi_ok && has) { napi_value v; bool on; napi_get_named_property(env, argv[0], "limiter", &v); napi_get_value_bool(env, v, &on); m->setLimiter(on, getNum(env, argv[0], "ceilingDb", -0.5), getNum(env, argv[0], "releaseMs", 100)); }
  return nullptr;
}
napi_value MixProcess(napi_env env, napi_callback_info info) {   // process([Float32Array per strip], outL, outR)
  napi_value argv[3]; size_t argc = 3; auto* m = unwrap<am::Mixer>(env, info, argv, &argc); if (!m || argc < 3) return nullptr;
  float *ol, *orr; size_t nl, nr; uint32_t cnt = 0; napi_get_array_length(env, argv[0], &cnt);
  if (!floats(env, argv[1], &ol, &nl) || !floats(env, argv[2], &orr, &nr) || static_cast<int>(cnt) < m->strips()) { napi_throw_type_error(env, nullptr, "process(inputs, outL, outR): one Float32Array per strip, two Float32Arrays out"); return nullptr; }
  std::vector<const float*> in(static_cast<size_t>(m->strips())); size_t frames = nl < nr ? nl : nr;
  for (int s = 0; s < m->strips(); ++s) { napi_value v; napi_get_element(env, argv[0], static_cast<uint32_t>(s), &v); float* p; size_t n; if (!floats(env, v, &p, &n) || n < frames) { napi_throw_type_error(env, nullptr, "an input is not a Float32Array of enough samples"); return nullptr; } in[static_cast<size_t>(s)] = p; }
  m->process(in.data(), frames, ol, orr); return nullptr;
}
napi_value MixState(napi_env env, napi_callback_info info) {
  size_t argc = 0; auto* m = unwrap<am::Mixer>(env, info, nullptr, &argc); if (!m) return nullptr; napi_value o, strips, master, ld, ph; napi_create_object(env, &o); napi_create_array(env, &strips); napi_create_array(env, &master); napi_create_object(env, &ld); napi_create_object(env, &ph);
  auto meter = [&](const am::MeterState& s) { napi_value x; napi_create_object(env, &x); setNum(env, x, "pk", s.pk); setNum(env, x, "rms", s.rms); setNum(env, x, "hold", s.hold); setNum(env, x, "clipN", s.clipN); setBool(env, x, "latched", s.latched); return x; };
  for (int s = 0; s < m->strips(); ++s) napi_set_element(env, strips, static_cast<uint32_t>(s), meter(m->strip(s).meter.state()));
  for (int c = 0; c < 2; ++c) napi_set_element(env, master, static_cast<uint32_t>(c), meter(m->masterMeter(c)));
  setNum(env, ld, "momentary", m->loudness().momentary()); setNum(env, ld, "shortTerm", m->loudness().shortTerm()); setNum(env, ld, "integrated", m->loudness().integrated()); setNum(env, ld, "lra", m->loudness().range()); setNum(env, ld, "truePeak", m->loudness().truePeak());
  setNum(env, ph, "corr", m->phase().corr); setNum(env, ph, "peak", m->phase().peak); setBool(env, ph, "silent", m->phase().silent);
  napi_set_named_property(env, o, "strips", strips); napi_set_named_property(env, o, "master", master); napi_set_named_property(env, o, "loudness", ld); napi_set_named_property(env, o, "phase", ph);
  return o;
}

napi_value Init(napi_env env, napi_value exports) {
  napi_value ctor;
  { napi_property_descriptor p[] = {{"push", nullptr, MeterPush, nullptr, nullptr, nullptr, napi_default, nullptr}, {"snapshot", nullptr, MeterSnapshot, nullptr, nullptr, nullptr, napi_default, nullptr}, {"setClipDb", nullptr, MeterSetClip, nullptr, nullptr, nullptr, napi_default, nullptr}};
    napi_define_class(env, "Meter", NAPI_AUTO_LENGTH, MeterNew, nullptr, 3, p, &ctor); napi_set_named_property(env, exports, "Meter", ctor); }
  { napi_property_descriptor p[] = {{"feed", nullptr, LoudFeed, nullptr, nullptr, nullptr, napi_default, nullptr}, {"reset", nullptr, LoudReset, nullptr, nullptr, nullptr, napi_default, nullptr}, {"values", nullptr, LoudValues, nullptr, nullptr, nullptr, napi_default, nullptr}};
    napi_define_class(env, "Loudness", NAPI_AUTO_LENGTH, LoudNew, nullptr, 3, p, &ctor); napi_set_named_property(env, exports, "Loudness", ctor); }
  { napi_property_descriptor p[] = {{"setStrip", nullptr, MixStrip, nullptr, nullptr, nullptr, napi_default, nullptr}, {"setMaster", nullptr, MixMaster, nullptr, nullptr, nullptr, napi_default, nullptr}, {"process", nullptr, MixProcess, nullptr, nullptr, nullptr, napi_default, nullptr}, {"state", nullptr, MixState, nullptr, nullptr, nullptr, napi_default, nullptr}};
    napi_define_class(env, "Mixer", NAPI_AUTO_LENGTH, MixNew, nullptr, 4, p, &ctor); napi_set_named_property(env, exports, "Mixer", ctor); }
  napi_value fn; napi_create_function(env, "scope", NAPI_AUTO_LENGTH, Scope, nullptr, &fn); napi_set_named_property(env, exports, "scope", fn);
  napi_set_named_property(env, exports, "version", num(env, 1));
  return exports;
}

}  // namespace

NAPI_MODULE(am_engine, Init)
