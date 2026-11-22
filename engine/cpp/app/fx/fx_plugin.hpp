// A built-in effect (am::fx) as a rack plug-in: the same face as a hosted VST (host::Plugin), so any of the ten effects sits in any insert
// slot of any channel, with its knobs on the generic PARAMS panel, bypass, and its state saved with the console. Parameter ids: the
// effect's knobs in order, then MIX (dry / wet; an insert starts at 35 % for the time-based effects and 100 % for the others).
#pragma once
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "fx/fx.hpp"
#include "plugin/plugin.hpp"

namespace am::fx {
class FxPlugin final : public host::Plugin {
 public:
  static std::unique_ptr<host::Plugin> make(const std::string& kind, double fs) { auto e = fx::make(kind, fs); if (!e) return nullptr; return std::unique_ptr<host::Plugin>(new FxPlugin(std::move(e), kind)); }
  const host::Info& info() const override { return info_; }
  const std::string& modulePath() const override { return path_; }
  int inputChannels() const override { return 2; } int outputChannels() const override { return 2; } std::uint32_t latencySamples() const override { return 0; }
  std::vector<host::ParamInfo> params() const override { std::vector<host::ParamInfo> v; std::uint32_t id = 0; for (const Param& p : e_->params()) { host::ParamInfo q; q.id = id++; q.title = p.title; q.units = p.units; q.normalized = p.value; q.defaultNormalized = p.def; v.push_back(q); }
    host::ParamInfo m; m.id = id; m.title = "MIX"; m.normalized = e_->mix(); m.defaultNormalized = defMix_; v.push_back(m); return v; }
  double param(std::uint32_t id) const override { const auto ps = e_->params(); return id < ps.size() ? ps[id].value : e_->mix(); }
  void setParam(std::uint32_t id, double n) override { const auto ps = e_->params(); if (id < ps.size()) e_->set(ps[id].id, n); else e_->setMix(n); }
  std::string paramText(std::uint32_t id, double n) const override { const auto ps = e_->params(); if (id < ps.size()) return e_->text(ps[id].id, n); char b[24]; std::snprintf(b, sizeof b, "%.0f %%", n * 100); return b; }
  bool hasBypass() const override { return true; } void setBypass(bool on) override { e_->setBypass(on); } bool bypass() const override { return e_->bypass(); }
  void process(const float* const* in, float* const* out, int frames) override { for (int i = 0; i < frames; ++i) { out[0][i] = in ? in[0][i] : 0.f; out[1][i] = in ? in[1][i] : 0.f; } e_->process(out[0], out[1], static_cast<std::size_t>(frames)); }
  std::string state() const override { std::string s; for (const Param& p : e_->params()) { char b[48]; std::snprintf(b, sizeof b, "%s=%.4f;", p.id.c_str(), p.value); s += b; } char b[32]; std::snprintf(b, sizeof b, "mix=%.4f;", e_->mix()); s += b; if (!ui_.empty()) s += "ui=" + ui_ + ";"; return s; }
  bool setState(const std::string& st) override { std::size_t pos = 0; bool any = false; while (pos < st.size()) { const std::size_t eq = st.find('=', pos), end = st.find(';', pos); if (eq == std::string::npos || end == std::string::npos || eq > end) break; const std::string k = st.substr(pos, eq - pos); const double v = std::atof(st.substr(eq + 1, end - eq - 1).c_str()); if (k == "ui") ui_ = st.substr(eq + 1, end - eq - 1); else if (k == "mix") e_->setMix(v); else e_->set(k, v); any = true; pos = end + 1; } return any; }
 public:
  Effect* effect() const { return e_.get(); }
  // what the editor keeps with the effect but the sound does not use (the W-SHAPE's A / B compare slots): saved in the state as ui=...;
  // (no '=' or ';' inside). An old state without it loads as before.
  const std::string& uiState() const { return ui_; }
  void setUiState(const std::string& u) { ui_.clear(); for (char ch : u) if (ch != '=' && ch != ';') ui_ += ch; if (ui_.size() > 4000) ui_.resize(4000); }   // for the effects that do more than knobs (the SAMPLE PLAYER's own file)
 private:
  FxPlugin(std::unique_ptr<Effect> e, const std::string& kind) : e_(std::move(e)), path_("fx:" + kind) { info_.format = "FX"; info_.name = e_->name(); info_.vendor = "AMFB Audio"; info_.category = "Fx"; info_.uid = kind;
    defMix_ = kind == "reverb" || kind == "hall" || kind == "room" || kind == "delay" || kind == "chorus" || kind == "flanger" || kind == "phaser" || kind == "wah" ? 0.35 : 1.0; if (kind == "flanger" || kind == "phaser") defMix_ = 0.7; if (kind == "wah") defMix_ = 0.8; e_->setMix(defMix_); }
  std::unique_ptr<Effect> e_; host::Info info_; std::string path_, ui_; double defMix_ = 1;
};
}  // namespace am::fx
