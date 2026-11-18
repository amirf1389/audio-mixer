#include "padbank.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "arrange/arrange.hpp"
#include "arrange/decode.hpp"
#include "samples.hpp"
#include "sfx.hpp"

namespace am::pads {
namespace fs = std::filesystem;

namespace {
bool number(const std::string& s, int lo, int hi, int& out) {
  if (s.empty() || s.size() > 5 || !std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; })) return false;
  out = std::atoi(s.c_str()); return out >= lo && out <= hi;
}
bool parse(const std::string& src, std::string& kind, int& id, std::string& file) {
  kind.clear(); id = 0; file.clear();
  if (src.empty()) { kind = "none"; return true; }
  if (src.rfind("sample:", 0) == 0) { kind = "sample"; return number(src.substr(7), 1, static_cast<int>(samples::list().size()), id); }
  if (src.rfind("sfx:", 0) == 0) { kind = "sfx"; return number(src.substr(4), 1, sfx::kCount, id); }
  if (src.rfind("file:", 0) == 0) { kind = "file"; file = src.substr(5); return !file.empty() && file.size() < 1024 && file.find('\0') == std::string::npos && file.find('\n') == std::string::npos; }
  return false;
}
// UTF-8 file names both ways (Windows paths with accents, Persian or Chinese letters)
fs::path pathOf(const std::string& u8) { return fs::path(std::u8string(u8.begin(), u8.end())); }
std::string utf8(const fs::path& p) { const std::u8string s = p.u8string(); return std::string(s.begin(), s.end()); }
long long mtimeOf(const fs::path& p) { std::error_code ec; const auto t = fs::last_write_time(p, ec); return ec ? 0 : static_cast<long long>(t.time_since_epoch().count()); }
}  // namespace

Pad defaultPad(int n) {
  Pad p; if (n < 1 || n > kPads) return p;
  p.source = n <= 100 ? "sample:" + std::to_string(n) : "sfx:" + std::to_string(n - 100); p.name = sourceName(p.source); p.color = n <= 100 ? 0 : 1 + ((n - 101) / sfx::kPerFamily) % (kColors - 1);
  return p;
}

bool validSource(const std::string& source, bool allowFiles) {
  std::string kind, file; int id = 0; if (!parse(source, kind, id, file)) return false;
  if (kind != "file") return true;
  if (!allowFiles) return false;
  std::error_code ec; return fs::is_regular_file(pathOf(file), ec) && arr::isAudioFile(file);
}

std::string sourceName(const std::string& source) {
  std::string kind, file; int id = 0; if (!parse(source, kind, id, file) || kind == "none") return {};
  if (kind == "sample") return samples::list()[static_cast<std::size_t>(id - 1)].name;
  if (kind == "sfx") return sfx::info(id).name;
  return utf8(pathOf(file).stem());
}

bool loadClip(const std::string& source, double rate, std::vector<float>& L, std::vector<float>& R, std::string& err) {
  L.clear(); R.clear(); std::string kind, file; int id = 0;
  if (rate <= 0 || !parse(source, kind, id, file)) { err = "not a pad source"; return false; }
  if (kind == "none") { err = "empty pad"; return false; }
  if (kind == "sample") return samples::render(id, rate, L, R);
  if (kind == "sfx") return sfx::render(id, rate, L, R);
  arr::Audio a; if (!arr::readAudio(file, a, err)) return false;
  const std::size_t keep = static_cast<std::size_t>(kMaxFileSeconds * a.rate); if (a.L.size() > keep) { a.L.resize(keep); a.R.resize(keep); }
  if (a.L.empty()) { err = "the file has no sound"; return false; }
  if (std::abs(a.rate - rate) > 0.5) { L = arr::resampleHq(a.L, a.rate, rate); R = arr::resampleHq(a.R, a.rate, rate); } else { L = std::move(a.L); R = std::move(a.R); }
  return true;
}

Bank::Bank(std::string path) : path_(std::move(path)) {
  pads_.resize(kPads); for (int n = 1; n <= kPads; ++n) pads_[static_cast<std::size_t>(n - 1)] = defaultPad(n);
  if (!path_.empty()) readFile();
}

Pad Bank::at(int n) const { std::lock_guard<std::mutex> l(m_); return n >= 1 && n <= kPads ? pads_[static_cast<std::size_t>(n - 1)] : Pad{}; }
int Bank::version() const { std::lock_guard<std::mutex> l(m_); return version_; }

bool Bank::set(int n, Pad p, bool allowFiles, std::string& err) {
  if (n < 1 || n > kPads) { err = "pad: 1..1000"; return false; }
  if (!validSource(p.source, allowFiles)) { err = p.source.rfind("file:", 0) == 0 ? (allowFiles ? "the file is not an audio file this program reads" : "files are chosen on the computer") : "source: sample:1..100, sfx:1..1000 or empty"; return false; }
  std::string name; for (char c : p.name) if (static_cast<unsigned char>(c) >= 0x20) name += c;   // no control characters
  if (name.size() > 48) name.resize(48);
  p.name = name.empty() ? sourceName(p.source) : name; p.gainDb = std::isfinite(p.gainDb) ? std::clamp(p.gainDb, -40.0, 12.0) : 0.0; p.color = std::clamp(p.color, 0, kColors - 1);
  std::lock_guard<std::mutex> l(m_); Pad& cur = pads_[static_cast<std::size_t>(n - 1)]; if (cur == p) return true;
  cur = std::move(p); ++version_; return true;
}
bool Bank::resetPad(int n) { if (n < 1 || n > kPads) return false; std::lock_guard<std::mutex> l(m_); pads_[static_cast<std::size_t>(n - 1)] = defaultPad(n); ++version_; return true; }
void Bank::resetAll() { std::lock_guard<std::mutex> l(m_); for (int n = 1; n <= kPads; ++n) pads_[static_cast<std::size_t>(n - 1)] = defaultPad(n); ++version_; }

