// SECURE ASIO LOADING: the enumeration with a mock registry / loader (trusted vs untrusted DLL paths, unsafe variables, unsigned marking),
// selection by registered name only (a remote / settings value cannot name a path), driver-reported values clamped, faulted drivers.
#include "audiomixer/asio_trust.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>

using namespace am::asiotrust;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

struct MockEnv : Env {
  std::vector<RegEntry> e64, e32; std::set<std::string> files; std::map<std::string, Sig> sigs;
  std::map<std::string, std::string> vars{{"systemroot", "C:\\Windows"}, {"windir", "C:\\Windows"}, {"programfiles", "C:\\Program Files"}, {"programfiles(x86)", "C:\\Program Files (x86)"},
                                          {"programw6432", "C:\\Program Files"}, {"userprofile", "C:\\Users\\bob"}, {"temp", "C:\\Users\\bob\\AppData\\Local\\Temp"}};
  std::vector<RegEntry> entries(int bits) override { return bits == 64 ? e64 : e32; }
  bool getVar(const std::string& n, std::string& v) override { auto it = vars.find(n); if (it == vars.end()) return false; v = it->second; return true; }
  bool fileExists(const std::string& p) override { return files.count(p) > 0; }
  Sig signature(const std::string& p) override { auto it = sigs.find(p); return it == sigs.end() ? Sig::Unsigned : it->second; }
};
static const char* G1 = "{11111111-2222-3333-4444-555555555555}";
static const char* G2 = "{22222222-2222-3333-4444-555555555555}";

static const Rejected* findRej(const std::vector<Rejected>& r, const std::string& n) { for (const auto& x : r) if (x.name == n) return &x; return nullptr; }

