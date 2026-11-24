# AmF X1 portal: update server + licence keys + staff dashboard

This folder runs three things together on one small server:

| Part | Code | What it does |
|---|---|---|
| **OTA update server** | `ota-server/server.js` | Gives the console and the Android app the signed update list (`/update.json`) and the release files (`/releases/...`). It also serves a public download page (`/`). |
| **Licence server + customer portal** | `bridge/license-server/server.js` | Issues licence keys and product keys. Activates and deactivates computers, reports key status and serves the signed revocation list. Customers use `/portal/`, `/account/` and the licence site `/licence/` (overview, my licence, activate, request / upgrade, downloads, help; the console and the Android app open it with MANAGE LICENCE ONLINE). |
| **Staff dashboard** | the same two servers, under `/admin/` | Covers licences, the CRM, website downloads, presets, system and backups (licence host), plus release files, manifests and the audit log (OTA host). One staff sign-in opens both. |
| **Caddy** | `Caddyfile` | Handles HTTPS with automatic certificates, HSTS, security headers, gzip, size limits and the IP allowlist for the staff hosts. |

There are no npm packages: both servers use Node.js 22 alone. Docker is optional.

## Folder layout

```
deploy/portal/
  README.md            this file
  .env.example         every setting, documented (init copies it to .env and fills in the secrets)
  init.sh / init.ps1   first-time set-up: random secrets, 0600 files, data folders
  docker-compose.yml   caddy + licence + ota, hardened (read-only root, no capabilities, user 1000)
  Dockerfile           one image for both servers (node:22.22.0-alpine3.22)
  Caddyfile            HTTPS reverse proxy for 3 host names
  start.js             without Docker: starts both servers from .env (npm start does the same)
  amfx1-portal.service systemd unit for start.js
  backup.sh            backup of all data (Docker install)
  tools/add-staff.js   creates a staff account (asks for the password)
  tools/staff-backup.js  consistent copy of the staff/CRM database
  test/e2e.test.js     end-to-end test without Docker (see "Testing")
  test/licence-site.e2e.test.js  the licence site /licence/ in a headless browser (see "Testing")
  test/caddy-routes.js the public host's routing, read from the Caddyfile (used by the tests)
  -- made by init / by you, never committed (.gitignore) --
  .env  secrets/  data/  site-releases/  backups/
```

## Which key lives where

| Material | Where | Why |
|---|---|---|
| **Vendor private key** (`vendor-private.pem`) | On **your PC** (`node scripts/license.js init` made it). A copy also goes on the server as `secrets/vendor-private.pem` (mode 0600, mounted read-only). | The licence server **needs it**. It signs every licence key (`/admin/licenses`, `/issue`, CRM "issue"), the key returned by `/redeem`, every activation (`/activate-online`, `/trial`) and the revocation list (`/revocations`). The server refuses to start if other users can read the file. It is never logged, returned or copied into the image. |
| **Vendor public key** | `bridge/license-public.json`, built into the apps and the image | The OTA server uses only this key to check manifests. It never holds a private key and refuses `OTA_PUBLIC_KEY_FILE` if that file holds one. |
| `update.json` signature | Made **only on your PC** (`node scripts/ota.js publish ...` or `scripts/make-update.js`) | The server accepts a manifest only if it verifies with the public key and its files are already uploaded with the same SHA-256. |
| `LICENSE_ADMIN_TOKEN` | `.env` (0600) | Full control of the licence server. Used by the tools here, never in a browser. |
| `OTA_ADMIN_TOKEN` | `.env`, and on your PC when you publish | Uploads and publishing. |
| `PORTAL_JWT_SECRET` | `.env` | Signs the 8-hour staff sign-in tokens. Both servers have it. |
| `PORTAL_CRM_SECRET` | `.env` | Encrypts the CRM. Only the licence server has it. |
| `secrets/licence-data.key` | made by init (0600) | Encrypts the customer name, e-mail and note in each licence record. |

**Important: one key does two jobs.** The apps check update manifests with the same public key they use for licences
(`bridge/update.js` uses `lic.PUBLIC_JWK`). The licence server must hold the matching private key. So anyone who takes
over the licence server could also sign an update manifest. Treat this server as being as sensitive as your PC's key folder:

- keep the staff hosts behind `ADMIN_ALLOW_IPS`
- keep SSH key-only
- keep the OS updated.

