# nginx proxy for Audio Mixer

`audio-mixer.conf` puts nginx in front of the mixer page and the local Node bridge:
HTTPS (TLS 1.2/1.3, HSTS), security headers, gzip, rate limiting, WebSocket upgrade for `/ws/output`,
and no exposure of dotfiles, bridge sources or repo metadata.

## Install (Linux)
    sudo cp deploy/nginx/audio-mixer.conf /etc/nginx/conf.d/audio-mixer.conf
    # edit: server_name, ssl_certificate(_key) (e.g. certbot), root /srv/audio-mixer -> your checkout
    sudo nginx -t && sudo nginx -s reload
    cd bridge && BRIDGE_HOSTS=mixer.example.com BRIDGE_ORIGINS=https://mixer.example.com node server.js

`BRIDGE_HOSTS` / `BRIDGE_ORIGINS` are needed on whichever machine runs the bridge behind nginx, because nginx
forwards the public `Host` and the page's `Origin`; the bridge otherwise only accepts localhost.

## Basic auth for the bridge
`/api/` and `/ws/output` require a username and password **in addition to** the IP rule (`satisfy all`).
Create the user file (nginx supports `openssl passwd -apr1`, or bcrypt via `htpasswd -B` from apache2-utils):

    sudo sh -c 'echo "mixer:$(openssl passwd -apr1)" > /etc/nginx/.htpasswd_mixer'   # asks for the password
    sudo chmod 640 /etc/nginx/.htpasswd_mixer && sudo chgrp www-data /etc/nginx/.htpasswd_mixer
    sudo nginx -t && sudo nginx -s reload

Add more users with `>>`. Credentials are not forwarded to the bridge, and repeated attempts hit the rate limit.
Over plain HTTP basic auth is readable on the wire, so keep it behind the HTTPS server block.
The first bridge request makes the browser ask for the login; if your browser does not reuse it for the
WebSocket, open `https://your-host/api/status` once in the same tab and sign in, then reload the mixer.

## Docker (Linux)
    docker compose -f deploy/nginx/docker-compose.yml up -d
The compose file uses host networking so nginx can reach the bridge on `127.0.0.1:8765`.
Put the auth file at `deploy/nginx/.htpasswd_mixer` (`echo "mixer:$(openssl passwd -apr1)" > deploy/nginx/.htpasswd_mixer`), mount your certificates at `./certs` (`fullchain.pem`, `privkey.pem`) and adjust `server_name`.

## How the page finds the bridge
The page (`connectBridge()`) tries `location.origin` only when the hostname is `localhost` / `127.0.0.1`, then
`http://localhost:8765`. So:
- **Public domain:** nginx serves the page; each visitor's browser looks for a bridge on *their own* PC
  (`http://localhost:8765`). That PC must start its bridge with `BRIDGE_ORIGINS=https://mixer.example.com`.
  The `/api` and `/ws` proxy blocks are not used by remote visitors (and stay refused for them).
- **Same machine (`https://localhost`, `server_name localhost`):** the page talks to the bridge through nginx, so
  `/api/drivers` and `/ws/output` go through the proxy (rate limited, TLS, WebSocket upgrade).

## Important: who can use the bridge
The bridge reports and drives the audio hardware of the machine it runs on. The config therefore only lets
`127.0.0.1` / `::1` reach `/api/` and `/ws/output` (add your LAN in the `allow` lines if you trust it). Anyone else
can load the page but gets 403 from those two paths and the page falls back to plain Web Audio, as without a bridge.
For a remote bridge, put authentication (basic auth / mTLS / VPN) in front first.

## Verify
    sudo nginx -t
    curl -sI https://mixer.example.com/ | grep -i -E "strict|x-frame|nosniff"
    curl -s -o /dev/null -w "%{http_code}\n" https://mixer.example.com/bridge/server.js   # 404
    node bridge/audit.js --url http://127.0.0.1:8765

## Tested
Validated with nginx 1.24 (`nginx -t`) against the real bridge: page 200 with HSTS / nosniff / X-Frame-Options /
Permissions-Policy, gzip, HTTP→HTTPS redirect, 404 for `/bridge/*`, `/.git/*`, `/.github/*`, `*.md`, `*.json`,
403 for POST and for foreign origins, basic auth on `/api` and `/ws/output` (401 without / with wrong credentials, 200 and WebSocket 101 with the right ones; the page itself stays public), `/api` rate limited (503 beyond the burst), WebSocket upgrade 101.
