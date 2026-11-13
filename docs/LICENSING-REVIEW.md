# Licensing: security review and proposed structure

Reviewed: `engine/cpp/app/license/` (keys, edition tokens, machine code, storage), `engine/cpp/app/license/crypto.cpp` (ECDSA P-256),
`bridge/license.js`, `bridge/license-server/server.js` (redeem / activate / status / admin), `native/amixos/src/Update.cpp` (redeem from the app).

## What is already right
- **Signatures**: ECDSA P-256 / SHA-256, vendor private key never in the app; the verifier checks r, s in [1, n-1], the public point on the curve,
  and a malformed or unsigned payload never decodes. A key cannot be forged without the private key.
- **Product keys** (25 characters) are never checked by the app: they are traded for a signed key at the licence server (`/redeem`),
  which never stores the product key in clear. 125 bits: not guessable; `/redeem` is rate-limited.
- Stored files are 0600 (Linux / macOS); an invalid or expired key never raises the plan.

## Holes found (most serious first)

| # | Hole | Effect | Severity |
|---|------|--------|----------|
| H1 | The app **never asks the licence server** (`/activate`, `/status` exist but only `/redeem` is called) | A key without a machine code (`mid`) works on any number of computers; the server's seat limit and **revocation are never enforced** | High |
| H2 | The **shipped edition token** (`edition-token.json` next to the program) is a bearer token: the first start binds it to whatever computer runs it | Copying the install folder copies PRO / STUDIO MASTER to any computer | High |
| H3 | **Clock roll-back**: expiry uses the computer's clock | Set the date back and an expired key or subscription keeps working | Medium |
| H4 | **No revocation in the app** (no list, no check) | A leaked, refunded or charged-back key keeps working forever | Medium |
| H5 | **Two spellings of one key**: ECDSA signatures are malleable (s and n−s both verify) | Anything that blocks keys by their text can be dodged; block by licence `id` instead | Low (design rule) |
| H6 | Seat registry is a JSON file read-modify-written without a lock | Two activations at the same moment can pass the seat limit | Low |
| H7 | No key id (`kid`) in keys or tokens | Rotating the vendor key after a leak means every old key fails, with no orderly overlap | Low |
| H8 | `/status`, `/activate` take any machine code the client sends | A key holder can fill their own seats with invented codes (only hurts them); machine codes are not secret | Info |

Not a hole, by design: someone who patches the running program can skip any local check. The aim is to stop casual
sharing, copying and key leaks, not a determined cracker; heavier protection (packers, anti-debug) costs real users more than it saves.

## Built (this change)

| Hole | Fix | Where |
|------|-----|-------|
| H1 | **Online activation**: product key / licence key / edition token + machine code → `AMXA1` activation bound to that computer; seats and revocation enforced at activation | `/activate-online` (server), `lic::verifyActivation / storeActivation`, LICENSE: ACTIVATE ONLINE |
| H2 | Shipped edition tokens no longer activate themselves for good: they run a **60-day grace**, then must be activated online once | `lic::status` (edition recorded as pending) |
| H1 | Unbound `AMIX1` keys: 60-day grace (counted from the first start of this version), then BASIC until activated | `lic::status` (`graceStart`) |
| H3 | **Clock roll-back**: a high-water mark of trusted time (own runs, activation `iat`, revocation list `issued`); "now" never goes back more than a day | `lic::trustedNow`, `clock.json` |
| H4 | **Revocation list** `AMXR1`, signed, fetched at start from the licence server, kept only when genuine and newer; checked by licence id offline | `/revocations`, `lic::storeRevocations / isRevoked` |
| H5 | Revocation is by licence id, so a second spelling of a key changes nothing | `isRevoked` |
| H7 | Every activation and revocation list carries `kid` (the signing key's fingerprint) | server `activationFor`, `revocationList` |
| — | **Terms**: perpetual (offline for good), annual (one year), trial (30 days, once per computer, STUDIO), limited (days) | `buildPayload` `term`, `/trial` |

Offline after activation: **unlimited** for perpetual, **to the end of the year** for annual, **30 days** for a trial; no check-in needed.
Still open: H6 (the registry is replaced whole and Node runs one request at a time, so it holds in one process; a second server process
would need SQLite) and moving the revocation list into the signed update manifest as a second channel.

## Proposed structure: licence → activation

Three signed objects, each with one job:

1. **Licence key** `AMIX2.<payload>.<sig>` (long-lived, from the store / Key Generator)
   `{ v:2, kid, lid (licence id), plan, seats, issued, expires?, sub? (subscription), name?, email-hash? }`
   It says *what was bought*. It does not by itself turn PRO on for an unbound key.
2. **Activation token** `AMXA1.<payload>.<sig>` (short-lived, signed by the licence server)
   `{ v:1, kid, lid, mid (this computer), plan, iat, exp (iat + 30 days), seq }`
   Issued by `/activate`, renewed silently by `/refresh` while online. The app runs the plan **only with a valid activation for this
   computer** (or a vendor-bound key, item 3). Offline: works until `exp` (30 days), then BASIC until it can reach the server.
   Seats and revocation are enforced at every renewal: a revoked or over-seated licence simply is not renewed.
3. **Vendor-bound key** (today's `mid` keys): for offline studios, the vendor binds the key to one machine code by hand; it needs no
   server, but is checked against the **revocation list** (4).
4. **Revocation list** `AMXR1.<payload>.<sig>` `{ issued, lids: [...] }`, small, signed by the vendor, shipped inside every update
   manifest and fetched with the update check; the app keeps the newest one it has seen.
5. **Edition tokens** stop being bearer tokens: the shipped token is redeemed once at `/activate` like a licence (its serial is the
   licence id), so copying the folder copies nothing usable.
6. **Clock**: the app keeps a high-water mark of the latest trusted time it has seen (activation `iat`, revocation list `issued`,
   update manifest time, its own last run). "Now" for every expiry check is the later of the clock and that mark (minus a day of slack).
7. **Key rotation**: every object carries `kid`; the app trusts a short list of public keys; old `kid`s are retired on a schedule.

Server: one SQLite table per object (licences, activations with seq, revocations) in one transaction per activation (fixes H6);
`/refresh` returns a new activation token or the reason it will not (revoked, seats, expired subscription).

## Migration (no customer loses anything)
- `AMIX1` keys keep working: with `mid` as vendor-bound keys (plus the revocation list); without `mid`, they get a 60-day grace in which
  the app activates them online once and from then on renews like `AMIX2`.
- Shipped edition tokens already activated on a computer keep working there; new installs activate online.

## Test plan
Unit tests in `engine/cpp/test/license_test.cpp` and `bridge/license-server/test.js`:
- activation for another machine code refused; expired activation → BASIC; renewal moves `exp`
- revoked `lid` refused online and through the list offline; a second spelling (s → n−s) of a revoked key still refused
- clock set back past the high-water mark does not revive an expired key
- seat limit holds under 20 simultaneous `/activate` calls
- copied install folder with a shipped token on a second machine stays BASIC