The proper fix is a separate update-signing key. That needs an app release that trusts a second public key for manifests
(`native/amixos`, `android`, `bridge/update.js`). It is listed under "Still to do" below.

## Step by step: a real server

1. **Get a small VPS and a domain.** Any provider works. Use 1 vCPU, 1 GB RAM, 20 GB disk, Ubuntu 24.04 or Debian 12. Turn on SSH key login and turn off password login.
2. **DNS.** At your domain registrar, create three **A records** that point to the VPS IP address, for example:
   `updates.example.com`, `admin.example.com`, `ota-admin.example.com`. If the VPS has IPv6, add AAAA records too.
3. **Firewall.** Allow only ports 22, 80 and 443: `ufw allow 22,80,443/tcp && ufw allow 443/udp && ufw enable`.
4. **Install Docker:** `curl -fsSL https://get.docker.com | sh` (or follow docs.docker.com/engine/install).
5. **Copy the code** to the server. Either clone the repository into `/opt/audio-mixer`, or copy just what the image needs:
   `bridge/`, `ota-server/`, `management/` and `deploy/portal/`.
6. **Initialise:** `cd /opt/audio-mixer/deploy/portal && sudo sh init.sh`. This writes `.env` with fresh secrets and
   `secrets/licence-data.key`, both 0600. It never prints them.
7. **Copy your vendor private key** from your PC. Then run init again so the file gets the right owner and mode:
   `scp ~/.audio-mixer-vendor/vendor-private.pem root@SERVER:/opt/audio-mixer/deploy/portal/secrets/` and then `sudo sh init.sh`.
   (Use the folder `scripts/license.js` reports as the vendor key folder.)
8. **Set the domains** in `.env`: `PUBLIC_DOMAIN`, `ADMIN_DOMAIN`, `OTA_ADMIN_DOMAIN` and `ACME_EMAIL`.
   Set `ADMIN_ALLOW_IPS` to your own IP address or addresses, for example `203.0.113.7/32`. When it is set, the staff hosts answer 404 to
   every other address. When it is not set they stay reachable from everywhere behind the password only (so existing deploys keep working),
   and `start.js` / `init.sh` print a WARNING.
   (The Caddyfile reads these values from the environment, so you do not edit it.)
9. **Start:** `docker compose up -d --build`. Check with `docker compose ps`: all three services should show *healthy* or *running*.
   Then open `https://updates.example.com/healthz`.
10. **Create the first staff account:**
    `docker compose exec licence node deploy/portal/tools/add-staff.js you@example.com "Your Name"`.
    It asks for a password of 12 or more characters.
    Then sign in at `https://admin.example.com/admin/login`. Further staff accounts can be added from the SYSTEM page.
11. **Point the apps at the update server.**
    - **Console (AmF X1, `native/amixos`):** on the Engine Control page, under the update link, enter `https://updates.example.com/update.json`
      and press **USE THIS LINK**. This is `Update::setServerLink`, stored as `update/url`. The default is the GitHub
      `releases/update.json` (`Update::kDefaultUrl` in `native/amixos/src/Update.cpp`).
    - **Bridge / Node app:** `BRIDGE_UPDATE_URL=https://updates.example.com/update.json` and `BRIDGE_UPDATE_HOSTS=updates.example.com`.
    - **Android:** the preset pack check uses `PresetPack.MANIFEST_URL` and the portal's `/portal/presets`
      (`android/app/src/main/java/com/amfb/amixos/PresetPack.kt`). To use your server by default, change that constant in an app release.
12. **Publish an update from your PC.** The key never leaves your PC:
    `OTA_ADMIN_TOKEN=<from .env> node scripts/ota.js publish --server https://ota-admin.example.com --base https://updates.example.com/releases --notes "What changed"`.
    This builds and **signs** the manifest on your PC, uploads the files the server does not have yet (each one checked by SHA-256),
    then publishes. The file URLs must start with `https://updates.example.com/releases/`. Set `--base` if `scripts/ota.js` asks for it.
    `node scripts/ota.js status --server https://ota-admin.example.com` shows the result.
13. **Customer portal downloads (optional).** Copy `releases/features.json`, `windows/*.exe|*.zip` and `android/*.apk` into
    `deploy/portal/site-releases/`. Then choose and publish the release on the WEBSITE staff page.
