# Security audit checker

`scripts/security/audit.js` is a static security audit of the whole repository. Run it whenever you like. It needs only Node.js 22+
and no packages. It runs offline: the one network step (`npm audit`) is skipped with a note when the registry cannot be reached. No
code is sent to an outside service.

```sh
node scripts/security/audit.js              # full audit -> security-report.md + security-report.json (both gitignored)
scripts/security/audit.sh                   # the same, after running the checker's own unit tests
node scripts/security/audit.js --offline    # never touch the network
node scripts/security/audit.js --compile-db <cmake build dir>   # also run clang-tidy (needs compile_commands.json)
node scripts/security/audit.js --deep       # cppcheck / clang-tidy on every own C/C++ file, not only daemon / net / licence
node scripts/security/audit.js --only secrets,signing --fail-on MEDIUM
node --test scripts/security/test/audit.test.js                  # unit tests of the checker
```

Exit status: `0` means no finding at or above `--fail-on` (HIGH by default). `1` means at least one such finding. `2` means the audit
itself failed. Each finding has a severity (HIGH / MEDIUM / LOW / INFO), the check that raised it, `file:line`, and a hint on what to do.

## What is checked

| # | Check | What it looks for |
|---|---|---|
| 1 | `secrets` | PEM private keys (a header followed by key data), private JWKs (`"d"`), AWS / GitHub / Slack / Google / Stripe / npm / `sk-` tokens, JWTs, credentials in URLs, `.env` files (templates must leave secrets empty), key stores (`.pfx .p12 .jks .keystore ...`), hard-coded values for secret-looking names, weak default passwords, high-entropy literals (LOW, for review). The pinned public keys are allowlisted. |
| 2 | `signing` | No private signing key anywhere. Every EC public key in the tree is a known one. The vendor key is pinned where the apps read it (`bridge/license-public.json`, `npm-installer/lib/public-key.json`, `engine/cpp/app/license/license.cpp`, `PresetPack.kt`). The update and licence code really verifies: ECDSA P-256 in `bridge/update.js`, `native/amixos/src/Update.cpp`, `win_tools.cpp`, the npm installer and Android, plus sha256 before PackageInstaller. TLS verification is not switched off. |
| 3 | `cpp` | Unsafe calls in our own C/C++ (vendored code is excluded): `gets` (HIGH); `strcpy`/`strcat`/`sprintf`/`scanf %s`/`system`/`popen` (MEDIUM, LOW in tests); `memcpy`/`memmove` with a computed size and no bound check nearby, `strncpy`, `alloca` (LOW). Comments and string literals are ignored. cppcheck and clang-tidy (security checks) run when installed. CMake hardening is checked: stack protector, `_FORTIFY_SOURCE`, PIE, RELRO, and on Windows ASLR / DEP / CFG. Visual Studio projects are checked for ASLR / DEP / CFG. |
| 4 | `network` | An inventory of every listening component (Node.js, Python / FastAPI, C++ daemon, Qt sockets, Android sockets): default bind address, routes, and evidence of auth, rate limiting, body-size limits and CORS. Flags: binding all interfaces by default, state-changing routes without auth, `/admin` routes without the auth middleware, reflected CORS without an allow-list, file reads from request paths with no containment check, and missing security headers. |
| 5 | `portal` | `npm audit` for every lock file that has dependencies. JWT / session secrets with literal fallbacks, JWT algorithm pinning, password hashing (scrypt / argon2 / bcrypt vs fast hashes), SQL built by interpolation, security headers on the servers. Caddy: automatic HTTPS, HSTS, nosniff, size limits, staff IP allow-list. nginx: TLS versions, HSTS, autoindex. Dockerfiles: non-root `USER`, pinned base images, no baked-in secrets. Compose: privileged containers, the Docker socket, host networking, published ports. |
| 6 | `android` | Manifest: `debuggable`, `allowBackup`, `usesCleartextTraffic`, exported components without permissions, implicit exports. Network security config: clear text, user CAs, pins. Gradle: debuggable release builds, debug-key fallback, keystore passwords. Code: PackageInstaller only after signature + sha256, WebView JS bridges, file access, `onReceivedSslError -> proceed`, custom TrustManagers, secrets in logcat. |
| 7 | `web` | Every HTML page: a CSP (meta tag, or sent by the server or proxy config that serves it), SRI on CDN scripts (HIGH on sign-in / staff pages), scripts over http, inline secret literals, and staff or sign-in pages inside the public web root (`deploy/website`). |
| 8 | `deps` | Vendored third-party code with its licence (declared, and detected from the licence file), missing licence files, copyleft notes, folders with their own licence that are not in the list, and an inventory of npm / Gradle / pip dependencies, with unpinned versions flagged. |

