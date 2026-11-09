#include "license.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>

#include "crypto.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

namespace am::lic {

const Jwk kVendorJwk = {"qs3G8HQmgXrE0oAJn9Vj9jNg15YaPWuclESZ01899Fw", "sugiBjJ1wcFHZRerQRq4VMEFSWrGDc6ts9cnD0x-_i0"};   // bridge/license-public.json

namespace {
const Plan kPlans[] = {{"basic", "BASIC", 8, 0}, {"pro", "PRO", 16, 1}, {"studio", "STUDIO", 32, 2}, {"master", "MASTER", 32, 3}};
std::string programDir_;
std::string strip(const std::string& s) { std::string o; for (char c : s) if (c != ' ' && c != '\n' && c != '\r' && c != '\t') o += c; return o; }
std::string noDash(const std::string& s) { std::string o; for (char c : s) if (c != '-') o += c; return o; }
std::string dateOf(double ms) { const std::time_t t = static_cast<std::time_t>(ms / 1000); char b[16] = {0}; std::tm tmv{};
#ifdef _WIN32
  gmtime_s(&tmv, &t);
#else
  gmtime_r(&t, &tmv);
#endif
  std::strftime(b, sizeof b, "%Y-%m-%d", &tmv); return b; }
// splits "PREFIX.body.sig", checks the signature against any trusted key, decodes the payload
bool open(const std::string& text, const char* prefix, const std::vector<Jwk>& trusted, json::Value& payload, bool& sigOk) {
  const std::string s = strip(text); sigOk = false;
  const std::size_t a = s.find('.'), b = a == std::string::npos ? a : s.find('.', a + 1);
  if (a == std::string::npos || b == std::string::npos || s.find('.', b + 1) != std::string::npos || s.substr(0, a) != prefix) return false;
  const std::string body = s.substr(a + 1, b - a - 1), sig = s.substr(b + 1);
  std::vector<std::uint8_t> sigB, x, y; if (!crypto::base64urlDecode(sig, sigB)) return true;
  for (const Jwk& k : trusted) { if (crypto::base64urlDecode(k.x, x) && crypto::base64urlDecode(k.y, y) && crypto::ecdsaP256Verify(x, y, reinterpret_cast<const std::uint8_t*>(body.data()), body.size(), sigB)) { sigOk = true; break; } }
  if (!sigOk) return true;
  std::vector<std::uint8_t> raw; if (!crypto::base64urlDecode(body, raw)) { sigOk = false; return true; }
  bool ok = false; payload = json::Value::parse(std::string(raw.begin(), raw.end()), ok); if (!ok || !payload.isObject()) { sigOk = false; payload = json::Value(); }
  return true;
}
bool readFile(const std::string& f, std::string& out) { std::ifstream in(f, std::ios::binary); if (!in) return false; std::ostringstream ss; ss << in.rdbuf(); out = ss.str(); return true; }
bool writePrivate(const std::string& f, const std::string& text) {
  const std::size_t slash = f.find_last_of("/\\");
  if (slash != std::string::npos) {
#ifdef _WIN32
    CreateDirectoryA(f.substr(0, slash).c_str(), nullptr);
#else
    ::mkdir(f.substr(0, slash).c_str(), 0700);
#endif
  }
#ifdef _WIN32
  FILE* fp = std::fopen(f.c_str(), "wb"); if (!fp) return false; std::fwrite(text.data(), 1, text.size(), fp); std::fclose(fp); return true;
#else
  const int fd = ::open(f.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600); if (fd < 0) return false; const auto w = ::write(fd, text.data(), text.size()); ::close(fd); ::chmod(f.c_str(), 0600); return w == static_cast<ssize_t>(text.size());
#endif
}
}  // namespace

std::string fingerprint(const Jwk& k) { const std::string s = k.x + "." + k.y; const auto h = crypto::sha256(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()); const std::string hx = crypto::hex(h.data(), 8); return hx.substr(0, 4) + "-" + hx.substr(4, 4) + "-" + hx.substr(8, 4) + "-" + hx.substr(12, 4); }
const Plan* plan(const std::string& id) { for (const Plan& p : kPlans) if (id == p.id) return &p; return nullptr; }
const Plan& basic() { return kPlans[0]; }

Verdict verifyKey(const std::string& key, std::int64_t nowMs, const std::string& machine, const std::vector<Jwk>& trusted) {
  Verdict v; bool sigOk = false;
  if (!open(key, "AMIX1", trusted, v.payload, sigOk)) { v.reason = "malformed"; v.message = "This is not an A mix OS license key."; return v; }
  if (!sigOk) { v.reason = "bad-signature"; v.message = "This key was not signed by the vendor key this copy trusts (key " + fingerprint(trusted.empty() ? kVendorJwk : trusted[0]) + "). Ask the vendor for a key from the current generator."; return v; }
  const json::Value& p = v.payload; const Plan* pl = plan(p["plan"].asString());
  if (p["v"].asInt() != 1 || !pl) { v.reason = "unknown-plan"; v.message = "The license key is for an unknown plan."; v.payload = json::Value(); return v; }
  v.planId = pl->id; const double now = static_cast<double>(nowMs);
  if (p.has("issued") && p["issued"].asNumber() > now + 86400000.0) { v.reason = "not-yet-valid"; v.message = "The license key is not valid yet (check the date of this computer)."; return v; }
  if (p.has("expires") && p["expires"].asNumber() > 0 && p["expires"].asNumber() < now) { v.reason = "expired"; v.message = "The license key expired on " + dateOf(p["expires"].asNumber()) + "."; return v; }
  if (p.has("mid") && !machine.empty() && noDash(p["mid"].asString()) != noDash(machine)) { v.reason = "wrong-machine"; v.message = "This license key was issued for a different computer."; return v; }
  v.ok = true; return v;
}

Verdict verifyToken(const std::string& token, std::int64_t nowMs, const std::vector<Jwk>& trusted) {
  Verdict v; bool sigOk = false;
  if (!open(token, "AMFXE1", trusted, v.payload, sigOk)) { v.reason = "malformed"; v.message = "This is not an A mix OS edition token."; return v; }
  if (!sigOk) { v.reason = "bad-signature"; v.message = "The edition token was not signed by the vendor key (changed or forged)."; return v; }
  const json::Value& p = v.payload; const std::string ed = p["edition"].asString(), pl = p["plan"].asString();
  const bool pair = (ed == "pro" && pl == "pro") || (ed == "studio-master" && pl == "master");
  if (p["v"].asInt() != 1 || p["kind"].asString() != "amfx1-edition" || !pair) { v.reason = "wrong-edition"; v.message = "The edition token names an unknown edition or the wrong plan for it."; v.payload = json::Value(); return v; }
  const std::string serial = p["serial"].asString(), want = ed == "pro" ? "AMFX-PRO-" : "AMFX-SM-";
  bool serialOk = serial.rfind(want, 0) == 0 && serial.size() == want.size() + 14;
  if (serialOk) for (std::size_t i = want.size(); i < serial.size(); ++i) { const char c = serial[i]; const std::size_t k = i - want.size(); if (k == 4 || k == 9) { if (c != '-') serialOk = false; } else if (!((c >= '2' && c <= '9') || (c >= 'A' && c <= 'Z' && c != 'I' && c != 'O'))) serialOk = false; }
  if (!serialOk) { v.reason = "bad-serial"; v.message = "The edition token has no valid serial number."; v.payload = json::Value(); return v; }
  v.planId = pl; const double now = static_cast<double>(nowMs);
  if (p.has("issued") && p["issued"].asNumber() > now + 86400000.0) { v.reason = "not-yet-valid"; v.message = "The edition token is not valid yet (check the date of this computer)."; return v; }
  if (p.has("expires") && p["expires"].asNumber() > 0 && p["expires"].asNumber() < now) { v.reason = "expired"; v.message = "The edition expired on " + dateOf(p["expires"].asNumber()) + "."; return v; }
  v.ok = true; return v;
}

std::string machineId() {
  static std::string cached;
  if (!cached.empty()) return cached;
  std::string platform, arch, id;
#ifdef _WIN32
  platform = "win32"; arch = sizeof(void*) == 8 ? "x64" : "ia32";
  HKEY k; char buf[128] = {0}; DWORD n = sizeof buf;
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ | KEY_WOW64_64KEY, &k) == ERROR_SUCCESS) { if (RegQueryValueExA(k, "MachineGuid", nullptr, nullptr, reinterpret_cast<LPBYTE>(buf), &n) == ERROR_SUCCESS) id = buf; RegCloseKey(k); }
  if (id.empty()) { char hn[256] = {0}; DWORD hl = sizeof hn; GetComputerNameA(hn, &hl); id = hn; }