json::Value Bank::toJson(int from, int to) const {
  from = std::max(1, from); to = std::min(kPads, to);
  std::lock_guard<std::mutex> l(m_); json::Value a = json::Value::array();
  for (int n = from; n <= to; ++n) { const Pad& p = pads_[static_cast<std::size_t>(n - 1)]; json::Value v = json::Value::object();
    v.set("n", n); v.set("source", p.source); v.set("name", p.name); v.set("gainDb", p.gainDb); v.set("color", p.color); v.set("custom", !(p == defaultPad(n))); a.push(v); }
  json::Value o = json::Value::object(); o.set("version", version_); o.set("count", kPads); o.set("from", from); o.set("to", to); o.set("pads", a); return o;
}

bool Bank::save(std::string& err) {
  if (path_.empty()) return true;
  json::Value a = json::Value::array(); int ver;
  { std::lock_guard<std::mutex> l(m_); ver = version_;
    for (int n = 1; n <= kPads; ++n) { const Pad& p = pads_[static_cast<std::size_t>(n - 1)]; if (p == defaultPad(n)) continue;
      json::Value v = json::Value::object(); v.set("n", n); v.set("source", p.source); v.set("name", p.name); v.set("gainDb", p.gainDb); v.set("color", p.color); a.push(v); } }
  json::Value o = json::Value::object(); o.set("format", "amf-x1-pads"); o.set("version", ver); o.set("pads", a);
  const fs::path dst = pathOf(path_), tmp = pathOf(path_ + ".tmp"); std::error_code ec; fs::create_directories(dst.parent_path(), ec);
  { std::ofstream f(tmp, std::ios::binary | std::ios::trunc); if (!f) { err = "cannot write " + path_; return false; } f << o.dump(); if (!f.good()) { err = "cannot write " + path_; return false; } }
  fs::rename(tmp, dst, ec); if (ec) { fs::remove(dst, ec); fs::rename(tmp, dst, ec); if (ec) { err = "cannot replace " + path_; return false; } }   // atomic where the system allows it: a reader never sees half a file
  std::lock_guard<std::mutex> l(m_); size_ = fs::file_size(dst, ec); mtime_ = mtimeOf(dst); return true;
}

bool Bank::readFile() {
  const fs::path p = pathOf(path_); std::error_code ec; if (!fs::exists(p, ec)) return false;
  const std::uintmax_t sz = fs::file_size(p, ec); if (ec || sz > (4u << 20)) return false;   // a pads file is a few hundred kB at most
  std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); bool ok = false; const json::Value o = json::Value::parse(ss.str(), ok);
  if (!ok || o["format"].asString() != "amf-x1-pads") return false;
  std::vector<Pad> next(kPads); for (int n = 1; n <= kPads; ++n) next[static_cast<std::size_t>(n - 1)] = defaultPad(n);
  for (const json::Value& v : o["pads"].items()) {   // the file is not trusted: every field checked as if a remote sent it (files allowed: only this computer writes it)
    const int n = v["n"].asInt(0); if (n < 1 || n > kPads) continue; std::string src = v["source"].asString();
    std::string kind, file; int id = 0; if (!parse(src, kind, id, file)) continue;   // a missing file stays assigned (an unplugged drive): it says so when played
    Pad pad; pad.source = src; pad.name = v["name"].asString(); if (pad.name.size() > 48) pad.name.resize(48); if (pad.name.empty()) pad.name = sourceName(src);
    const double g = v["gainDb"].asNumber(0); pad.gainDb = std::isfinite(g) ? std::clamp(g, -40.0, 12.0) : 0.0; pad.color = std::clamp(v["color"].asInt(0), 0, kColors - 1);
    next[static_cast<std::size_t>(n - 1)] = pad; }
  std::lock_guard<std::mutex> l(m_); const bool changed = next != pads_; pads_ = std::move(next);
  version_ = std::max(version_ + (changed ? 1 : 0), o["version"].asInt(0)); size_ = sz; mtime_ = mtimeOf(p); return changed;
}

bool Bank::reloadIfChanged() {
  if (path_.empty()) return false;
  const fs::path p = pathOf(path_); std::error_code ec; if (!fs::exists(p, ec)) return false;
  const std::uintmax_t sz = fs::file_size(p, ec); const long long mt = mtimeOf(p);
  { std::lock_guard<std::mutex> l(m_); if (sz == size_ && mt == mtime_) return false; }
  return readFile();
}

}  // namespace am::pads
