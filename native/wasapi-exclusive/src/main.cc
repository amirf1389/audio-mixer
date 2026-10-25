// EXPERIMENTAL, UNWIRED native addon entry point: see native/wasapi-exclusive/README.md before building or calling
// this. Node-API via node-addon-api (not NAN: NAN is deprecated and ABI-unstable; N-API addons are ABI-stable
// across Node versions, so the built .node file does not need to be rebuilt for every Node release) - the same
// choice native/asio-binder made, for the same reason.
#include <napi.h>

#include "wasapi.h"

namespace {

// wasapiExclusive.openExclusive(): a deliberate no-op stub. See wasapi.cc::NotYetIntegrated for why, and
// README.md for what a real integration needs to do first.
Napi::Value OpenExclusive(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
#ifndef _WIN32
  Napi::Error::New(env, "wasapi_exclusive is Windows only").ThrowAsJavaScriptException();
  return env.Undefined();
#else
  std::string reason;
  wasapiexclusive::NotYetIntegrated(reason);
  Napi::Error::New(env, reason).ThrowAsJavaScriptException();
  return env.Undefined();
#endif
}

// wasapiExclusive.listEndpoints(): harmless (read-only IMMDeviceEnumerator listing), usable today. One call, every
// active render + capture endpoint; see wasapi.h.
Napi::Value ListEndpoints(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
#ifndef _WIN32
  Napi::Error::New(env, "wasapi_exclusive is Windows only").ThrowAsJavaScriptException();
  return env.Undefined();
#else
  const auto eps = wasapiexclusive::ListEndpoints();
  Napi::Array out = Napi::Array::New(env, eps.size());
  for (size_t i = 0; i < eps.size(); ++i) {
    Napi::Object o = Napi::Object::New(env);
    o.Set("id", eps[i].id); o.Set("name", eps[i].name);
    o.Set("direction", eps[i].render ? "render" : "capture"); o.Set("isDefault", eps[i].isDefault);
    out[i] = o;
  }
  return out;
#endif
}

// wasapiExclusive.probeExclusiveFormat(endpointId, sampleRate, channels, bitsPerSample): harmless (no stream is
// opened, nothing is reserved); see wasapi.h for exactly what it asks the device and why there is no "closest
// match" the way a shared-mode probe would offer one.
Napi::Value ProbeExclusiveFormat(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
#ifndef _WIN32
  Napi::Error::New(env, "wasapi_exclusive is Windows only").ThrowAsJavaScriptException();
  return env.Undefined();
#else
  if (info.Length() < 4 || !info[0].IsString() || !info[1].IsNumber() || !info[2].IsNumber() || !info[3].IsNumber()) {
    Napi::TypeError::New(env, "probeExclusiveFormat(endpointId: string, sampleRate: number, channels: number, bitsPerSample: number)").ThrowAsJavaScriptException();
    return env.Undefined();
  }
  const std::string endpointId = info[0].As<Napi::String>().Utf8Value();
  const int sampleRate = info[1].As<Napi::Number>().Int32Value();
  const int channels = info[2].As<Napi::Number>().Int32Value();
  const int bitsPerSample = info[3].As<Napi::Number>().Int32Value();
  bool supported = false; std::string error;
  if (!wasapiexclusive::ProbeExclusiveFormat(endpointId, sampleRate, channels, bitsPerSample, supported, error)) {
    Napi::Error::New(env, error).ThrowAsJavaScriptException();
    return env.Undefined();
  }
  return Napi::Boolean::New(env, supported);
#endif
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  exports.Set("openExclusive", Napi::Function::New(env, OpenExclusive));
  exports.Set("listEndpoints", Napi::Function::New(env, ListEndpoints));
  exports.Set("probeExclusiveFormat", Napi::Function::New(env, ProbeExclusiveFormat));
  exports.Set("wired", Napi::Boolean::New(env, false));   // false until a real integration (see README) flips this
  return exports;
}

}  // namespace

NODE_API_MODULE(wasapi_exclusive, Init)
