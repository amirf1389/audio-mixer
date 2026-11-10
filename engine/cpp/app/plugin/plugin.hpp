// One face for every plug-in format the console hosts (VST3 in app/vst3, VST2 in app/vst2): what the PLUGIN RACK needs and nothing format-
// specific. Implementations are created by their own loaders; the console only holds host::Plugin.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "audiomixer/runtime.hpp"

namespace am::host {

struct ParamInfo { std::uint32_t id = 0; std::string title, units; double normalized = 0, defaultNormalized = 0; int stepCount = 0; bool automatable = true, readOnly = false, bypass = false, hidden = false; };
struct Info { std::string format;   // "VST3" | "VST2"
              std::string name, vendor, version, category, subCategories, uid; };

class Plugin {
 public:
  virtual ~Plugin() = default;
  virtual const Info& info() const = 0;
  virtual const std::string& modulePath() const = 0;
  virtual int inputChannels() const = 0;          // of the main input (0 for a generator)
  virtual int outputChannels() const = 0;         // 1 or 2
  virtual std::uint32_t latencySamples() const = 0;
  virtual std::vector<ParamInfo> params() const = 0;                 // UI thread
  virtual double param(std::uint32_t id) const = 0;
  virtual void setParam(std::uint32_t id, double normalized) = 0;   // UI thread; reaches the audio thread lock-free
  virtual std::string paramText(std::uint32_t id, double normalized) const = 0;
  virtual bool hasBypass() const = 0;
  virtual void setBypass(bool on) = 0;
  virtual bool bypass() const = 0;
  virtual void process(const float* const* in, float* const* out, int frames) = 0;   // audio thread
  virtual std::string state() const = 0;           // opaque, base64
  virtual bool setState(const std::string& base64) = 0;
};

// A plug-in as a Runtime::Insert on a mono strip: a mono plug-in gets the strip, a stereo one gets it on both sides and gives back the left.
class MonoInsert : public Runtime::Insert {
 public:
  MonoInsert(std::shared_ptr<Plugin> p, int maxBlock) : p_(std::move(p)), ol_(static_cast<std::size_t>(maxBlock)), or_(static_cast<std::size_t>(maxBlock)) {}
  void process(float* mono, std::size_t frames) override {
    std::size_t done = 0;
    while (done < frames) {   // blocks larger than the plug-in's maximum are split
      const std::size_t n = std::min(frames - done, ol_.size()); const float* in[2] = {mono + done, mono + done}; float* out[2] = {ol_.data(), or_.data()};
      p_->process(p_->inputChannels() > 0 ? in : nullptr, out, static_cast<int>(n));
      for (std::size_t i = 0; i < n; ++i) mono[done + i] = ol_[i];
      done += n;
    }
  }
  const std::shared_ptr<Plugin>& plugin() const { return p_; }
 private:
  std::shared_ptr<Plugin> p_; std::vector<float> ol_, or_;
};

}  // namespace am::host
