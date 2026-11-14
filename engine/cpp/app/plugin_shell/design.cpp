#include "plugin_shell/design.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>

#include "daemon/json.hpp"
#include "fx/fx.hpp"

namespace fs = std::filesystem;
namespace am::shell {

static bool knownKind(const std::string& k) { for (const auto& [kind, name] : am::fx::catalog()) if (kind == k) return true; return false; }
static bool hasKnob(const std::string& kind, const std::string& id) {
  auto e = am::fx::make(kind, 48000); if (!e) return false;
  for (const auto& p : e->params()) if (p.id == id) return true; return false;
}

bool parse(const std::string& text, Design& d, std::string& err) {
  bool ok = false; const json::Value v = json::Value::parse(text, ok);
  if (!ok || !v.isObject()) { err = "not a design (the file is not JSON)"; return false; }
  d = Design{}; d.name = v["name"].asString(); d.vendor = v["vendor"].asString("AMFB Audio"); d.uid = v["uid"].asString();
  d.category = v["category"].asString("Fx"); d.version = v["version"].asString("1.0.0");
  if (v.has("panel")) d.panelJson = v["panel"].dump();
  if (d.name.empty() || d.name.size() > 60) { err = "the plug-in needs a name (up to 60 characters)"; return false; }
  if (d.uid.size() != 32 || d.uid.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) { err = "uid must be 32 hex digits"; return false; }
  for (const json::Value& b : v["blocks"].items()) {
    Block bl; bl.kind = b["kind"].asString();
    if (!knownKind(bl.kind)) { err = "unknown effect block: " + bl.kind; return false; }
    for (const auto& [k, x] : b["set"].members()) { if (!hasKnob(bl.kind, k)) { err = bl.kind + " has no knob " + k; return false; } bl.set.emplace_back(k, std::clamp(x.asNumber(0.5), 0.0, 1.0)); }
    d.blocks.push_back(bl);
  }
  if (d.blocks.empty() || static_cast<int>(d.blocks.size()) > kMaxBlocks) { err = "a plug-in has 1 to 6 effect blocks"; return false; }
  for (const json::Value& p : v["params"].items()) {
    Param pa; pa.title = p["title"].asString(); pa.id = p["id"].asString(); pa.block = p["block"].asInt(-1); pa.def = std::clamp(p["def"].asNumber(0.5), 0.0, 1.0);
    if (pa.block < 0 || pa.block >= static_cast<int>(d.blocks.size())) { err = "knob \"" + pa.title + "\" points at no block"; return false; }
    if (!hasKnob(d.blocks[static_cast<std::size_t>(pa.block)].kind, pa.id)) { err = "knob \"" + pa.title + "\": the block has no " + pa.id; return false; }
    if (pa.title.empty()) pa.title = pa.id;
    d.params.push_back(pa);
  }
  if (static_cast<int>(d.params.size()) > kMaxParams) { err = "at most 16 knobs go to the DAW"; return false; }
  return true;
}

std::string toJson(const Design& d) {
  json::Value o = json::Value::object(); o.set("v", 1); o.set("name", d.name); o.set("vendor", d.vendor); o.set("uid", d.uid); o.set("category", d.category); o.set("version", d.version);
  json::Value bl = json::Value::array();
  for (const Block& b : d.blocks) { json::Value x = json::Value::object(); x.set("kind", b.kind); json::Value s = json::Value::object(); for (const auto& [k, v] : b.set) s.set(k, v); x.set("set", s); bl.push(x); }
  o.set("blocks", bl); json::Value ps = json::Value::array();
  for (const Param& p : d.params) { json::Value x = json::Value::object(); x.set("title", p.title); x.set("block", p.block); x.set("id", p.id); x.set("def", p.def); ps.push(x); }
  o.set("params", ps);
  if (!d.panelJson.empty()) { bool ok = false; const json::Value p = json::Value::parse(d.panelJson, ok); if (ok) o.set("panel", p); }
  return o.dump();
}

std::string newUid(const std::string& seed) {
  const auto t = std::chrono::high_resolution_clock::now().time_since_epoch().count();
  const std::size_t a = std::hash<std::string>{}(seed + "|amfb|" + std::to_string(t)), b = std::hash<std::string>{}(std::to_string(t) + "|" + seed + "|shell");
  char out[33]; std::snprintf(out, sizeof out, "%016llx%016llx", static_cast<unsigned long long>(a), static_cast<unsigned long long>(b)); return out;
}

std::string safeName(const std::string& name) {
  std::string s; for (char c : name) if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_') s += c;
  while (!s.empty() && s.back() == ' ') s.pop_back(); while (!s.empty() && s.front() == ' ') s.erase(s.begin());
  return s.empty() ? std::string("AMFB Plugin") : s;
}

std::string exportBundle(const std::string& shellBundle, const Design& d, const std::string& outDir, std::string& err) {
  std::error_code ec; const fs::path src(shellBundle);
  if (!fs::is_directory(src / "Contents")) { err = "the plug-in shell is not installed next to the console (" + shellBundle + ")"; return {}; }
  const std::string nm = safeName(d.name); const fs::path dst = fs::path(outDir) / (nm + ".vst3");
  fs::create_directories(outDir, ec); fs::remove_all(dst, ec);
  // every architecture folder of the shell, its binary renamed to the plug-in's name (hosts load Contents/<arch>/<Name>.<ext>)
  int bins = 0;
  for (const auto& arch : fs::directory_iterator(src / "Contents", ec)) {
    if (!arch.is_directory() || arch.path().filename() == "Resources") continue;
    for (const auto& f : fs::directory_iterator(arch.path(), ec)) {
      if (!f.is_regular_file()) continue; const std::string ext = f.path().extension().string();
      if (ext != ".vst3" && ext != ".so" && ext != ".dll") continue;
      fs::create_directories(dst / "Contents" / arch.path().filename(), ec);
      fs::copy_file(f.path(), dst / "Contents" / arch.path().filename() / (nm + ext), fs::copy_options::overwrite_existing, ec);
      if (ec) { err = "could not copy the shell: " + ec.message(); return {}; } ++bins;
    }
  }
  if (!bins) { err = "the plug-in shell bundle has no binary"; return {}; }
  fs::create_directories(dst / "Contents" / "Resources", ec);
  std::ofstream(dst / "Contents" / "Resources" / "design.json", std::ios::binary) << toJson(d);
  if (!fs::exists(dst / "Contents" / "Resources" / "design.json")) { err = "could not write the design into the bundle"; return {}; }
  return dst.string();
}

}  // namespace am::shell
