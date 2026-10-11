# Audio Mixer OTA server

`ota-server/server.js` is the update server that the app's **Check for updates** asks. It serves the signed manifest (`/update.json`) and the
release files (`/releases/<file>`), and has an admin API to publish new versions. Plain Node.js (18+), no packages to install.

The server never has the vendor private key. Manifests are signed on **your** machine (`scripts/ota.js`) and the server only accepts a manifest
whose signature verifies with the Audio Mixer public key, whose files it holds with the same size and SHA-256, whose version is newer than the
published one, and (with `OTA_PUBLIC_URL`) whose file URLs point at this server. The app verifies the signature again and the SHA-256 of every download.

## Run it
    export OTA_ADMIN_TOKEN="$(openssl rand -hex 32)"      # keep it: it is the publish password
    export OTA_PUBLIC_URL=https://ota.example.com
    node ota-server/server.js --host 127.0.0.1 --port 8780 --data /var/lib/audio-mixer-ota

The app only talks to **https**. Either put nginx / Caddy in front (`nginx.conf`, `Caddyfile` here) or give the server a certificate directly
(`OTA_TLS_CERT=/path/fullchain.pem OTA_TLS_KEY=/path/privkey.pem`). Other settings: `OTA_TRUST_PROXY=1` (behind a proxy: use `X-Forwarded-For` for
rate limits), `OTA_RATE` (downloads per minute per address, default 120), `--seed <releases dir>` (take over an existing `releases/` folder and its `update.json`).

Everything: `ota-server.service` (systemd), `docker-compose.yml` + `Dockerfile` (container).

## Publish a version (on the machine that has the vendor key)
    node scripts/license.js init                          # once: creates the vendor key (private.pem stays on this machine)
    npm run build:installers && npm run build:unix        # produce the files in the `<os>/releases/` folders (see the main README)
    node scripts/ota.js publish --server https://ota.example.com --token "$OTA_ADMIN_TOKEN" --notes "What is new|Another note"
    node scripts/ota.js status  --server https://ota.example.com --token "$OTA_ADMIN_TOKEN"

`publish` builds the manifest of the version in `package.json` from the `<os>/releases/` folders (`--releases <folder>` takes one flat folder instead), signs it, uploads the files the server does not have, then
publishes the manifest. `--channel beta` publishes to `/beta/update.json`; `--force` replaces a version that is already published.

## Web dashboard
With `OTA_ADMIN_TOKEN` set, open `https://ota.example.com/admin/` and sign in with the token (kept in the tab's memory only; "remember in this tab" uses `sessionStorage`).
- **Overview:** every channel: version, release date, notes, files per platform, a "signature valid" check done in the browser against the public key, the `BRIDGE_UPDATE_URL` to give the apps, Unpublish.
- **Files:** drag in the release files: the SHA-256 is computed in the browser, the upload shows progress and the server checks the hash again; list with "listed by" badges, Delete (not for files a manifest lists).
- **Publish:** the version and the file for each platform are filled in from the file names; add notes, choose your `private.pem`, preview the manifest, **Sign and publish**. The key is imported as a non-extractable WebCrypto key and **signs in the browser; it is never uploaded**. The server and the apps verify the signature like any other.
- **History:** every manifest ever published per channel, with **Roll back to this** (publishes the older, already signed manifest again).
- **Audit:** every upload, delete, publish, roll back, unpublish and file check, refused sign-ins, rate-limit hits and dashboard openings: when, from where (address, browser), with which token (the first 8 hex characters of its SHA-256, never the token) and what the server answered (ok / rejected with the reason / denied / error). Filter by action and result, search, load older entries, **Verify the log**, **Download (JSON lines)**.
- **Stats:** update checks and downloads per day, downloads per file. **Health:** re-hash every file on the disk and report damage or missing files.
The dashboard files are served from the same origin under a strict Content-Security-Policy (no external scripts or styles, no inline code, not framable); it writes server data as text only. Keep `/admin/` behind your IP allow-list in nginx (see `nginx.conf`) as well.

## Point the apps at it
    BRIDGE_UPDATE_URL=https://ota.example.com/update.json
    BRIDGE_UPDATE_HOSTS=ota.example.com

(on every PC that runs the Audio Mixer server; for the Windows service / autostart set them as machine environment variables.)
Without them the app asks the project's GitHub `releases/update.json`.

## API
| | |
|---|---|
| `GET /update.json`, `GET /<channel>/update.json` | signed manifest (ETag, `Cache-Control: no-cache`) |
| `GET/HEAD /releases/<file>` | file, `Range` (resume) supported |
| `GET /healthz` | `{ ok, channels: { stable: "1.4.1.0" } }` |
| `PUT /admin/files/<file>` | upload, header `X-SHA256` required and checked; not replaced without `?overwrite=1` |
| `GET /admin/files` | files, sizes, SHA-256, which manifests list them |
| `DELETE /admin/files/<file>` | only files no manifest lists |
| `PUT /admin/manifest/<channel>` | publish a signed manifest (`?force=1` for same / older versions) |
| `GET /admin/stats` | update checks and downloads per day |
| `GET /admin/config` | public URL, public key, limits |
| `GET /admin/history/<channel>`, `/<version>` | published versions (newest first); the signed envelope of one |
| `DELETE /admin/manifest/<channel>` | unpublish (history stays) |
| `GET /admin/verify` | hash every file on disk again |
| `GET /admin/` | the dashboard |
| `GET /admin/audit?limit=&before=&action=&result=` | audit log, newest first (`action` exact or a prefix ending in `.`, `result` ok / rejected / denied / error) |
| `GET /admin/audit/verify`, `GET /admin/audit/export` | check the hash chain; the whole log as JSON lines (the export is logged too) |

Admin routes answer 404 without `OTA_ADMIN_TOKEN`, 401 with a wrong token (10 wrong tries a minute per address, then 429). The data folder holds
`releases/`, `manifests/` (and `manifests/history/`), `index.json` and `stats.json`; back it up with the vendor key.

## Audit log
`<data>/audit.jsonl` is an append-only JSON-lines file, one entry per line: `seq`, `t` (UTC), `actor` (`fp` = first 8 hex characters of the SHA-256 of the admin token used, `ip`, `ua`), `action`, `target`, `detail`, `status`, `result`, `prev`, `hash`. Each `hash` covers the entry and the hash before it, so an edited, removed or inserted line is found by `GET /admin/audit/verify` (or "Verify the log" in the dashboard), which names the entry where the chain breaks; the file also stops being believable if someone with access to the disk rewrites it **and** every line after the change, so copy it off the server now and then (`/admin/audit/export`) if that matters to you. Actions: `file.upload`, `file.delete`, `manifest.publish`, `manifest.rollback` (a publish of an older version), `manifest.unpublish`, `files.verify`, `audit.export`, `session.open` (once per token and address per 30 minutes), `auth.denied` (at most 20 per address and minute), `admin.rate_limited` (at most 5). A refused attempt never stores the token it offered. Reads (files, stats, history, the audit log itself) are not logged. To rotate, move the file away: the next entry starts a new chain. The server has one admin token, so the log tells tokens apart by that fingerprint and people by address.
