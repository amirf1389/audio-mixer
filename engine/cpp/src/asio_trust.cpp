// SECURE ASIO LOADING: the policy part (asio_trust.hpp), plain C++ on every system. The Windows glue lives in backend_asio.cpp.
#include "audiomixer/asio_trust.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <mutex>
#include <set>
#include <map>

namespace am {
namespace asiotrust {
namespace {
std::string lower(std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
bool isHex(char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }
std::mutex& mu() { static std::mutex m; return m; }
std::map<std::string, std::string>& faults() { static std::map<std::string, std::string> f; return f; }
std::set<std::string>& allowed() { static std::set<std::string> a; return a; }
const char* const kSafeVars[] = {"systemroot", "windir", "programfiles", "programfiles(x86)", "programw6432", "commonprogramfiles", "commonprogramfiles(x86)", "commonprogramw6432", "systemdrive"};
}  // namespace

const char* sigName(Sig s) { switch (s) { case Sig::Valid: return "signed"; case Sig::Unsigned: return "unsigned"; case Sig::Invalid: return "invalid signature"; default: return "unknown"; } }

bool validClsid(const std::string& s) {
  static const int groups[] = {8, 4, 4, 4, 12};
  if (s.size() != 38 || s.front() != '{' || s.back() != '}') return false;
  std::size_t p = 1;
  for (int g = 0; g < 5; ++g) { for (int i = 0; i < groups[g]; ++i, ++p) if (!isHex(s[p])) return false; if (g < 4) { if (s[p] != '-') return false; ++p; } }
  return p == 37;
}

bool expandSafe(const std::string& raw, const std::function<bool(const std::string&, std::string&)>& getVar, std::string& out) {
  out.clear();
  for (std::size_t i = 0; i < raw.size();) {
    if (raw[i] != '%') { out += raw[i++]; continue; }
    const std::size_t end = raw.find('%', i + 1); if (end == std::string::npos) return false;
    const std::string name = lower(raw.substr(i + 1, end - i - 1));
    if (std::find(std::begin(kSafeVars), std::end(kSafeVars), name) == std::end(kSafeVars)) return false;
    std::string v; if (!getVar(name, v) || v.empty() || v.find('%') != std::string::npos) return false;
    out += v; i = end + 1;
  }
  return out.size() < 1024;
}

bool normalizePath(const std::string& raw, std::string& out, std::string& reason) {
  std::string p = raw;
  while (!p.empty() && (p.front() == ' ' || p.front() == '"')) p.erase(p.begin());
  while (!p.empty() && (p.back() == ' ' || p.back() == '"')) p.pop_back();
  for (char& c : p) if (c == '/') c = '\\';
  if (p.empty()) { reason = "no DLL path"; return false; }
  for (unsigned char c : p) if (c < 0x20) { reason = "control characters in the path"; return false; }
  if (p.size() >= 2 && p[0] == '\\' && p[1] == '\\') { reason = "a network (UNC) or device path"; return false; }
  if (p.size() < 3 || !std::isalpha(static_cast<unsigned char>(p[0])) || p[1] != ':' || p[2] != '\\') { reason = "a relative path"; return false; }
  if (p.find(':', 2) != std::string::npos) { reason = "a stream or device name in the path"; return false; }
  // whole components: no "." / ".." / empty parts
  std::string clean = p.substr(0, 2);
  std::size_t i = 3;
  while (i <= p.size()) {
    const std::size_t j = std::min(p.find('\\', i), p.size()); const std::string part = p.substr(i, j - i);
    if (part == "..") { reason = "\"..\" in the path"; return false; }
    if (!part.empty() && part != ".") {
      if (part.find_first_of("<>|?*") != std::string::npos) { reason = "wildcards in the path"; return false; }
      clean += '\\'; clean += part;
    }
    i = j + 1;
  }
  out = clean; return true;
}

std::vector<std::string> trustedRoots(const std::function<bool(const std::string&, std::string&)>& getVar) {
  std::vector<std::string> roots; std::string r, why, n;
  auto add = [&](const std::string& raw) { std::string e; if (expandSafe(raw, getVar, e) && normalizePath(e, n, why)) { if (std::find(roots.begin(), roots.end(), lower(n)) == roots.end()) roots.push_back(lower(n)); } };
  add("%SystemRoot%\\System32"); add("%SystemRoot%\\SysWOW64");
  add("%ProgramFiles%"); add("%ProgramFiles(x86)%"); add("%ProgramW6432%");
  return roots;
}

bool insideTrusted(const std::string& path, const std::vector<std::string>& roots) {
  const std::string p = lower(path);
  for (const std::string& r : roots) if (p.size() > r.size() + 1 && p.compare(0, r.size(), r) == 0 && p[r.size()] == '\\') return true;
  return false;
}

std::string sanitizeText(const char* data, std::size_t len, std::size_t maxBytes, const char* fallback) {
  std::string out;
  if (data) {
    std::size_t n = 0; while (n < len && data[n]) ++n;   // a fixed char[] from a driver may lack its terminating zero
    const auto* s = reinterpret_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < n;) {
      const unsigned char c = s[i]; int extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
      bool ok = extra >= 0 && i + static_cast<std::size_t>(extra) < n;
      for (int k = 1; ok && k <= extra; ++k) ok = (s[i + static_cast<std::size_t>(k)] & 0xC0) == 0x80;
      if (ok && extra > 0) {   // no overlong forms, no surrogates, nothing above U+10FFFF
        const unsigned cp = extra == 1 ? ((c & 0x1Fu) << 6 | (s[i + 1] & 0x3Fu)) : extra == 2 ? ((c & 0x0Fu) << 12 | (s[i + 1] & 0x3Fu) << 6 | (s[i + 2] & 0x3Fu)) : ((c & 0x07u) << 18 | (s[i + 1] & 0x3Fu) << 12 | (s[i + 2] & 0x3Fu) << 6 | (s[i + 3] & 0x3Fu));
        if ((extra == 1 && cp < 0x80) || (extra == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) || (extra == 3 && (cp < 0x10000 || cp > 0x10FFFF))) ok = false; }
      std::string ch;
      if (!ok) { ch = "?"; extra = 0; }
      else if (extra == 0) { if (c < 0x20 || c == 0x7F) ch = " "; else ch = static_cast<char>(c); }
      else ch.assign(data + i, static_cast<std::size_t>(extra) + 1);
      if (out.size() + ch.size() > maxBytes) break;
      out += ch; i += static_cast<std::size_t>(extra) + 1;
    }
  }
  while (!out.empty() && out.front() == ' ') out.erase(out.begin());
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out.empty() ? std::string(fallback ? fallback : "") : out;
}

long clampChannels(long n) { return n < 0 ? 0 : n > kMaxChannels ? kMaxChannels : n; }
bool validateBuffers(BufferSizes& b) {
  if (b.mn < 1 || b.mx < 1 || b.mn > kMaxBuffer) return false;
  if (b.mx > kMaxBuffer) b.mx = kMaxBuffer;
  if (b.mn > b.mx) return false;
  if (b.pref < b.mn || b.pref > b.mx) b.pref = b.mn;
  if (b.gran < -1 || b.gran > b.mx) b.gran = 0;
  return true;
}
bool validRate(double r) { return std::isfinite(r) && r >= 8000.0 && r <= 1100000.0; }
long clampLatency(long f) { return f < 0 ? 0 : f > (1L << 20) ? (1L << 20) : f; }

std::vector<Driver> enumerate(Env& env, std::vector<Rejected>* rejected) {
  std::vector<Driver> out;
  auto getVar = [&env](const std::string& n, std::string& v) { return env.getVar(n, v); };
  const std::vector<std::string> roots = trustedRoots(getVar);
  auto reject = [&](const std::string& name, const std::string& why) { if (rejected) rejected->push_back({sanitizeText(name, kMaxDriverName, "?"), why}); };
  for (int bits : {64, 32}) {
    for (const RegEntry& e : env.entries(bits)) {
      const std::string name = sanitizeText(e.name, kMaxDriverName);
      if (name.empty()) { reject(e.name, "no name"); continue; }
      if (!validClsid(e.clsid)) { reject(name, "no valid CLSID"); continue; }
      bool dup = false; for (const Driver& d : out) if (lower(d.clsid) == lower(e.clsid)) dup = true;   // both views: listed once, 64-bit first
      if (dup) continue;
      std::string expanded = e.inproc, why;
      if (e.expandSz || e.inproc.find('%') != std::string::npos) { if (!expandSafe(e.inproc, getVar, expanded)) { reject(name, "an unsafe variable in the DLL path"); continue; } }
      std::string path; if (!normalizePath(expanded, path, why)) { reject(name, why); continue; }
      if (lower(path).size() < 5 || lower(path).compare(lower(path).size() - 4, 4, ".dll") != 0) { reject(name, "not a .dll"); continue; }
      if (!insideTrusted(path, roots)) { reject(name, "the DLL is outside System32 / Program Files (a user-writable folder)"); continue; }
      if (!env.fileExists(path)) { reject(name, "the DLL does not exist"); continue; }
      Driver d; d.name = name; d.clsid = e.clsid; d.path = path; d.bits = bits; d.sig = env.signature(path);
      out.push_back(d);
    }
  }
  return out;
}

const Driver* select(const std::vector<Driver>& list, const std::string& want, std::string* why) {
  auto no = [why](const char* w) -> const Driver* { if (why) *why = w; return nullptr; };
  if (want.empty()) return list.empty() ? no("no ASIO driver is installed") : &list.front();
  if (want.size() > 128) return no("the name is too long");
  if (want.find_first_of("\\/:") != std::string::npos || lower(want).find(".dll") != std::string::npos) return no("ASIO drivers are chosen by their registered name, never by a path");
  const std::string w = lower(want);
  for (const Driver& d : list) if (lower(d.name) == w) return &d;
  for (const Driver& d : list) if (lower(d.clsid) == w) return &d;
  for (const Driver& d : list) if (lower(d.name).find(w) != std::string::npos) return &d;
  return no("no registered ASIO driver has that name");
}

void markFaulted(const std::string& clsid, const std::string& what) { std::lock_guard<std::mutex> g(mu()); faults()[lower(clsid)] = what; }
bool isFaulted(const std::string& clsid) { std::lock_guard<std::mutex> g(mu()); return faults().count(lower(clsid)) > 0; }
std::string faultText(const std::string& clsid) { std::lock_guard<std::mutex> g(mu()); auto it = faults().find(lower(clsid)); return it == faults().end() ? std::string() : it->second; }
void clearFaulted() { std::lock_guard<std::mutex> g(mu()); faults().clear(); }
void allowUnsigned(const std::string& clsid, const std::string& path, bool allow) {
  std::lock_guard<std::mutex> g(mu()); const std::string k = lower(clsid) + "|" + lower(path); if (allow) allowed().insert(k); else allowed().erase(k); }
bool unsignedAllowed(const std::string& clsid, const std::string& path) { std::lock_guard<std::mutex> g(mu()); return allowed().count(lower(clsid) + "|" + lower(path)) > 0; }
bool loadAllowed(const Driver& d, std::string* why) {
  if (isFaulted(d.clsid)) { if (why) *why = "faulted earlier in this run (" + faultText(d.clsid) + "): not loaded again until the program restarts"; return false; }
  if (d.sig != Sig::Valid && !unsignedAllowed(d.clsid, d.path)) { if (why) *why = std::string("unsigned driver (") + sigName(d.sig) + "): confirm it once before it is used"; return false; }
  return true;
}

}  // namespace asiotrust
}  // namespace am
