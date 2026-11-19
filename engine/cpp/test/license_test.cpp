// The native licence check against vectors made by the Node tooling (test/license_vectors.json: a throwaway key pair, a bound PRO key, an
// any-PC STUDIO key, a Studio Master edition token, a tampered key), RFC test vectors for SHA-256 / HMAC-SHA1 / TOTP, and the store round trip.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "license/crypto.hpp"
#include "license/license.hpp"

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
using namespace am;

int main(int argc, char** argv) {
  // hashes
  { const char* m = "abc"; const auto h = crypto::sha256(reinterpret_cast<const std::uint8_t*>(m), 3); CHECK(crypto::hex(h.data(), 32, false) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const auto s1 = crypto::sha1(reinterpret_cast<const std::uint8_t*>(m), 3); CHECK(crypto::hex(s1.data(), 20, false) == "a9993e364706816aba3e25717850c26c9cd0d89d");
    const auto e = crypto::sha256(nullptr, 0); CHECK(crypto::hex(e.data(), 32, false) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"); }
  // HMAC-SHA1 RFC 2202 case 2, TOTP RFC 6238 (SHA-1, secret "12345678901234567890")
  { const std::string k = "Jefe", d = "what do ya want for nothing?"; const auto h = crypto::hmacSha1(reinterpret_cast<const std::uint8_t*>(k.data()), k.size(), reinterpret_cast<const std::uint8_t*>(d.data()), d.size());
    CHECK(crypto::hex(h.data(), 20, false) == "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79");
    const std::string s = "12345678901234567890"; const std::vector<std::uint8_t> secret(s.begin(), s.end());
    CHECK(crypto::totp(secret, 59, 8) == 94287082u); CHECK(crypto::totp(secret, 1111111109, 8) == 7081804u); CHECK(crypto::totp(secret, 1234567890, 8) == 89005924u); CHECK(crypto::totp(secret, 20000000000LL, 8) == 65353130u);
    CHECK(crypto::verifyTotp(secret, 94287082u, 59 + 30, 8) && !crypto::verifyTotp(secret, 94287082u, 59 + 120, 8));
    std::vector<std::uint8_t> b32; CHECK(crypto::base32Decode("GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ", b32) && std::string(b32.begin(), b32.end()) == s); }
  // base64url round trip
  { const std::uint8_t raw[] = {0xfb, 0xff, 0x00, 0x7e}; const std::string e = crypto::base64url(raw, 4); std::vector<std::uint8_t> d; CHECK(e == "-_8Afg" && crypto::base64urlDecode(e, d) && d.size() == 4 && d[0] == 0xfb && d[3] == 0x7e); }
  CHECK(lic::fingerprint(lic::kVendorJwk) == "3D0E-717F-366A-EA41");   // what bridge/license.js prints for bridge/license-public.json

  // the Node-made vectors
  const std::string file = argc > 1 ? argv[1] : "license_vectors.json";
  std::ifstream in(file); CHECK(in.good()); std::ostringstream ss; ss << in.rdbuf(); bool ok = false; const json::Value vec = json::Value::parse(ss.str(), ok); CHECK(ok);
  const lic::Jwk test{vec["jwk"]["x"].asString(), vec["jwk"]["y"].asString()}; const std::int64_t now = static_cast<std::int64_t>(vec["now"].asNumber());
  CHECK(lic::fingerprint(test) == vec["fingerprint"].asString());
  { const lic::Verdict v = lic::verifyKey(vec["key"].asString(), now, "ABCDEF0123456789ABCD", {test}); CHECK(v.ok && v.planId == "pro" && v.payload["name"].asString() == "Test User"); }
  { const lic::Verdict v = lic::verifyKey(vec["key"].asString(), now, "0000-0000-0000-0000-0000", {test}); CHECK(!v.ok && v.reason == "wrong-machine"); }
  { const lic::Verdict v = lic::verifyKey(vec["key"].asString(), now + 400LL * 86400000LL, "", {test}); CHECK(!v.ok && v.reason == "expired" && v.message.find("2026-11") != std::string::npos); }
  { const lic::Verdict v = lic::verifyKey(vec["keyAny"].asString(), now, "WHAT-EVER", {test}); CHECK(v.ok && v.planId == "studio"); }
  { const lic::Verdict v = lic::verifyKey(vec["key"].asString(), now, "", {lic::kVendorJwk}); CHECK(!v.ok && v.reason == "bad-signature"); }   // the real vendor key did not sign it
  { const lic::Verdict v = lic::verifyKey(vec["tampered"].asString(), now, "", {test}); CHECK(!v.ok && v.reason == "bad-signature"); }
  { const lic::Verdict v = lic::verifyKey("hello", now, "", {test}); CHECK(!v.ok && v.reason == "malformed"); }
  { const lic::Verdict v = lic::verifyToken(vec["token"].asString(), now, {test}); CHECK(v.ok && v.planId == "master" && v.payload["serial"].asString().rfind("AMFX-SM-", 0) == 0 && v.payload["order"].asString() == "SHOP-77"); }
  { const lic::Verdict v = lic::verifyToken(vec["key"].asString(), now, {test}); CHECK(!v.ok && v.reason == "malformed"); }
  // ULTIMATE: its own plan above MASTER, 300 channels, from a signed key or a signed edition token only
  { const lic::Plan* u = lic::plan("ultimate"); CHECK(u && u->channels == 300 && u->rank == 4 && std::string(u->name) == "ULTIMATE" && lic::plan("master")->channels == 32 && !lic::plan("extend")); }
  { const lic::Verdict v = lic::verifyKey(vec["keyUltimate"].asString(), now, "", {test}); CHECK(v.ok && v.planId == "ultimate"); }
  { const lic::Verdict v = lic::verifyKey(vec["keyUltimate"].asString(), now, "", {lic::kVendorJwk}); CHECK(!v.ok && v.reason == "bad-signature"); }
  { const lic::Verdict v = lic::verifyToken(vec["tokenUltimate"].asString(), now, {test}); CHECK(v.ok && v.planId == "ultimate" && v.payload["serial"].asString().rfind("AMFX-ULT-", 0) == 0); }
  { const lic::Verdict v = lic::verifyToken(vec["tokenUltimateWrongPlan"].asString(), now, {test}); CHECK(!v.ok && v.reason == "wrong-edition"); }   // an ULTIMATE edition must carry the ULTIMATE plan

  // the store: a temp home, activate, status, deactivate
  const std::string dir = std::string(argc > 2 ? argv[2] : "/tmp") + "/amixos-lic-test"; 
  setenv("AMIXOS_LICENSE_FILE", (dir + "/license.json").c_str(), 1); setenv("AMIXOS_EDITION_FILE", (dir + "/edition.json").c_str(), 1); setenv("AMIXOS_EDITION_TOKEN_FILE", (dir + "/edition-token.json").c_str(), 1);
  setenv("AMIXOS_EXTEND_FILE", (dir + "/extend.json").c_str(), 1); std::remove((dir + "/extend.json").c_str());
  setenv("AMIXOS_ACTIVATION_FILE", (dir + "/activation.json").c_str(), 1); setenv("AMIXOS_REVOCATIONS_FILE", (dir + "/revocations.json").c_str(), 1); setenv("AMIXOS_CLOCK_FILE", (dir + "/clock.json").c_str(), 1);
  auto clockReset = [&]() { std::remove((dir + "/clock.json").c_str()); };
  std::remove((dir + "/license.json").c_str()); std::remove((dir + "/edition.json").c_str()); std::remove((dir + "/edition-token.json").c_str()); std::remove((dir + "/activation.json").c_str()); std::remove((dir + "/revocations.json").c_str()); clockReset();
  CHECK(lic::machineId().size() == 24);
  { const lic::Status s = lic::status(now, {test}); CHECK(s.state == "basic" && s.channels == 10); }
  { const lic::Verdict v = lic::activate(vec["keyAny"].asString(), now, {test}); CHECK(v.ok); const lic::Status s = lic::status(now, {test}); CHECK(s.state == "active" && s.planId == "studio" && s.channels == 32 && s.source == "key" && !s.boundToThisPc && s.needsActivation && s.graceDaysLeft == 60); }   // unbound: runs, but must be activated within 60 days
  { const lic::Verdict v = lic::activate(vec["key"].asString(), now, {test}); CHECK(!v.ok && v.reason == "wrong-machine"); }   // bound to another PC: refused, the stored key stays
  { const lic::Status s = lic::status(now + 61LL * 86400000LL, {test}); CHECK(s.state == "needs-activation" && s.planId == "basic" && s.channels == 10); }   // the grace period is over: BASIC until activated
  clockReset();
  // a shipped edition token: activated on first status(), recorded, wins over a lower key
  { std::ofstream t(dir + "/edition-token.json"); t << "{\"token\":\"" << vec["token"].asString() << "\"}\n"; }
  { const lic::Status s = lic::status(now, {test}); CHECK(s.state == "active" && s.source == "edition" && s.planId == "master" && s.editionName == "A mix OS Studio Master" && s.needsActivation); std::ifstream rec(dir + "/edition.json"); CHECK(rec.good()); }   // recorded as pending
  std::remove((dir + "/edition-token.json").c_str());
  { const lic::Status s = lic::status(now, {test}); CHECK(s.source == "edition" && s.needsActivation); }   // from the record, the installer file is gone
  { const lic::Status s = lic::status(now + 61LL * 86400000LL, {test}); CHECK(s.state != "active" || s.source != "edition"); } clockReset();   // a copied install does not run PRO past the grace
  CHECK(lic::deactivate()); { const lic::Status s = lic::status(now, {test}); CHECK(s.source == "edition" && s.planId == "master"); }
  std::remove((dir + "/edition.json").c_str()); { const lic::Status s = lic::status(now, {test}); CHECK(s.state == "basic"); }

  // ACTIVATION for this computer (signed by the throwaway key at test time), revocation list, clock roll-back
  { const lic::Verdict v = lic::verifyActivation(vec["activationOther"].asString(), now, lic::machineId(), {test}); CHECK(!v.ok && v.reason == "wrong-machine"); }
  { const lic::Verdict v = lic::verifyActivation(vec["activationOther"].asString(), now, "ABCD-EF01-2345-6789-ABCD", {test}); CHECK(v.ok && v.planId == "master"); }
  { const lic::Verdict v = lic::verifyActivation(vec["keyAny"].asString(), now, "x", {test}); CHECK(!v.ok && v.reason == "malformed"); }   // a licence key is not an activation
  if (const char* gen = std::getenv("AMIX_LICENSE_VECTOR_GEN")) {
    for (const char* term : {"perpetual", "annual"}) {
      const std::string out = dir + "/act-" + term + ".json", cmd = std::string("node \"") + gen + "\" --mid " + lic::machineId() + " --term " + term + " --revoke TEST-0004 --out \"" + out + "\" > /dev/null";
      CHECK(std::system(cmd.c_str()) == 0); std::ifstream f(out); std::ostringstream t; t << f.rdbuf(); bool aok = false; const json::Value av = json::Value::parse(t.str(), aok); CHECK(aok);
      const lic::Jwk tk{av["jwk"]["x"].asString(), av["jwk"]["y"].asString()};
      std::remove((dir + "/activation.json").c_str()); std::remove((dir + "/revocations.json").c_str()); clockReset();
      CHECK(lic::storeActivation(av["activation"].asString(), now, {tk}).ok);
      { const lic::Status s = lic::status(now, {tk}); CHECK(s.state == "active" && s.source == "activation" && s.term == term && s.planId == "studio" && s.boundToThisPc); }
      if (std::string(term) == "perpetual") {
        { const lic::Status s = lic::status(now + 3650LL * 86400000LL, {tk}); CHECK(s.state == "active"); }   // perpetual: offline for good
        clockReset(); CHECK(lic::storeRevocations(av["revocations"].asString(), {tk}));
        CHECK(!lic::storeRevocations(av["revocationsOld"].asString(), {tk}));   // an older list never replaces a newer one
        { const lic::Status s = lic::status(now, {tk}); CHECK(s.state == "revoked" && s.channels == 10); }
      } else {
        { const lic::Status s = lic::status(now + 400LL * 86400000LL, {tk}); CHECK(s.state == "expired" && s.channels == 10); }   // the year is over
        { const lic::Status s = lic::status(now, {tk}); CHECK(s.state == "expired"); }   // and setting the clock back does not bring it back
      }
    }
    { // CHANNEL EXTEND: +30 channels per pack on PRO / STUDIO / MASTER, never on BASIC, nothing to add on ULTIMATE, 300 at most
      const std::string out = dir + "/ext.json", cmd = std::string("node \"") + gen + "\" --mid " + lic::machineId() + " --out \"" + out + "\" > /dev/null";
      CHECK(std::system(cmd.c_str()) == 0); std::ifstream f(out); std::ostringstream t; t << f.rdbuf(); bool aok = false; const json::Value av = json::Value::parse(t.str(), aok); CHECK(aok);
      const lic::Jwk tk{av["jwk"]["x"].asString(), av["jwk"]["y"].asString()}; const json::Value& ex = av["extends"];
      std::remove((dir + "/activation.json").c_str()); std::remove((dir + "/revocations.json").c_str()); std::remove((dir + "/license.json").c_str()); std::remove((dir + "/extend.json").c_str()); clockReset();
      { const lic::Verdict v = lic::verifyExtend(ex.at(0).asString(), now, lic::machineId(), {tk}); CHECK(v.ok && v.payload["channels"].asInt() == 30); }
      { const lic::Verdict v = lic::verifyExtend(ex.at(0).asString(), now, lic::machineId(), {lic::kVendorJwk}); CHECK(!v.ok && v.reason == "bad-signature"); }
      { const lic::Verdict v = lic::addExtend(av["extendOther"].asString(), now, {tk}); CHECK(!v.ok && v.reason == "wrong-machine"); }
      { const lic::Verdict v = lic::addExtend(av["extendUnbound"].asString(), now, {tk}); CHECK(!v.ok && v.reason == "malformed"); }   // a pack is always for one computer
      { const lic::Verdict v = lic::addExtend(av["extendBig"].asString(), now, {tk}); CHECK(!v.ok && v.reason == "malformed"); }       // a pack is 30 channels, nothing else
      { const lic::Verdict v = lic::addExtend(av["extendExpired"].asString(), now, {tk}); CHECK(!v.ok && v.reason == "expired"); }
      { const lic::Verdict v = lic::addExtend(av["proBound"].asString(), now, {tk}); CHECK(!v.ok && v.reason == "malformed"); }        // a licence key is not a pack
      CHECK(lic::addExtend(ex.at(0).asString(), now, {tk}).ok);
      { const lic::Status s = lic::status(now, {tk}); CHECK(s.state == "basic" && s.channels == 10 && s.extendPacks == 0 && s.extendChannels == 0); }   // BASIC: a pack adds nothing
      CHECK(lic::activate(av["proBound"].asString(), now, {tk}).ok);
      { const lic::Status s = lic::status(now, {tk}); CHECK(s.planId == "pro" && s.baseChannels == 20 && s.extendPacks == 1 && s.channels == 50 && s.extendChannels == 30); }
      { const lic::Verdict v = lic::addExtend(ex.at(0).asString(), now, {tk}); CHECK(!v.ok && v.reason == "duplicate"); }             // the same pack twice counts once
      CHECK(lic::addExtend(ex.at(1).asString(), now, {tk}).ok);
      { const lic::Status s = lic::status(now, {tk}); CHECK(s.channels == 80 && s.extendPacks == 2); }
      { const lic::Status s = lic::status(now, {lic::kVendorJwk}); CHECK(s.channels == 10); }                                           // packs signed by another key: nothing
      CHECK(lic::storeRevocations(av["revocationsExt"].asString(), {tk}));
      { const lic::Status s = lic::status(now, {tk}); CHECK(s.channels == 50 && s.extendPacks == 1); }                                  // a revoked pack stops counting
      { const lic::Verdict v = lic::addExtend(ex.at(1).asString(), now, {tk}); CHECK(!v.ok && v.reason == "revoked"); }
      std::remove((dir + "/revocations.json").c_str()); CHECK(lic::removeExtends()); clockReset();
      CHECK(lic::activate(av["masterBound"].asString(), now, {tk}).ok);
      for (std::size_t i = 0; i < 9; ++i) CHECK(lic::addExtend(ex.at(i).asString(), now, {tk}).ok);
      { const lic::Status s = lic::status(now, {tk}); CHECK(s.planId == "master" && s.extendPacks == 9 && s.channels == 300 && s.extendChannels == 268); }   // 32 + 270 stops at 300
      { const lic::Verdict v = lic::addExtend(ex.at(9).asString(), now, {tk}); CHECK(!v.ok && v.reason == "full"); }
      CHECK(lic::activate(av["ultimateBound"].asString(), now, {tk}).ok);
      { const lic::Status s = lic::status(now, {tk}); CHECK(s.planId == "ultimate" && s.planName == "ULTIMATE" && s.channels == 300 && s.extendPacks == 0 && s.boundToThisPc); }
      CHECK(lic::removeExtends()); CHECK(lic::deactivate());
      { const lic::Status s = lic::status(now, {tk}); CHECK(s.state == "basic" && s.channels == 10); }
      std::printf("license_test: ULTIMATE (300 channels) and CHANNEL EXTEND packs (+30, bound, revocable, 300 at most) OK\n");
    }
    std::printf("license_test: activation (perpetual / annual), revocation list, clock roll-back OK\n");
  } else std::printf("license_test: AMIX_LICENSE_VECTOR_GEN not set: activation store tests skipped\n");
  const json::Value r = lic::request("pro", "Someone <x>", "a@b.c", now, "0.1.0"); CHECK(r["machineId"].asString() == lic::machineId() && r["name"].asString() == "Someone x" && r["plan"].asString() == "pro");
  std::printf("license_test: sha256 / sha1 / hmac / totp vectors, P-256 verify of Node-made key + token, tampered and wrong-key refused, store round trip OK (machine %s)\n", lic::machineId().c_str());
  return 0;
}