## Configuration and exceptions

`scripts/security/config.json` holds each reviewed exception and expectation, with a reason for each: the pinned public keys,
upstream demo material that may contain keys (`native/asio/src/examples`, reported as INFO rather than hidden), the files that must
verify signatures, and the vendored folders. Fix the cause before you add an entry, and never add one only to make the audit pass.

`scripts/security/hardening.cmake` is a ready-made set of compiler and linker hardening flags for GCC, Clang, MSVC and MinGW. It is not
included by any project yet. Including it from `engine/cpp` and `native/amixos` closes the `hardening-*` findings. It has been checked on
Linux (PIE, BIND_NOW, FORTIFY abort on overflow) and with the MinGW cross toolchain (DYNAMIC_BASE, NX_COMPAT, HIGH_ENTROPY_VA).

## Tests

`scripts/security/test/audit.test.js` runs the audit over `scripts/security/test-fixtures/repo`. That tree is deliberately unsafe C++,
servers, Android, Docker, Caddy and HTML; its files end in `.fx`, so no build or linter treats them as sources. The secrets are planted
at test time: a freshly generated private key, an AWS-style key id and a `.env` file. The real scan always skips
`scripts/security/test-fixtures/`.

## Status of the first run (and what was fixed)

The first run found these HIGH findings, and all of them were fixed:

- `tools/brand3d/render.js` and `tools/hero/render.js` listened on every interface and served any file on the machine
  (`/fonts/../../etc/passwd`). They now bind 127.0.0.1 and refuse paths outside the mapped folder.
- The staff sign-in page (`bridge/license-server/dashboard/login.html`, served by the licence server and the OTA server), My account
  (`management/customer`) and the vendor manager (`management/vendor`) loaded the Tailwind Play CDN script with no integrity check. That
  CDN sends no CORS headers, so SRI cannot be used. Its "3.4.16" response also carries a Last-Modified date of October 2026, so the
  pinned URL is not an immutable file. A pinned copy now lives in
  `bridge/license-server/dashboard/vendor/` (MIT, with its licence), served as `tailwind.js`, and the CSPs allow `'self'` only.

These findings remain open. They are MEDIUM, and several are in `engine/` and `native/amixos`, which were left for their owners:

- C/C++: `strcpy` / `sprintf` in `drivers/asio/audiomixer_asio.cpp`, `strcpy` in `engine/cpp/app/plugin_shell/shell.cpp` and
  `engine/cpp/app/win_tools.cpp`, `popen` in `engine/cpp/src/bluetooth.cpp`. Hardening flags are missing in `engine/cpp/CMakeLists.txt`
  and `native/amixos/CMakeLists.txt` (include `scripts/security/hardening.cmake`).
- Android: a release build falls back to the debug key when no keystore is configured (`android/app`, `android/mixer`). The base network
  config allows clear text, which the LAN remote needs; keep every internet host in the HTTPS-only `domain-config`.
- Deployment: `deploy/ota/Caddyfile` has no HSTS. The LKMS site-host publishes port 8090 on every interface and has no rate limiting or
  security headers of its own. `deploy/nginx` uses host networking. `tools/release-manager.html` has no CSP.

See the latest `security-report.md` for the full list, including the LOW review items.
