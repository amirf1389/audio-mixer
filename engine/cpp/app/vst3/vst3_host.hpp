// A small VST3 host: loads a .vst3 module (Linux bundle, Windows bundle or single DLL), lists its audio-effect classes, creates one
// (component + controller), arranges the main buses mono or stereo, runs it at the engine's rate and block size, carries parameter changes
// from the UI thread to the audio thread lock-free, saves / restores the plug-in state, and adapts a plug-in to the Runtime's mono insert.
// Talks to plug-ins only through the Steinberg VST3 interface headers (drivers/vst3/sdk, dual licence); every host-side object (host
// application, component handler, parameter queues, memory stream) is implemented here, so nothing of public.sdk is compiled in.
// No plug-in editor (GUI) is opened: parameters are shown as generic controls by the console. macOS bundles are not loaded yet.
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "plugin/plugin.hpp"

namespace am::vst3 {

struct ClassInfo { std::string cid; std::string name, vendor, version, category, subCategories; };   // cid: 32 hex characters
std::vector<std::string> defaultDirs();                                                  // the platform's VST3 folders that exist
std::vector<std::string> scan(const std::vector<std::string>& dirs);                     // every .vst3 bundle / file under them (one level)
bool inspect(const std::string& modulePath, std::vector<ClassInfo>& classes, std::string& err);   // loads the module, lists Audio Module classes, unloads
std::string binaryOf(const std::string& modulePath);                                     // the shared library inside a bundle ("" when none fits this platform)

using ParamInfo = host::ParamInfo;

class Plugin final : public host::Plugin {
 public:
  // cid: one of inspect()'s, or "" for the first Audio Module class. rate / maxBlock: the engine's. Fails with a reason in err.
  static std::unique_ptr<Plugin> load(const std::string& modulePath, const std::string& cid, double rate, int maxBlock, std::string& err);
  ~Plugin() override;
  const host::Info& info() const override { return hinfo_; }
  const ClassInfo& classInfo() const { return info_; }
  const std::string& modulePath() const override { return path_; }
  int inputChannels() const override { return inCh_; }      // of the main input bus after arrangement (0 for a generator)
  int outputChannels() const override { return outCh_; }
  std::uint32_t latencySamples() const override { return latency_; }
  // parameters (UI thread): the controller's list; set queues the change for the next audio block and updates the controller
  std::vector<ParamInfo> params() const override;
  double param(std::uint32_t id) const override;
  void setParam(std::uint32_t id, double normalized) override;
  std::string paramText(std::uint32_t id, double normalized) const override;   // the plug-in's own display string ("-6.0 dB")
  bool hasBypass() const override { return bypassId_ != 0xffffffffu; }
  void setBypass(bool on) override;
  bool bypass() const override { return bypassOn_; }
  // audio thread: in / out have inputChannels() / outputChannels() pointers of `frames` samples (in may be nullptr when no input bus)
  void process(const float* const* in, float* const* out, int frames) override;
  // state (component + controller), base64, for saving with the console
  std::string state() const override;
  bool setState(const std::string& base64) override;
  bool hasEditor() const override; bool openEditor(void* parentWindow, int& width, int& height) override; void closeEditor() override; bool editorOpen() const override;

  struct Impl;
 private:
  Plugin() = default;
  std::unique_ptr<Impl> impl_; ClassInfo info_; host::Info hinfo_; std::string path_; int inCh_ = 0, outCh_ = 0; std::uint32_t latency_ = 0, bypassId_ = 0xffffffffu; std::atomic<bool> bypassOn_{false};
};

using MonoInsert = host::MonoInsert;

}  // namespace am::vst3
