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
  CHECK(hostAllowed("https://raw.githubusercontent.com/amirf1389/audio-mixer/main/releases/update.json")); CHECK(hostAllowed("https://github.com/x/y/releases/download/v/a.exe"));
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
  std::printf(fails ? "update_logic_test: %d FAILED\n" : "update_logic_test: ok\n", fails);
  return fails ? 1 : 0;
}
