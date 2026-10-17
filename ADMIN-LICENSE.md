# Admin license keys: a second signing authority for PRO / STUDIO upgrades

Audio Mixer already has a vendor root key (`scripts/license.js`) that signs every license key the app trusts, and a dev-only sandbox key
(`scripts/license-sandbox.js`) for trying the key flow without the vendor key. This adds a third, **production** key: the **admin key**
(`scripts/license-admin.js`), a second signing authority that can issue real PRO / STUDIO keys — with an expiration date, "all access" (STUDIO) included —
without the vendor key itself ever leaving the vendor's safe. It is meant for a support team, a reseller, or anyone the vendor wants to delegate upgrade
issuance to, separate from the one key that everything else (software updates, the vendor's own licenses) trusts.

## How it works

The admin key is a normal ECDSA P-256 key pair, handled exactly like the vendor key:

- The **public** half ships in the app (`bridge/license-admin-public.json`, and embedded in `index.html` between the `LICENSE_ADMIN_JWK` markers) so every
  installed copy — the local server (`bridge/license.js`) and the page's own WebCrypto check — trusts keys signed by it, the same as vendor keys.
- The **private** half never goes into this repository, a server process, a database, or an environment variable on any machine but the one that made it.
  `bridge/license.js` and the page only ever read the public key from disk / the embedded marker; nothing in this project ever asks for, stores, or
  transmits the private key.

This is the "truly secure key environment": the private key's only home is the local filesystem of whoever ran `init`, in a key pair file with `0o600`
permissions, never touched by any code that runs on a server or in a browser tab that isn't yours.

## Set it up once

```bash
node scripts/license-admin.js init
```

This writes `~/.audio-mixer-admin/admin-private.pem` (keep it secret, back it up somewhere safe — a password manager or an offline drive, not a cloud
sync folder) and commits the public half into `bridge/license-admin-public.json` and `index.html`. Rebuild the installers / web bundle
(`node scripts/build.js`, `node scripts/build-installers.js`, …) so the new trusted key actually ships. Use `--dir <folder>` or `AUDIO_MIXER_ADMIN_DIR` to
keep the key pair somewhere other than the default.

## Issue a client key

```bash
node scripts/license-admin.js issue --plan studio --name "Client Co" --email client@example.com --days 30
```

Prints an `AMIX1....` key on stdout (and a one-line summary on stderr). `--plan` is `basic`, `pro` or `studio` (STUDIO = every feature, "all access");
`--days` sets the expiration (defaults to 30 — an admin-issued key is meant to be a bounded upgrade, pass `--days 0` for one that never expires);
`--machine XXXX-XXXX-...` binds it to one computer (the code the LICENSE page shows); `--seats N` for a multi-seat key. Hand the printed key to the client
however you already deliver support — email, chat, your own OTA/download page — then they paste it into **LICENSE > ENTER KEY** in the app, exactly like a
vendor-issued key. `node scripts/license-admin.js show <key>` decodes and verifies any key (vendor, admin or, in sandbox builds, sandbox) without
activating it.

### Or skip the flags: `scripts/license-generator.js`

```bash
node scripts/license-generator.js --kind admin
```

Prints a local address (`http://127.0.0.1:<port>/`) to open in a browser on this machine. A one-page form — plan, name, e-mail, machine code, expiry,
seats — issues the key with a click and a COPY button, instead of typing the `issue` command above by hand. It calls the exact same `issue()` this file
already documents, reading the key pair from the same `--dir` / `AUDIO_MIXER_ADMIN_DIR` default, so everything above about where the private key lives
still applies unchanged: the key never leaves this process, and the generator never writes it anywhere but the signed key text itself. The server binds
to `127.0.0.1` only (never configurable to another host) and checks Origin / Host on every request, so nothing but a browser on this same machine can
reach it. `--kind vendor` / `--kind sandbox` drive the other two key pairs the same way.

## Where this plugs into the existing system

- **Local server** (`bridge/server.js`): `POST /api/license/activate` already calls `bridge/license.js`'s `verifyKey`, which now also trusts the admin
  key — no server change was needed beyond that one trust list. `GET /api/license` reports `admin: true` once an admin key is embedded.
- **OTA server**: licensing was never part of the OTA protocol (it only ships software updates), so an admin-issued key works the same way everywhere the
  app runs, regardless of which update channel it is on.
- **Sandbox**: unaffected — the sandbox key is still separate, dev-only, and never packaged. An admin key is the opposite: meant to ship.

## Revoking a compromised admin key

There is no online revocation list (the same is true of the vendor key): verification is fully offline by design. To stop trusting an admin key, delete
`bridge/license-admin-public.json`, run `node scripts/license-admin.js embed` with no key pair restored (or just leave the file deleted — the build
already copies it only when it exists) so the `LICENSE_ADMIN_JWK` marker in `index.html` goes back to `null`, and rebuild the installers. Every key that
admin identity signed stops working in the next version; keys already activated on a customer's machine keep running until they re-check (the local
server verifies on every activation and status read) or the customer updates.
