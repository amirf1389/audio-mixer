// The licence check of A mix OS, natively: the same keys and tokens the Node tooling (bridge/license.js, bridge/edition.js) makes and checks.
//   Licence key    "AMIX1."  + base64url(payload JSON) + "." + base64url(ECDSA P-256 / SHA-256 signature, 64 bytes r||s)
//                  payload { v: 1, plan: basic|pro|studio|master, id?, name?, email?, issued?, expires?, mid? (machine code), seats?, admin? }
//   Edition token  "AMFXE1." + base64url(payload JSON) + "." + base64url(signature), payload { v: 1, kind: "amfx1-edition", edition: pro|studio-master,
//                  plan, serial: AMFX-PRO-XXXX-XXXX-XXXX | AMFX-SM-..., issued, order?, expires? }
// The app holds only the vendor PUBLIC key (bridge/license-public.json, embedded below as kVendorJwkX / Y). Without a valid key or token the app
// runs BASIC (10 channels). As in the Node code: the signature stops forged keys; it does not stop someone who edits a locally running program.
// TOTP (RFC 6238) is here for the vendor's Key Generator second factor and tests, not for end users.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "daemon/json.hpp"

namespace am::lic {

struct Jwk { std::string x, y; };                       // base64url, 32 bytes each
extern const Jwk kVendorJwk;                             // bridge/license-public.json
std::string fingerprint(const Jwk& k);                   // "3D0E-717F-366A-EA41": sha256(x + "." + y), first 16 hex, upper

struct Plan { const char* id; const char* name; int channels; int rank; };
const Plan* plan(const std::string& id);                 // nullptr for an unknown plan
const Plan& basic();

struct Verdict {
  bool ok = false;
  std::string reason;                                    // malformed | bad-signature | unknown-plan | wrong-edition | bad-serial | not-yet-valid | expired | wrong-machine
  std::string message;                                   // for the user
  json::Value payload;                                   // decoded even when !ok (expired, wrong machine), Null for malformed / bad signature
  std::string planId;                                    // payload.plan when known
};
// now: ms since the epoch. machine: this computer's code (compared without dashes) when the key is bound; empty = do not check.
Verdict verifyKey(const std::string& key, std::int64_t nowMs, const std::string& machine, const std::vector<Jwk>& trusted = {kVendorJwk});
Verdict verifyToken(const std::string& token, std::int64_t nowMs, const std::vector<Jwk>& trusted = {kVendorJwk});

// ACTIVATION (Windows-style): a product key or licence key is traded once, online, for an activation token bound to this computer's
// machine code: "AMXA1." + base64url(payload) + "." + signature, payload { v: 1, kind: "amfx1-activation", kid, lid (licence id), mid,
// plan, term: perpetual | annual | trial | limited, iat, exp (ms, 0 = never) }. Once activated the computer runs its plan OFFLINE for the
// whole term (no check-in); the licence server enforces the seats when it activates, and stops a leaked or refunded licence through the
// REVOCATION LIST "AMXR1." + base64url({ v: 1, kind: "amfx1-revocations", kid, issued, lids: [...] }) + "." + signature, which the app keeps
// (the newest it has seen) and checks every licence id against, offline.
Verdict verifyActivation(const std::string& token, std::int64_t nowMs, const std::string& machine, const std::vector<Jwk>& trusted = {kVendorJwk});
Verdict verifyRevocations(const std::string& list, const std::vector<Jwk>& trusted = {kVendorJwk});
std::string activationFile();                            // ~/.amixos/activation.json
std::string revocationsFile();                           // ~/.amixos/revocations.json
std::string clockFile();                                 // ~/.amixos/clock.json (the latest trusted time seen: setting the clock back revives nothing)
Verdict storeActivation(const std::string& token, std::int64_t nowMs, const std::vector<Jwk>& trusted = {kVendorJwk});   // verified for this computer, then stored
bool storeRevocations(const std::string& list, const std::vector<Jwk>& trusted = {kVendorJwk});   // kept only when genuine and newer than the one held
bool isRevoked(const std::string& lid, const std::vector<Jwk>& trusted = {kVendorJwk});
std::int64_t trustedNow(std::int64_t nowMs);             // max(clock, latest trusted time seen - 1 day); moves the mark forward
constexpr std::int64_t kGraceMs = 60LL * 86400000LL;     // an unbound key or a not yet activated edition runs this long before it must be activated once

// A stable, non-secret code for this computer (the same recipe as the Node machineId: sha256("audio-mixer:" + platform|arch|machine guid)).
std::string machineId();

// What the app runs as: the stored key (~/.amixos/license.json) and / or an edition token (shipped edition-token.json next to the program,
// activation recorded in ~/.amixos/edition.json bound to this machine). The higher plan wins. An invalid or expired key never raises the plan.
struct Status {
  std::string state = "basic";                          // basic | active | expired | invalid | revoked | needs-activation
  std::string planId = "basic"; std::string planName = "BASIC"; int channels = 10;
  std::string source;                                    // activation | key | edition | ""
  std::string id, name, serial, editionName, message; std::int64_t expires = 0; bool boundToThisPc = false;
  std::string machine;
  std::string term;                                      // perpetual | annual | trial | limited (activation), "" otherwise
  bool needsActivation = false; int graceDaysLeft = 0;    // an unbound key / pending edition: activate online within graceDaysLeft days
};
std::string homeDir();
std::string licenseFile();                               // ~/.amixos/license.json
std::string editionFile();                               // ~/.amixos/edition.json
std::string shippedTokenFile();                          // <program dir>/edition-token.json (set with setProgramDir)
void setProgramDir(const std::string& dir);
Status status(std::int64_t nowMs, const std::vector<Jwk>& trusted = {kVendorJwk});
Verdict activate(const std::string& key, std::int64_t nowMs, const std::vector<Jwk>& trusted = {kVendorJwk});   // verifies, then stores (0600)
bool deactivate();                                       // removes the stored key; true when something was removed
// what the customer sends the vendor to get a key: machine code, plan wanted, who for. Nothing secret.
json::Value request(const std::string& planId, const std::string& name, const std::string& email, std::int64_t nowMs, const std::string& version);

}  // namespace am::lic
