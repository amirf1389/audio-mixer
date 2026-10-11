// ALSA backend (Linux): duplex through two PCM handles on one thread (blocking read, process, blocking write). Float samples, interleaved by the backend.
#ifdef AM_HAVE_ALSA
#include <alsa/asoundlib.h>

#include <atomic>
#include <cstring>
#include <thread>

#include "backends.hpp"

namespace am {
namespace {
class AlsaBackend : public Backend {
 public:
  ~AlsaBackend() override { stop(); closeAll(); }
  const char* name() const override { return "alsa"; }
  bool open(const DeviceConfig& cfg, AudioCallback cb, std::string& err) override {
    cfg_ = cfg; cb_ = std::move(cb); const bool wantIn = cfg.inChannels > 0 && cfg.inDevice != "-", wantOut = cfg.outChannels > 0 && cfg.outDevice != "-";
    if (!wantIn && !wantOut) { err = "no input and no output device"; return false; }
    if (wantOut && !setup(&play_, cfg.outDevice.empty() ? "default" : cfg.outDevice, SND_PCM_STREAM_PLAYBACK, cfg.outChannels, err)) return false;
    if (wantIn && !setup(&cap_, cfg.inDevice.empty() ? "default" : cfg.inDevice, SND_PCM_STREAM_CAPTURE, cfg.inChannels, err)) { closeAll(); return false; }
    inter_in_.assign(static_cast<std::size_t>(cfg.frames) * static_cast<std::size_t>(std::max(1, cfg.inChannels)), 0.0f); inter_out_.assign(static_cast<std::size_t>(cfg.frames) * static_cast<std::size_t>(std::max(1, cfg.outChannels)), 0.0f);
    in_.assign(static_cast<std::size_t>(cfg.inChannels), std::vector<float>(cfg.frames, 0.0f)); out_.assign(static_cast<std::size_t>(cfg.outChannels), std::vector<float>(cfg.frames, 0.0f));
    for (auto& v : in_) inp_.push_back(v.data());
    for (auto& v : out_) outp_.push_back(v.data());
    return true;
  }
  bool start(std::string&) override { stop_ = false; thread_ = std::thread([this] { loop(); }); return true; }
  void stop() override { stop_ = true; if (thread_.joinable()) thread_.join(); }
  std::uint64_t xruns() const override { return xruns_; }
 private:
  bool setup(snd_pcm_t** h, const std::string& dev, snd_pcm_stream_t dir, int ch, std::string& err) {
    int rc = snd_pcm_open(h, dev.c_str(), dir, 0);
    if (rc < 0) { err = "cannot open " + dev + ": " + snd_strerror(rc); *h = nullptr; return false; }
    snd_pcm_hw_params_t* hw; snd_pcm_hw_params_alloca(&hw); snd_pcm_hw_params_any(*h, hw);
    unsigned rate = static_cast<unsigned>(cfg_.sampleRate); snd_pcm_uframes_t period = cfg_.frames;
    if ((rc = snd_pcm_hw_params_set_access(*h, hw, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0 || (rc = snd_pcm_hw_params_set_format(*h, hw, SND_PCM_FORMAT_FLOAT_LE)) < 0 ||
        (rc = snd_pcm_hw_params_set_channels(*h, hw, static_cast<unsigned>(ch))) < 0 || (rc = snd_pcm_hw_params_set_rate_near(*h, hw, &rate, nullptr)) < 0 ||
        (rc = snd_pcm_hw_params_set_period_size_near(*h, hw, &period, nullptr)) < 0 || (rc = snd_pcm_hw_params_set_periods(*h, hw, 3, 0)) < 0 || (rc = snd_pcm_hw_params(*h, hw)) < 0) {
      err = dev + ": " + snd_strerror(rc); snd_pcm_close(*h); *h = nullptr; return false;
    }
    if (rate != static_cast<unsigned>(cfg_.sampleRate)) { err = dev + " does not run at " + std::to_string(static_cast<unsigned>(cfg_.sampleRate)) + " Hz (it offers " + std::to_string(rate) + ")"; snd_pcm_close(*h); *h = nullptr; return false; }
    return true;
  }
  void closeAll() { if (cap_) { snd_pcm_close(cap_); cap_ = nullptr; } if (play_) { snd_pcm_close(play_); play_ = nullptr; } }
  bool recover(snd_pcm_t* h, long rc) { if (rc == -EPIPE || rc == -ESTRPIPE) { ++xruns_; return snd_pcm_recover(h, static_cast<int>(rc), 1) >= 0; } return false; }
  void loop() {
    const std::size_t F = cfg_.frames; const int ci = cfg_.inChannels, co = cfg_.outChannels;
    if (cap_) snd_pcm_prepare(cap_);
    if (play_) { snd_pcm_prepare(play_); std::fill(inter_out_.begin(), inter_out_.end(), 0.0f); for (int i = 0; i < 2; ++i) snd_pcm_writei(play_, inter_out_.data(), F); }   // prime the device
    while (!stop_) {
      if (cap_) {
        const long rc = snd_pcm_readi(cap_, inter_in_.data(), F);
        if (rc < 0) { if (!recover(cap_, rc)) break; std::fill(inter_in_.begin(), inter_in_.end(), 0.0f); }
        for (int c = 0; c < ci; ++c) for (std::size_t i = 0; i < F; ++i) in_[static_cast<std::size_t>(c)][i] = inter_in_[i * static_cast<std::size_t>(ci) + static_cast<std::size_t>(c)];
      } else for (auto& v : in_) std::fill(v.begin(), v.end(), 0.0f);
      cb_(inp_.data(), outp_.data(), F);
      if (play_) {
        for (int c = 0; c < co; ++c) for (std::size_t i = 0; i < F; ++i) inter_out_[i * static_cast<std::size_t>(co) + static_cast<std::size_t>(c)] = out_[static_cast<std::size_t>(c)][i];
        const long rc = snd_pcm_writei(play_, inter_out_.data(), F);
        if (rc < 0 && !recover(play_, rc)) break;
      }
    }
  }
  DeviceConfig cfg_; AudioCallback cb_; snd_pcm_t *cap_ = nullptr, *play_ = nullptr;
  std::vector<float> inter_in_, inter_out_; std::vector<std::vector<float>> in_, out_; std::vector<const float*> inp_; std::vector<float*> outp_;
  std::thread thread_; std::atomic<bool> stop_{false}; std::atomic<std::uint64_t> xruns_{0};
};
}  // namespace
std::unique_ptr<Backend> makeAlsaBackend() { return std::make_unique<AlsaBackend>(); }
}  // namespace am
#endif
