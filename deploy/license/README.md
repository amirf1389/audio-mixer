# AmF X1 licence server and licence management

`bridge/license-server/server.js` issues signed licence keys, keeps the list of computers each key runs on, and has a management page.
Plain Node.js (18+), no packages: it is built on `bridge/license-server/framework.js`, the small in-house backend framework
(routes, JSON body limit, security headers, bearer token, rate limiter, fixed static files).

## Run it

    export LICENSE_ADMIN_TOKEN="$(openssl rand -hex 32)"          # the management password: keep it
    export LICENSE_PRIVATE_KEY_FILE=/secure/vendor-private.pem     # read from a file; never in the repository, the image or a variable
    export LICENSE_DATA_FILE=/var/lib/amfx1-license/licenses.json
    node bridge/license-server/server.js                           # listens on 127.0.0.1:8787

Or `docker compose -f deploy/license/docker-compose.yml up -d` (the port is published on 127.0.0.1 only).

## Management page

Open `http://127.0.0.1:8787/admin/` **on the server itself or through an SSH tunnel** (`ssh -L 8787:127.0.0.1:8787 you@server`) and type the
admin token. The page is deliberately not routed by the public site's nginx configuration.

- **Issue a key**: customer, plan, days, computers, optional machine lock. The key is shown once and is not stored on the server.
- **Keys on record**: search, filter by state, export CSV, **revoke** / **restore**, remove one computer from a key.
- **Activity**: every action with time and address, hash-chained so a removed or changed line shows.

## What is stored

| Stored | How |
| --- | --- |
| Key id, plan, issue / expiry dates, seats, machine lock, computers, revoked state | plain JSON in `LICENSE_DATA_FILE` (mode 600) |
| Customer name, e-mail, note | AES-256-GCM, key derived with scrypt from `LICENSE_DATA_KEY_FILE` (or the admin token when unset) |
| The licence key itself, the admin token, the vendor private key | never |

Changing the admin token without a separate `LICENSE_DATA_KEY_FILE` makes the stored names unreadable (the records stay usable): set
`LICENSE_DATA_KEY_FILE` to a file of 32+ random characters before going live and back it up.

## API

| Call | Who | What |
| --- | --- | --- |
| `POST /activate`, `/deactivate`, `/status` `{key, machine}` | the app (the key is the proof) | seat lock; a revoked key is refused |
| `POST /issue`, `POST /admin/licenses` | admin token | issue a key |
| `GET /admin/licenses`, `/admin/stats`, `/admin/audit`, `/admin/audit/verify` | admin token | read |
| `POST /admin/licenses/:id/revoke`, `/restore`, `DELETE /admin/licenses/:id/devices/:machine` | admin token | change |

Limits per address and minute: 30 public calls, 120 admin calls, 10 wrong tokens. Behind nginx / Caddy on the same host set `LICENSE_TRUST_PROXY=1`.

Revocation reaches a computer when it next activates or asks `/status`; a key already activated keeps verifying offline, because the
signature check needs no server. Tests: `cd bridge && node --test license-server/test.js`; every button of the page in a real browser:
`node scripts/check-licence-dashboard.js`.

## The customer portal (`/portal/`)

The licence server also serves a small public page for customers at `/portal/`:

- **Activate**: the customer types the 25-character product key and gets the full licence key, to copy or to save as `AmF-X1.amixkey`.
- **Check a key**: shows the edition, end date and state of a full key.
- **Updates**: shows the newest release from the published update list, with each file's SHA-256.

It uses only the public routes (`/redeem`, `/status`, `/portal/updates`) and carries no admin data. `/admin/` stays where it was: reach it
on the server itself or through an SSH tunnel, and do not publish it.

Set `LICENSE_UPDATE_URL` to the https address of your update list (default: the list in this repository). The server reads it for the page
and keeps it for five minutes; the page does not verify the list's signature, the console does that before it installs anything.

In nginx, publish only what customers need:

```nginx
location = /redeem          { proxy_pass http://127.0.0.1:8787; }
location = /status          { proxy_pass http://127.0.0.1:8787; }
location = /activate        { proxy_pass http://127.0.0.1:8787; }
location = /deactivate      { proxy_pass http://127.0.0.1:8787; }
location /portal/           { proxy_pass http://127.0.0.1:8787; }
location /admin             { return 404; }   # management stays off the public site
```

A complete, hardened file is `deploy/license/nginx-license.conf` (TLS, limits, security headers, POST-only calls); use that one.

## Portal management (in `/admin/`)

The management page has a **Customer portal** card:

- how many product keys are out, how many were used, how many never;
- activations and refused tries in the last 24 hours;
- what the portal's Updates tab is showing (version, signed or not, number of files), with **Read the update list again** after you
  publish a release.

Each licence row shows whether its product key was used and when, and has **New product key**: it makes a fresh one and the old one stops
working at once (for a customer who lost it, or when it was passed on).

## Going online through nginx (`deploy/license/nginx-license.conf`)

This is how the AmF X1 app reaches the licence server over the internet:

    AmF X1 app / portal  --https-->  nginx  --http on 127.0.0.1-->  licence server

1. Run the licence server on the same machine, bound to this computer only, and tell it that it is behind a proxy:

       LICENSE_HOST=127.0.0.1 LICENSE_PORT=8787 LICENSE_TRUST_PROXY=1 node bridge/license-server/server.js

2. Copy `deploy/license/nginx-license.conf` to `/etc/nginx/conf.d/`, replace `licence.amirfakhribadri.my` and the two certificate paths
   (for example from Let's Encrypt), run `nginx -t`, then reload nginx.
3. In AmF X1, on LICENCE, type `https://licence.amirfakhribadri.my` into LICENCE SERVER. Customers use `https://licence.amirfakhribadri.my/portal/`.

What the file lets through, and nothing else:

| Open | For |
|---|---|
| `POST /redeem`, `/status`, `/activate`, `/deactivate` | the app |
| `GET /portal/…` | the customer page |
| `GET /health` | monitoring |

`/admin`, `/issue` and every other path answer 404 from nginx, with or without the admin token. Management is done on the server itself
or through an SSH tunnel.

It was run against the real server with a test certificate, and these held: a product key redeems over https and the key is genuine;
`/admin` and `/issue` are not served even with the token; GET on a call and POST on the portal are refused; a body over 8 kB is
refused; plain http is redirected; TLS 1.1 is refused; the security headers are set; a caller cannot fake its address through
`X-Forwarded-For`; the portal works in a browser under the Content-Security-Policy; a flood of calls gets 429.

Keep in mind: the firewall should allow 80 and 443 only; port 8787 must not be reachable from outside; keep the private key file and
the admin token readable by the service user only; renew the certificate before it runs out.

## The domain: amirfakhribadri.my

| Name | Points at | Serves |
|---|---|---|
| `licence.amirfakhribadri.my` | the machine that runs the licence server and nginx | the app's licence calls and `/portal/` |
| `ota.amirfakhribadri.my` | the machine that runs the OTA server (`deploy/ota`) | update lists and downloads |
| `amirfakhribadri.my` | your website, as it is | not touched by these files |

Make an `A` record (and `AAAA` if the machine has IPv6) for `licence` at your DNS provider, then get the certificate, for example:

    sudo certbot certonly --nginx -d licence.amirfakhribadri.my

The paths in `nginx-license.conf` already match what certbot writes. AmF X1 uses `https://licence.amirfakhribadri.my` as its licence
server unless another address is typed on the LICENCE page.
