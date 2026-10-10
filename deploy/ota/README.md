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
    npm run build:installers && npm run build:unix        # produce the files in releases/ (see the main README)
    node scripts/ota.js publish --server https://ota.example.com --token "$OTA_ADMIN_TOKEN" --notes "What is new|Another note"
    node scripts/ota.js status  --server https://ota.example.com --token "$OTA_ADMIN_TOKEN"

`publish` builds the manifest of the version in `package.json` from `releases/`, signs it, uploads the files the server does not have, then
publishes the manifest. `--channel beta` publishes to `/beta/update.json`; `--force` replaces a version that is already published.

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

Admin routes answer 404 without `OTA_ADMIN_TOKEN`, 401 with a wrong token (10 wrong tries a minute per address, then 429). The data folder holds
`releases/`, `manifests/` (and `manifests/history/`), `index.json` and `stats.json`; back it up with the vendor key.
