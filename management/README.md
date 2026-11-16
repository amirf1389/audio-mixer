# management/: licence keys and OTA updates, for the vendor and for customers

Two websites, each **one HTML file** (inline script and component styles, the Tailwind Play CDN pinned to `3.4.16` for the utility
classes). The Content-Security-Policy still lets no other script run: each server works out the SHA-256 of the page's inline script
(`inline-page.js`) and allows exactly that hash plus the pinned CDN address. Styles are inline (the Play CDN writes them at run time).

| Folder | What | Where it runs | Who |
| --- | --- | --- | --- |
| `vendor/` | **AmF X1 MANAGER**: overview, licence keys (issue, revoke / restore, free a computer, new product key), customers (CRM), releases (channels, history, files, verify), key requests from the app, activity of both servers | `node management/vendor/server.js` on the vendor's own computer, `http://127.0.0.1:8765/` only | staff |
| `customer/` | **MY ACCOUNT**: a licence key shows its plan, end date and computers; free a computer; trade a product key for the licence key; the downloads of the published version | the licence server serves it at `/account/` (nginx: `deploy/license/nginx-license.conf`) | customers |
| `windows/` | `AmF X1 Manager.cmd` (starts the manager and opens it; vendor only), `AmF X1 My Account.url` (ships with the Windows app), `AmF X1 Portal.url` | Windows | |

## The vendor manager

Nothing is stored by the manager. It signs in with a **staff account** of the licence server (`bridge/portal-auth`: scrypt passwords,
HS256 JWT for 8 hours) and that one token works on the licence server and on the OTA server (`PORTAL_DB` and `PORTAL_JWT_SECRET` set the
same on both). The token is kept in the tab (`sessionStorage`) and sent as `Authorization: Bearer`.

The manager is a small proxy plus the page:

- `/lic/admin/*`, `/lic/health` → `LICENSE_SERVER` (default `http://127.0.0.1:8765`)
- `/ota/admin/*`, `/ota/healthz` → `OTA_SERVER` (default `http://127.0.0.1:8765`)
- nothing else passes: the public routes of the servers are not reachable through it

It listens on `127.0.0.1` only, answers only its own `Host` (a made-up name pointed at 127.0.0.1 gets 421), and takes a change only from
its own page (`Origin`). For servers elsewhere, use an SSH tunnel to their 127.0.0.1 (`ssh -L 8787:127.0.0.1:8765 -L 8780:127.0.0.1:8765 you@server`):
the admin routes are not on the public nginx sites.

Release files are still signed on the vendor's computer and uploaded with `scripts/ota.js`; the manager shows and checks them. The
vendor private key never comes near either website.

## MY ACCOUNT

Public, GET only. Its calls are the licence server's public routes: `POST /status` (with `list: true` it also returns the machine codes on
the key, to whoever holds the signed key), `POST /deactivate`, `POST /redeem`, `GET /portal/updates`, `GET /portal/notice`. The key stays
in the browser (`localStorage` only when "remember" is ticked). In the console, **LICENSE → MY ACCOUNT ONLINE** opens it on the licence
server that is set there.

## Test

    node management/test.js           # a real licence server and OTA server on 127.0.0.1 with throw-away keys, the manager in front
    node management/test.js --demo    # the same, left running with sample data (prints the addresses and a demo sign-in)
