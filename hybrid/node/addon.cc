// N-API add-on: attaches to the engine's shared memory. It only reads meters and writes clamped
// parameters; it never touches audio, so nothing a caller passes can stall or crash the audio thread.
#include <node_api.h>
#include <algorithm>
#include <cmath>
#include <string>
#include "../core/shared.hpp"

namespace {
amfb::SharedMem g_mem;
amfb::SharedBlock* g_b = nullptr;

napi_value boolean(napi_env env, bool v) { napi_value r; napi_get_boolean(env, v, &r); return r; }
napi_value number(napi_env env, double v) { napi_value r; napi_create_double(env, v, &r); return r; }
napi_value fail(napi_env env, const char* msg) { napi_throw_error(env, nullptr, msg); return nullptr; }

bool args(napi_env env, napi_callback_info info, size_t want, napi_value* out) {
  size_t n = want;
  return napi_get_cb_info(env, info, &n, out, nullptr, nullptr) == napi_ok && n >= want;
}
bool asNumber(napi_env env, napi_value v, double* out) {
  napi_valuetype t;
  return napi_typeof(env, v, &t) == napi_ok && t == napi_number && napi_get_value_double(env, v, out) == napi_ok && std::isfinite(*out);
}
bool asIndex(napi_env env, napi_value v, int limit, int* out) {
  double d;
  if (!asNumber(env, v, &d) || d != std::floor(d) || d < 0 || d >= limit) return false;
  *out = int(d);
  return true;
}
// reads obj[key] when it is there; false = the key is present but not a finite number / boolean
bool optNumber(napi_env env, napi_value obj, const char* key, double lo, double hi, bool* has, double* out) {
  bool present = false; *has = false;
  if (napi_has_named_property(env, obj, key, &present) != napi_ok || !present) return true;
  napi_value v;
  if (napi_get_named_property(env, obj, key, &v) != napi_ok || !asNumber(env, v, out)) return false;
  *out = std::clamp(*out, lo, hi); *has = true;
  return true;
}
bool optBool(napi_env env, napi_value obj, const char* key, bool* has, bool* out) {
  bool present = false; *has = false;
  if (napi_has_named_property(env, obj, key, &present) != napi_ok || !present) return true;
  napi_value v; napi_valuetype t;
  if (napi_get_named_property(env, obj, key, &v) != napi_ok || napi_typeof(env, v, &t) != napi_ok || t != napi_boolean) return false;
  napi_get_value_bool(env, v, out); *has = true;
  return true;
}
bool isObject(napi_env env, napi_value v) { napi_valuetype t; return napi_typeof(env, v, &t) == napi_ok && t == napi_object; }

napi_value Open(napi_env env, napi_callback_info) {
  if (!g_b) g_b = g_mem.open(false);
  return boolean(env, g_b != nullptr);
}
napi_value Close(napi_env env, napi_callback_info) {
  g_mem.close(); g_b = nullptr;
  return boolean(env, true);
}

// setStrip(index, { gainDb, pan, width, mute, solo, eqOn })
napi_value SetStrip(napi_env env, napi_callback_info info) {
  napi_value a[2]; int i;
  if (!g_b) return fail(env, "the engine is not running");
  if (!args(env, info, 2, a) || !asIndex(env, a[0], amfb::kStrips, &i) || !isObject(env, a[1])) return fail(env, "setStrip(index 0-15, object)");
  auto& p = g_b->p[i];
  bool has, bv; double d;
  if (!optNumber(env, a[1], "gainDb", -90, 12, &has, &d)) return fail(env, "gainDb must be a number"); if (has) p.gainDb = float(d);
  if (!optNumber(env, a[1], "pan", -1, 1, &has, &d)) return fail(env, "pan must be a number"); if (has) p.pan = float(d);
  if (!optNumber(env, a[1], "width", 0, 2, &has, &d)) return fail(env, "width must be a number"); if (has) p.width = float(d);
  if (!optBool(env, a[1], "mute", &has, &bv)) return fail(env, "mute must be true or false"); if (has) p.mute = bv;
  if (!optBool(env, a[1], "solo", &has, &bv)) return fail(env, "solo must be true or false"); if (has) p.solo = bv;
  if (!optBool(env, a[1], "eqOn", &has, &bv)) return fail(env, "eqOn must be true or false"); if (has) p.eqOn = bv;
  g_b->paramGen.fetch_add(1, std::memory_order_release);
  return boolean(env, true);
}

// setEq(index, band, { freq, gainDb, q })
napi_value SetEq(napi_env env, napi_callback_info info) {
  napi_value a[3]; int i, k;
  if (!g_b) return fail(env, "the engine is not running");
  if (!args(env, info, 3, a) || !asIndex(env, a[0], amfb::kStrips, &i) || !asIndex(env, a[1], amfb::kBands, &k) || !isObject(env, a[2]))
    return fail(env, "setEq(index 0-15, band 0-6, object)");
  auto& p = g_b->p[i];
  bool has; double d;
  if (!optNumber(env, a[2], "freq", 20, 20000, &has, &d)) return fail(env, "freq must be a number"); if (has) p.eqFreq[k] = float(d);
  if (!optNumber(env, a[2], "gainDb", -18, 18, &has, &d)) return fail(env, "gainDb must be a number"); if (has) p.eqGain[k] = float(d);
  if (!optNumber(env, a[2], "q", 0.1, 18, &has, &d)) return fail(env, "q must be a number"); if (has) p.eqQ[k] = float(d);
  g_b->paramGen.fetch_add(1, std::memory_order_release);
  return boolean(env, true);
}

napi_value SetMaster(napi_env env, napi_callback_info info) {
  napi_value a[1]; double d;
  if (!g_b) return fail(env, "the engine is not running");
  if (!args(env, info, 1, a) || !asNumber(env, a[0], &d)) return fail(env, "setMaster(gainDb)");
  g_b->masterGainDb = float(std::clamp(d, -90.0, 12.0));
  g_b->paramGen.fetch_add(1, std::memory_order_release);
  return boolean(env, true);
}

void put(napi_env env, napi_value o, const char* k, napi_value v) { napi_set_named_property(env, o, k, v); }

napi_value State(napi_env env, napi_callback_info) {
  if (!g_b) return fail(env, "the engine is not running");
  napi_value o, strips;
  napi_create_object(env, &o); napi_create_array_with_length(env, amfb::kStrips, &strips);
  put(env, o, "sampleRate", number(env, g_b->sampleRate.load()));
  put(env, o, "masterGainDb", number(env, g_b->masterGainDb.load()));
  put(env, o, "heartbeat", number(env, double(g_b->heartbeat.load())));
  put(env, o, "underruns", number(env, g_b->underruns.load()));
  for (int i = 0; i < amfb::kStrips; ++i) {
    const auto& p = g_b->p[i];
    napi_value s, eq, name;
    napi_create_object(env, &s); napi_create_array_with_length(env, amfb::kBands, &eq);
    char label[sizeof g_b->m[i].source + 1] = {};
    std::copy(std::begin(g_b->m[i].source), std::end(g_b->m[i].source), label);
    napi_create_string_utf8(env, label, NAPI_AUTO_LENGTH, &name);
    put(env, s, "source", name);
    put(env, s, "gainDb", number(env, p.gainDb.load())); put(env, s, "pan", number(env, p.pan.load()));
    put(env, s, "width", number(env, p.width.load()));
    put(env, s, "mute", boolean(env, p.mute.load() != 0)); put(env, s, "solo", boolean(env, p.solo.load() != 0));
    put(env, s, "eqOn", boolean(env, p.eqOn.load() != 0));
    for (int k = 0; k < amfb::kBands; ++k) {
      napi_value b; napi_create_object(env, &b);
      put(env, b, "freq", number(env, p.eqFreq[k].load())); put(env, b, "gainDb", number(env, p.eqGain[k].load()));
      put(env, b, "q", number(env, p.eqQ[k].load()));
      napi_set_element(env, eq, uint32_t(k), b);
    }
    put(env, s, "eq", eq);
    napi_set_element(env, strips, uint32_t(i), s);
  }
  put(env, o, "strips", strips);
  return o;
}

// meters() -> Float32Array, 8 values per bus (16 strips, then the master):
// peakL, peakR, rmsL, rmsR, truePeak, hold, phase, active
napi_value Meters(napi_env env, napi_callback_info) {
  if (!g_b) return fail(env, "the engine is not running");
  const size_t n = (amfb::kStrips + 1) * 8;
  void* data = nullptr; napi_value buf, arr;
  if (napi_create_arraybuffer(env, n * sizeof(float), &data, &buf) != napi_ok) return fail(env, "out of memory");
  float* f = static_cast<float*>(data);
  for (int i = 0; i <= amfb::kStrips; ++i) {
    const auto& m = g_b->m[i];
    const float v[8] = {m.peakL.load(), m.peakR.load(), m.rmsL.load(), m.rmsR.load(), m.truePeak.load(), m.hold.load(), m.phase.load(),
                        float(m.active.load())};
    std::copy(v, v + 8, f + i * 8);
  }
  napi_create_typedarray(env, napi_float32_array, n, buf, 0, &arr);
  return arr;
}

napi_value Init(napi_env env, napi_value exports) {
  const napi_property_descriptor d[] = {
      {"open", nullptr, Open, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"close", nullptr, Close, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"setStrip", nullptr, SetStrip, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"setEq", nullptr, SetEq, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"setMaster", nullptr, SetMaster, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"state", nullptr, State, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"meters", nullptr, Meters, nullptr, nullptr, nullptr, napi_default, nullptr},
  };
  napi_define_properties(env, exports, sizeof d / sizeof d[0], d);
  return exports;
}
}  // namespace

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