14. **Backups:** add `15 3 * * * cd /opt/audio-mixer/deploy/portal && sh backup.sh` to root's crontab.
    Copy `backups/` off the server regularly (`rsync`). Keep **one offline copy of `.env` and `secrets/`** in a password manager or on an encrypted USB stick.

### Updating the server software

`git pull && docker compose up -d --build`. The data is in named volumes and survives this.

### Restore

1. On a fresh server, follow steps 1 to 8, using your saved `.env` and `secrets/` (the same secrets, or the data cannot be read).
   Then run `docker compose up -d --build` once and `docker compose stop licence ota`.
2. Put the data back:
   ```
   B=backups/2026-11-21T031500Z
   docker compose run --rm -T --entrypoint sh licence -c 'tar -C /data -xzf -' < $B/licence.tgz
   docker compose run --rm -T --entrypoint sh ota -c 'tar -C /data -xzf -' < $B/ota-meta.tgz
   docker compose run --rm -T --entrypoint sh ota -c 'tar -C /data -xzf -' < $B/ota-releases.tgz   # if you made one; or re-upload with scripts/ota.js
   docker compose run --rm -T --entrypoint sh licence -c 'cat > /data/staff/portal.db' < $B/staff.db
   ```
3. Run `docker compose up -d`, then check the audit chain on the SYSTEM page and `/admin/audit/verify`.

### Rotating secrets

- **`LICENSE_ADMIN_TOKEN` / `OTA_ADMIN_TOKEN`:** change the value in `.env`, then run `docker compose up -d`.
- **`PORTAL_JWT_SECRET`:** changing it signs out every staff member. Nothing else is affected.
- **`PORTAL_CRM_SECRET` and `secrets/licence-data.key`:** do **not** change these. They encrypt stored data, so the old data would become unreadable.
- **Vendor key:** see `scripts/license.js`. A new key also needs an app release.

## Without Docker (Linux with systemd)

```
sudo apt install -y caddy            # official Caddy package; Node.js 22.13+ from nodejs.org or NodeSource
cd /opt/audio-mixer/deploy/portal && sh init.sh && npm ci        # npm ci: nothing to install, it checks the lock file
sudo useradd --system --home /opt/audio-mixer --shell /usr/sbin/nologin amfx1 && sudo chown -R amfx1: data secrets .env
sudo cp amfx1-portal.service /etc/systemd/system/ && sudo systemctl daemon-reload && sudo systemctl enable --now amfx1-portal
# Caddy: put the domain values in /etc/default/caddy (PUBLIC_DOMAIN=..., ADMIN_DOMAIN=..., OTA_ADMIN_DOMAIN=..., ACME_EMAIL=..., ADMIN_ALLOW_IPS=...)
sudo cp Caddyfile /etc/caddy/Caddyfile && sudo systemctl edit caddy   # add: [Service] EnvironmentFile=/etc/default/caddy
sudo systemctl restart caddy
```

The two servers listen on `127.0.0.1:8787` and `127.0.0.1:8780`. The Caddyfile's defaults point at those addresses.
Backup without Docker: run `LICENSE_ADMIN_TOKEN=... node tools/staff-backup.js --out staff.db`, then copy the `data/licence` and `data/ota` folders.

## On Windows, for testing (no Docker)

1. Install Node.js 22.13 or newer from nodejs.org.
2. In PowerShell, run `powershell -ExecutionPolicy Bypass -File deploy\portal\init.ps1`.
3. Put a **test** key pair in `deploy\portal\secrets\vendor-private.pem`:
   `node -e "const c=require('crypto');const k=c.generateKeyPairSync('ec',{namedCurve:'P-256'});require('fs').writeFileSync('deploy/portal/secrets/vendor-private.pem',k.privateKey.export({type:'pkcs8',format:'pem'}));require('fs').writeFileSync('deploy/portal/secrets/test-public.json',JSON.stringify(k.publicKey.export({format:'jwk'})))"`
   Then set `OTA_PUBLIC_KEY_FILE=secrets/test-public.json`, `OTA_ALLOW_HTTP=1`, `OTA_PUBLIC_URL=http://127.0.0.1:8780` and `TRUST_PROXY=0` in `.env`.
   Keys made this way never activate a real installed app. That is the point of a test key.
4. Run `node deploy\portal\start.js`.
5. Create a staff account: `node deploy\portal\tools\add-staff.js --env deploy\portal\.env you@example.com "You"`.
6. Open `http://127.0.0.1:8787/admin/login`, `http://127.0.0.1:8787/portal/` and `http://127.0.0.1:8780/`.