#else
#ifdef __APPLE__
  platform = "darwin";
#else
  platform = "linux";
#endif
  struct utsname u{}; if (uname(&u) == 0) { const std::string m = u.machine; arch = m == "x86_64" ? "x64" : m == "aarch64" || m == "arm64" ? "arm64" : m; }
  { std::string t; if (readFile("/etc/machine-id", t)) { while (!t.empty() && (t.back() == '\n' || t.back() == ' ')) t.pop_back(); id = t; } }
  if (id.empty()) { char hn[256] = {0}; gethostname(hn, sizeof hn - 1); id = hn; }
#endif
  const std::string s = "audio-mixer:" + platform + "|" + arch + "|" + id;
  const auto h = crypto::sha256(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()); const std::string hx = crypto::hex(h.data(), 10);
  cached = hx.substr(0, 4) + "-" + hx.substr(4, 4) + "-" + hx.substr(8, 4) + "-" + hx.substr(12, 4) + "-" + hx.substr(16, 4);
  return cached;
}

std::string homeDir() { if (const char* h = std::getenv("HOME")) return h; if (const char* h = std::getenv("USERPROFILE")) return h; return "."; }
std::string licenseFile() { if (const char* f = std::getenv("AMIXOS_LICENSE_FILE")) return f; return homeDir() + "/.amixos/license.json"; }
std::string editionFile() { if (const char* f = std::getenv("AMIXOS_EDITION_FILE")) return f; return homeDir() + "/.amixos/edition.json"; }
void setProgramDir(const std::string& dir) { programDir_ = dir; }
std::string shippedTokenFile() { if (const char* f = std::getenv("AMIXOS_EDITION_TOKEN_FILE")) return f; return (programDir_.empty() ? std::string(".") : programDir_) + "/edition-token.json"; }

