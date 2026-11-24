// Unit test of app/update_logic.hpp (the self-update's manifest reading) against the real, signed releases/update.json.  usage: update_logic_test <path to update.json>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "../app/update_logic.hpp"
using namespace am::upd;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

int main(int argc, char** argv) {
  CHECK(cmpVersion("1.10.0", "1.9.0") > 0); CHECK(cmpVersion("1.18.0.0", "1.18.0.0") == 0); CHECK(cmpVersion("1.12.2.0", "1.18.0.0") < 0); CHECK(cmpVersion("2", "1.99.99") > 0); CHECK(cmpVersion("1.0", "1.0.0.0") == 0);
  CHECK(pluginFileOk("AMFB Warm Channel.vst3") && pluginFileOk("My-Plug_2 (v1.2).VST3"));
  for (const char* bad : {"", "a.vst3x", "plug.exe", "..vst3", ".hidden.vst3", "a/../b.vst3", "..\\x.vst3", "dir/plug.vst3", "C:plug.vst3", "plug.vst3.exe", " lead.vst3", "tab\tname.vst3", "nul\x01.vst3"}) CHECK(!pluginFileOk(bad));
  CHECK(!pluginFileOk(std::string(90, 'a') + ".vst3") && kMaxPluginBytes == 200ull * 1024 * 1024);
  CHECK(hostAllowed("https://raw.githubusercontent.com/amirf1389/audio-mixer/main/releases/update.json")); CHECK(hostAllowed("https://github.com/x/y/releases/download/v/a.exe"));
  CHECK(hostAllowed("https://ota.amirfakhribadri.my/update.json") && hostAllowed("https://amirfakhribadri.my/x") && !hostAllowed("http://ota.amirfakhribadri.my/update.json") && !hostAllowed("https://amirfakhribadri.my.evil.example/x") && !hostAllowed("https://evil-amirfakhribadri.my/x"));
  // the update source: the public OTA server by default; a local OTA server on 127.0.0.1 is an option, nothing else over plain http
  CHECK(std::string(publicManifestUrl()) == "https://ota.amirfakhribadri.my/update.json" && hostAllowed(publicManifestUrl()) && sourceOf(publicManifestUrl()) == "public");
  CHECK(localManifest(localManifestUrl()) && sourceOf(localManifestUrl()) == "local" && localManifest("http://localhost:8780/update.json") && sourceOf("https://github.com/x") == "custom");
  CHECK(!localManifest("http://127.0.0.1.evil.example/update.json") && !localManifest("http://127.0.0.1:/x") && !localManifest("http://127.0.0.1:80@evil/x") && !localManifest("https://127.0.0.1:8780/x") && !localManifest("http://10.0.0.2:8780/x"));
  CHECK(!hostAllowed("http://github.com/x")); CHECK(!hostAllowed("https://evil.example/a.exe")); CHECK(!hostAllowed("https://github.com.evil.example/a")); CHECK(!hostAllowed("https://user@github.com/a")); CHECK(!hostAllowed("https://evilgithub.com/a"));
  CHECK(b64url("AAECAw").size() == 4 && b64url("AAECAw")[3] == 3); CHECK(b64url("a b").empty());
  std::string s; std::size_t p = 0; CHECK(readString(std::string("\"a\\\"b\\\\c\\u0041\\/\""), p, s) && s == "a\"b\\cA/");
  const std::string j = "{\"a\":{\"x\":\"1\"},\"x\":\"top\",\"n\":[{\"x\":\"deep\"}],\"s\":\"{\\\"x\\\":\\\"fake\\\"}\"}";
  CHECK(getString(j, "x", s) && s == "top"); CHECK(!getString(j, "missing", s)); std::string o; CHECK(getObject(j, "a", o) && o == "{\"x\":\"1\"}");
  if (argc > 1) {
    std::ifstream f(argv[1], std::ios::binary); std::stringstream ss; ss << f.rdbuf(); const std::string env = ss.str();
    std::string payload, sig, ver, files;
    CHECK(getString(env, "payload", payload)); CHECK(getString(env, "signature", sig)); CHECK(b64url(sig).size() == 64);
    CHECK(getString(payload, "version", ver) && !ver.empty()); CHECK(getObject(payload, "files", files));
    int n = 0;
    for (const char* k : {"win-x64-exe", "win-x86-msi", "win-asio-driver", "win-x64-native", "win-x86-native"}) {
      std::string e, url, sha; if (!getObject(files, k, e)) continue;
      CHECK(getString(e, "url", url) && hostAllowed(url)); CHECK(getString(e, "sha256", sha) && isHex64(sha)); ++n;
    }
    CHECK(n >= 3);
    std::printf("manifest %s: %d files read\n", ver.c_str(), n);
  }
  { // THE UPDATE NOTICE: banner / badge / notification, each with its own memory
    using am::upd::noticeFor; const long long now = 1000000;
    { const auto n = noticeFor(false, "2.0", "", "", "", 0, now); CHECK(!n.banner && !n.badge && !n.toast); }                 // nothing newer: nothing
    { const auto n = noticeFor(true, "2.0", "", "", "", 0, now); CHECK(n.banner && n.badge && n.toast); }                     // a new version: all three
    { const auto n = noticeFor(true, "2.0", "", "2.0", "2.0", 0, now); CHECK(n.banner && !n.badge && !n.toast); }             // page opened, already notified: the banner stays
    { const auto n = noticeFor(true, "2.0", "", "", "2.0", now + 1, now); CHECK(!n.banner && n.badge && !n.toast); }           // LATER: no banner until then, the badge stays
    { const auto n = noticeFor(true, "2.0", "", "", "2.0", now, now); CHECK(n.banner); }                                      // and it is back when the time has come
    { const auto n = noticeFor(true, "2.0", "2.0", "", "", 0, now); CHECK(!n.banner && !n.badge && !n.toast); }               // SKIP THIS VERSION: silence
    { const auto n = noticeFor(true, "2.1", "2.0", "2.0", "2.0", 0, now); CHECK(n.banner && n.badge && n.toast); }            // the next version is announced again
    { const auto n = noticeFor(true, "", "", "", "", 0, now); CHECK(!n.banner && !n.badge && !n.toast); } }
  std::printf(fails ? "update_logic_test: %d FAILED\n" : "update_logic_test: ok\n", fails);
  return fails ? 1 : 0;
}