## Security, in short

- **HTTPS only.** Caddy redirects http to https and sends HSTS (1 year), `nosniff`, `Referrer-Policy: no-referrer` and
  `frame-ancestors 'none'`. The servers set a strict CSP per page.
- **Staff pages never appear on the public host.** `/admin/*` and `/issue` answer 404 there. Staff pages are only on
  `ADMIN_DOMAIN` and `OTA_ADMIN_DOMAIN`, behind `ADMIN_ALLOW_IPS`. Note: on Docker with IPv6, the userland proxy can hide the
  client address. Set `"userland-proxy": false` in `/etc/docker/daemon.json`, or use IPv4 only, if you rely on the allowlist.
- **Staff sign-in:**
  - Passwords are hashed with scrypt (N=16384, r=8, p=1) with a per-user salt, and need 12 or more characters.
  - 10 wrong passwords in 15 minutes lock that e-mail for 15 minutes. On top of that, there is a limit of 10 sign-in attempts a minute per address.
  - Sessions are signed tokens that end after 8 hours. "Sign out" ends every session of the account.
  - The token is kept in the tab's sessionStorage and sent as `Authorization: Bearer`, never as a cookie, so the staff forms cannot be triggered cross-site (no CSRF).
  - There is **no 2FA yet**. The IP allowlist is the second factor for now.
- **Rate limits** are applied by the servers themselves, per address:
  - public licence API: 30 a minute
  - trial: 10 a minute
  - downloads: `OTA_RATE`
  - wrong tokens: 10 a minute
- **Size limits:** request bodies are capped at 16 KB to 64 KB on the public host, 5 MB on the licence staff host and 450 MB on the OTA staff host. The servers apply their own limits on top.
- **Downloads** are only ever served from lists the server built itself. A file name from the request is checked against that list and never used as a path.
- **Audit logs** are hash-chained on both servers. They contain no key, token, name or e-mail.

## Testing

```
node --test --test-force-exit deploy/portal/test/e2e.test.js
```

The licence site (`/licence/`) has its own browser test. It runs the same portal plus a small proxy that routes as the public host of the
Caddyfile does, issues a test licence, then uses the site as a customer: the console's MANAGE LICENCE ONLINE link (machine code filled in),
activate online with the product key (and the `.amixkey` file for AmF X1), look the licence up by key and by serial + e-mail, free a
computer, send a licence request, and the signed download. It also checks that no staff route answers on the public host. Playwright
is needed (it is skipped without it):

```
NODE_PATH=/opt/node-tools/node_modules node --test --test-force-exit deploy/portal/test/licence-site.e2e.test.js
#   AMIX_SMOKE_LOG=<output of amixos --smoke>   use the console's own link    LICSITE_SHOTS=<folder>   screenshots, desktop and phone
```

This test starts `start.js` on random localhost ports with a throwaway key pair in a temporary folder, which it deletes afterwards. It checks:

- health, and that no secret appears in the log
- that every staff route refuses without a sign-in, and refuses a forged token
- creating a staff account with the admin token, then signing in; the same sign-in works on the OTA dashboard
- issuing a licence, activation, the seat limit, status, deactivation, product key to activation, revocation, the signed revocation list and the audit chain
- the OTA flow: refused damaged upload, upload, refused foreign signature, publish, fetch and verify the manifest, download with SHA-256 and ranges, `/api/latest`
- the portal downloads, presets and update list
- path traversal attempts on both servers
- oversize and broken bodies
- that the rate limits kick in
- the security fixes listed below

## How this relates to `lkms/`

`lkms/` is a separate, larger licence service built with FastAPI and PostgreSQL. It handles volume licences for several apps through one REST API.
It is **not** part of this deployment, and no app uses it yet. The apps today use the keys and activations of
`bridge/license-server`, which this folder deploys.
If you adopt `lkms` later, run it next to this stack with its own container and its own host name in the Caddyfile (`lkms/deploy`).

## Still to do (on the real server / later)

- Buy the VPS and domain, set up DNS, run the steps above, set `ADMIN_ALLOW_IPS`, and set up the backup cron job and off-site copies.
- Separate the update-signing key from the licence key (an app release; see "Which key lives where").
- Add 2FA (TOTP) for staff sign-in.
- Optionally, add an external uptime check on `https://updates.example.com/healthz`.
