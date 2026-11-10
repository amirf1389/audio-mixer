// A VST2 host written clean-room against the public VST 2.4 binary interface (the AEffect structure, dispatcher opcodes and host callback
// that every open-source host implements). No Steinberg VST2 SDK file is used or shipped: Steinberg no longer licenses that SDK, and
// "VST" is their trademark; A mix OS merely loads plug-ins the user already owns. Modules: Windows .dll, Linux .so (macOS .vst bundles
// not yet). Mono / stereo processing with processReplacing, parameters 0..1 with the plug-in's own display strings, bypass via the
// plug-in (effSetBypass when it canDo "bypass") or the host, state as the plug-in's chunk (or its parameters when it has no chunks).
#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "plugin/plugin.hpp"

namespace am::vst2 {

std::vector<std::string> defaultDirs();                                   // the platform's usual VST2 folders that exist
std::vector<std::string> scan(const std::vector<std::string>& dirs);      // candidate modules (.dll / .so) under them, one level
bool inspect(const std::string& modulePath, host::Info& info, std::string& err);   // loads, asks name / vendor / channels, unloads

class Plugin final : public host::Plugin {
 public:
  static std::unique_ptr<Plugin> load(const std::string& modulePath, double rate, int maxBlock, std::string& err);
  ~Plugin() override;
  const host::Info& info() const override { return info_; }
  const std::string& modulePath() const override { return path_; }
  int inputChannels() const override { return inCh_; }
  int outputChannels() const override { return outCh_; }
  std::uint32_t latencySamples() const override { return latency_; }
  std::vector<host::ParamInfo> params() const override;
  double param(std::uint32_t id) const override;
  void setParam(std::uint32_t id, double normalized) override;
  std::string paramText(std::uint32_t id, double normalized) const override;
  bool hasBypass() const override { return pluginBypass_; }
  void setBypass(bool on) override;
  bool bypass() const override { return bypassOn_.load(); }
  void process(const float* const* in, float* const* out, int frames) override;
  std::string state() const override;
  bool setState(const std::string& base64) override;
  struct Impl;
 private:
  Plugin() = default;
  std::unique_ptr<Impl> impl_; host::Info info_; std::string path_; int inCh_ = 0, outCh_ = 0; std::uint32_t latency_ = 0; bool pluginBypass_ = false; std::atomic<bool> bypassOn_{false};
};

}  // namespace am::vst2