Status status(std::int64_t nowMs, const std::vector<Jwk>& trusted) {
  Status st; st.machine = machineId();
  // the edition: an activation on this machine, else the shipped token (activated now and recorded)
  Verdict ed; bool haveEd = false;
  { std::string t; bool ok = false; json::Value rec = readFile(editionFile(), t) ? json::Value::parse(t, ok) : json::Value();
    if (ok && rec.isObject() && noDash(rec["machineId"].asString()) == noDash(st.machine)) { ed = verifyToken(rec["token"].asString(), nowMs, trusted); haveEd = ed.ok; }
    if (!haveEd) { std::string s; json::Value sh = readFile(shippedTokenFile(), s) ? json::Value::parse(s, ok) : json::Value(); if (ok && sh.isObject() && !sh["token"].asString().empty()) {
      ed = verifyToken(sh["token"].asString(), nowMs, trusted); haveEd = ed.ok;
      if (haveEd) { json::Value n = json::Value::object(); n.set("token", strip(sh["token"].asString())); n.set("machineId", st.machine); n.set("serial", ed.payload["serial"].asString()); n.set("edition", ed.payload["edition"].asString()); n.set("activatedAt", static_cast<double>(nowMs)); writePrivate(editionFile(), n.dump() + "\n"); }
      else if (!ed.reason.empty()) st.message = ed.message; } } }
  auto applyEdition = [&]() { const Plan* p = plan(ed.planId); st.state = "active"; st.source = "edition"; st.planId = p->id; st.planName = p->name; st.channels = p->channels; st.serial = ed.payload["serial"].asString(); st.editionName = ed.payload["edition"].asString() == "pro" ? "A mix OS Pro" : "A mix OS Studio Master"; st.id = st.serial; st.expires = static_cast<std::int64_t>(ed.payload["expires"].asNumber(0)); st.boundToThisPc = true; st.message.clear(); };
  // the key
  std::string kt; bool kok = false; const json::Value rec = readFile(licenseFile(), kt) ? json::Value::parse(kt, kok) : json::Value();
  const std::string key = kok && rec.isObject() ? rec["key"].asString() : std::string();
  if (key.empty()) { if (haveEd) applyEdition(); return st; }
  const Verdict v = verifyKey(key, nowMs, st.machine, trusted);
  if (v.ok) {
    const Plan* p = plan(v.planId);
    if (haveEd && plan(ed.planId)->rank > p->rank) { applyEdition(); return st; }
    st.state = "active"; st.source = "key"; st.planId = p->id; st.planName = p->name; st.channels = p->channels; st.id = v.payload["id"].asString(); st.name = v.payload["name"].asString(); st.expires = static_cast<std::int64_t>(v.payload["expires"].asNumber(0)); st.boundToThisPc = v.payload.has("mid"); st.message.clear();
    return st;
  }
  if (haveEd) { applyEdition(); return st; }
  st.state = v.reason == "expired" ? "expired" : "invalid"; st.message = v.message;
  if (!v.payload.isNull()) { st.id = v.payload["id"].asString(); st.name = v.payload["name"].asString(); st.expires = static_cast<std::int64_t>(v.payload["expires"].asNumber(0)); }
  return st;
}

