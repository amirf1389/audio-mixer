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
location = /redeem          { proxy_pass http://127.0.0.1:8790; }
location = /status          { proxy_pass http://127.0.0.1:8790; }
location = /activate        { proxy_pass http://127.0.0.1:8790; }
location = /deactivate      { proxy_pass http://127.0.0.1:8790; }
location /portal/           { proxy_pass http://127.0.0.1:8790; }
location /admin             { return 404; }   # management stays off the public site
```

Use the port your server listens on in place of 8790.