int main() {
  // CLSID form
  CHECK(validClsid(G1)); CHECK(!validClsid("{1111111-2222-3333-4444-555555555555}")); CHECK(!validClsid("11111111-2222-3333-4444-555555555555"));
  CHECK(!validClsid("{1111111G-2222-3333-4444-555555555555}")); CHECK(!validClsid("C:\\evil.dll"));

  // paths
  std::string out, why;
  CHECK(normalizePath("C:/Program Files/X/x.dll", out, why) && out == "C:\\Program Files\\X\\x.dll");
  CHECK(!normalizePath("\\\\server\\share\\x.dll", out, why));                // UNC
  CHECK(!normalizePath("\\\\?\\C:\\Windows\\System32\\x.dll", out, why));     // device path
  CHECK(!normalizePath("x.dll", out, why)); CHECK(!normalizePath("drivers\\x.dll", out, why));   // relative
  CHECK(!normalizePath("C:\\Program Files\\..\\Users\\bob\\x.dll", out, why));
  CHECK(!normalizePath("C:\\Program Files\\x.dll:evil", out, why));          // stream
  auto gv = [](const std::string& n, std::string& v) { if (n == "systemroot") { v = "C:\\Windows"; return true; } if (n == "temp") { v = "C:\\T"; return true; } return false; };
  CHECK(expandSafe("%SystemRoot%\\System32\\a.dll", gv, out) && out == "C:\\Windows\\System32\\a.dll");
  CHECK(!expandSafe("%TEMP%\\a.dll", gv, out));        // a user variable is never expanded
  CHECK(!expandSafe("%SystemRoot\\a.dll", gv, out));   // unclosed

  MockEnv env;
  auto add = [&](std::vector<RegEntry>& v, const std::string& name, const std::string& clsid, const std::string& dll, bool sz = false) { v.push_back({name, clsid, dll, sz, 64}); };
  add(env.e64, "Good Signed", G1, "C:\\Program Files\\Vendor\\good.dll"); env.files.insert("C:\\Program Files\\Vendor\\good.dll"); env.sigs["C:\\Program Files\\Vendor\\good.dll"] = Sig::Valid;
  add(env.e64, "Sys Unsigned", G2, "%SystemRoot%\\System32\\asio2.dll", true); env.files.insert("C:\\Windows\\System32\\asio2.dll");
  add(env.e64, "In Temp", "{33333333-2222-3333-4444-555555555555}", "C:\\Users\\bob\\AppData\\Local\\Temp\\evil.dll"); env.files.insert("C:\\Users\\bob\\AppData\\Local\\Temp\\evil.dll");
  add(env.e64, "User Var", "{44444444-2222-3333-4444-555555555555}", "%USERPROFILE%\\evil.dll", true);
  add(env.e64, "Relative", "{55555555-2222-3333-4444-555555555555}", "evil.dll");
  add(env.e64, "Unc", "{66666666-2222-3333-4444-555555555555}", "\\\\attacker\\share\\evil.dll");
  add(env.e64, "Bad Clsid", "not-a-clsid", "C:\\Program Files\\Vendor\\good.dll");
  add(env.e64, "Missing", "{77777777-2222-3333-4444-555555555555}", "C:\\Program Files\\Gone\\gone.dll");
  add(env.e64, "Lookalike", "{88888888-2222-3333-4444-555555555555}", "C:\\Program Files Evil\\x.dll"); env.files.insert("C:\\Program Files Evil\\x.dll");
  add(env.e64, "Dotdot", "{99999999-2222-3333-4444-555555555555}", "C:\\Program Files\\..\\Users\\x.dll");
  add(env.e64, "Not Dll", "{AAAAAAAA-2222-3333-4444-555555555555}", "C:\\Program Files\\Vendor\\x.exe"); env.files.insert("C:\\Program Files\\Vendor\\x.exe");
  add(env.e32, "Good Signed", G1, "C:\\Program Files (x86)\\Vendor\\good.dll");   // the same CLSID in the 32-bit view: listed once, as 64-bit
  RegEntry w32{"Old 32", "{BBBBBBBB-2222-3333-4444-555555555555}", "C:\\Windows\\SysWOW64\\old.dll", false, 32}; env.e32.push_back(w32); env.files.insert("C:\\Windows\\SysWOW64\\old.dll"); env.sigs["C:\\Windows\\SysWOW64\\old.dll"] = Sig::Invalid;
  add(env.e64, std::string("Bad\x01Name\xff\xfe") + std::string(100, 'x'), "{CCCCCCCC-2222-3333-4444-555555555555}", "C:\\Program Files\\V\\n.dll"); env.files.insert("C:\\Program Files\\V\\n.dll");

  std::vector<Rejected> rej; const std::vector<Driver> list = enumerate(env, &rej);
  CHECK(list.size() == 4);
  CHECK(list.size() > 0 && list[0].name == "Good Signed" && list[0].sig == Sig::Valid && list[0].bits == 64);
  CHECK(list.size() > 1 && list[1].name == "Sys Unsigned" && list[1].sig == Sig::Unsigned && list[1].path == "C:\\Windows\\System32\\asio2.dll");
  bool sawOld = false, sawLong = false;
  for (const Driver& d : list) { if (d.name == "Old 32") { sawOld = d.bits == 32 && d.sig == Sig::Invalid; } if (d.clsid[1] == 'C') sawLong = d.name.size() <= kMaxDriverName && d.name.find('\x01') == std::string::npos && d.name.find("\xff") == std::string::npos; }
  CHECK(sawOld); CHECK(sawLong);
  for (const char* n : {"In Temp", "User Var", "Relative", "Unc", "Bad Clsid", "Missing", "Lookalike", "Dotdot", "Not Dll"}) { const Rejected* r = findRej(rej, n); CHECK(r != nullptr); if (!r) std::printf("  not rejected: %s\n", n); }
  if (const Rejected* r = findRej(rej, "In Temp")) CHECK(r->reason.find("outside") != std::string::npos);

  // selection: names only
  CHECK(select(list, "") == &list[0]);
  CHECK(select(list, "good signed") == &list[0]);
  CHECK(select(list, G2) == &list[1]);
  CHECK(select(list, "Unsigned") == &list[1]);
  CHECK(select(list, "C:\\Users\\bob\\evil.dll") == nullptr);   // remote / settings / command line: a path selects nothing
  CHECK(select(list, "evil.dll") == nullptr);
  CHECK(select(list, "\\\\attacker\\share\\x") == nullptr);
  CHECK(select(list, "{DEADBEEF-2222-3333-4444-555555555555}") == nullptr);   // a CLSID not in the registry list
  CHECK(select(list, std::string(300, 'a')) == nullptr);
  std::string w; CHECK(select(list, "C:/x/y.dll", &w) == nullptr && w.find("path") != std::string::npos);

  // unsigned: listed, not loaded before a confirmation; a swapped DLL path needs a new one
  CHECK(loadAllowed(list[0]));
  CHECK(!loadAllowed(list[1], &w) && w.find("unsigned") != std::string::npos);
  allowUnsigned(list[1].clsid, list[1].path); CHECK(loadAllowed(list[1]));
  CHECK(!unsignedAllowed(list[1].clsid, "C:\\Program Files\\Other\\asio2.dll"));
  allowUnsigned(list[1].clsid, list[1].path, false); CHECK(!loadAllowed(list[1]));

  // faulted: not loaded again in this run
  markFaulted(G1, "init timed out"); CHECK(isFaulted(G1)); CHECK(!loadAllowed(list[0], &w) && w.find("init timed out") != std::string::npos);
  clearFaulted(); CHECK(loadAllowed(list[0]));

  // driver-reported values
  CHECK(clampChannels(-5) == 0); CHECK(clampChannels(100000) == kMaxChannels); CHECK(clampChannels(8) == 8);
  BufferSizes b{64, 2048, 99999, 0}; CHECK(validateBuffers(b) && b.pref == 64);
  b = {0, 512, 256, -1}; CHECK(!validateBuffers(b));
  b = {512, 64, 256, -1}; CHECK(!validateBuffers(b));
  b = {64, 1 << 30, 256, -7}; CHECK(validateBuffers(b) && b.mx == kMaxBuffer && b.gran == 0);
  CHECK(validRate(48000)); CHECK(!validRate(0)); CHECK(!validRate(1e12)); CHECK(!validRate(std::nan("")));
  CHECK(clampLatency(-1) == 0); CHECK(clampLatency(0x7fffffffL) == (1L << 20));
  char raw[32]; std::memset(raw, 'A', sizeof raw);   // a channel name without its terminating zero
  CHECK(sanitizeText(raw, sizeof raw, kMaxChannelName) == std::string(32, 'A'));
  CHECK(sanitizeText("\xe2\x82\xac ok", 6, 32) == "\xe2\x82\xac ok");   // valid UTF-8 kept
  CHECK(sanitizeText("a\xc0\xafz", 4, 32) == "a??z");                   // overlong form replaced
  CHECK(sanitizeText("\xe2\x82\xac\xe2\x82\xac", 6, 4) == "\xe2\x82\xac");   // never cut inside a character
  CHECK(sanitizeText("", 0, 8, "Driver") == "Driver");
  CHECK(sanitizeText("x\ny\tz", 5, 8) == "x y z");

  std::printf(fails ? "asio_trust: %d FAILED\n" : "asio_trust: all passed\n", fails);
  return fails ? 1 : 0;
}
