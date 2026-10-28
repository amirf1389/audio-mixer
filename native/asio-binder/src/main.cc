// EXPERIMENTAL, UNWIRED native addon entry point: see native/asio-binder/README.md before building or calling
// this. Node-API via node-addon-api (not NAN: NAN is deprecated and ABI-unstable; N-API addons are ABI-stable
// across Node versions, so the built .node file does not need to be rebuilt for every Node release).
#include <napi.h>

#include "asiosdk.h"

namespace {

// asio.listDrivers(): harmless (read-only registry listing), usable today to confirm the addon loads and can
// see what bridge/detect.js also sees. One registry view only; see asiosdk.h.
Napi::Value ListDrivers(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
#ifndef _WIN32
  Napi::Error::New(env, "asio_binder is Windows only").ThrowAsJavaScriptException();
  return env.Undefined();
#else
  const auto names = asiobinder::ListRegisteredDrivers();
  Napi::Array out = Napi::Array::New(env, names.size());
  for (size_t i = 0; i < names.size(); ++i) out[i] = Napi::String::New(env, names[i]);
  return out;
#endif
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  exports.Set("listDrivers", Napi::Function::New(env, ListDrivers));
  return exports;
}

}  // namespace

NODE_API_MODULE(asio_binder, Init)