Verdict activate(const std::string& key, std::int64_t nowMs, const std::vector<Jwk>& trusted) {
  Verdict v = verifyKey(key, nowMs, machineId(), trusted);
  if (!v.ok) return v;
  json::Value rec = json::Value::object(); rec.set("key", strip(key)); rec.set("activatedAt", static_cast<double>(nowMs));
  if (!writePrivate(licenseFile(), rec.dump() + "\n")) { v.ok = false; v.reason = "store"; v.message = "The key is genuine but could not be saved in " + licenseFile() + "."; }
  return v;
}
bool deactivate() { return std::remove(licenseFile().c_str()) == 0; }

json::Value request(const std::string& planId, const std::string& name, const std::string& email, std::int64_t nowMs, const std::string& version) {
  auto clean = [](const std::string& s, std::size_t n) { std::string o; for (unsigned char c : s) if (c >= 0x20 && c != '<' && c != '>') o += static_cast<char>(c); if (o.size() > n) o.resize(n); return o; };
  json::Value r = json::Value::object(); r.set("app", "audio-mixer"); r.set("kind", "license-request"); r.set("v", 1); r.set("machineId", machineId()); r.set("plan", plan(planId) ? planId : "pro"); r.set("name", clean(name, 80)); r.set("email", clean(email, 120));
#ifdef _WIN32
  r.set("platform", "win32");
#elif defined(__APPLE__)
  r.set("platform", "darwin");
#else
  r.set("platform", "linux");
#endif
  r.set("arch", sizeof(void*) == 8 ? "x64" : "ia32"); r.set("version", version); r.set("at", static_cast<double>(nowMs)); r.set("native", true);
  return r;
}

}  // namespace am::lic
