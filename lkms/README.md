# LKMS: private licence key management

One private database and one REST API that all your apps ask about licences: create and revoke keys, tie a key to
an app, limit the number of devices, expire keys, and see which devices are activated.

- **Stack:** Python 3.11+, FastAPI, PostgreSQL 14+.
- **Status:** built and tested here (22 tests against a real PostgreSQL; the admin console was driven in a real browser). Not yet deployed anywhere, and AmF X1
  does not use it yet (see "AmF X1" at the end).

## Layout

| Path | What it is |
|---|---|
| `schema.sql` | Tables, constraints and indexes |
| `lkms/config.py` | Settings from the environment; refuses to start with a missing secret |
| `lkms/security.py` | Key generation, hashes, signed leases |
| `lkms/service.py` | The licence rules (activate, validate, deactivate, revoke) |
| `lkms/main.py` | The HTTP API, authentication, rate limits |
| `lkms/keygen.py` | Makes the secrets on your own machine |
| `lkms/ui/` | The admin console (one page, no outside files) |
| `sdk/lkms_client.py` | One-file helper for your apps, with offline grace period |
| `tests/` | End-to-end tests; they start their own throw-away PostgreSQL |

## Run it

```sh
pip install -r requirements.txt
python -m lkms.keygen                 # prints the secrets; put them in .env (see .env.example)
psql "$LKMS_DATABASE_URL" -f schema.sql
uvicorn --factory lkms.main:app --host 127.0.0.1 --port 8088
python -m pytest -q                   # the tests
```

Or with Docker: write `.env` and `db_password.txt`, then `docker compose up -d`. The compose file is not tested here.

## API

**Admin** (header `Authorization: Bearer <admin key>`)

| Call | Does |
|---|---|
| `POST /admin/apps` `{name}` | Registers an app. Returns `app_id` and `api_secret` (shown once). |
| `POST /admin/licenses` `{app_id, count, plan, max_activations, expires_at, customer_email, metadata}` | Makes 1 to 1000 keys. The keys are shown once. |
| `PATCH /admin/licenses/{key}/revoke` `{action: revoke \| suspend \| reinstate, reason}` | Revoke is final and frees every seat. Suspend keeps the seats. |
| `GET /admin/licenses?app_id=&limit=&offset=` | Lists licences with status and seats used. |

**Apps** (headers `X-App-Id`, `X-App-Secret`; body `{license_key, device_id, timestamp, nonce}`)

| Call | Does |
|---|---|
| `POST /v1/activate` | Gives this device a seat, up to `max_activations`. The same device keeps its one seat. |
| `POST /v1/validate` | Confirms the key is active, belongs to this app, has not expired, and this device has a seat. |
| `POST /v1/deactivate` | Hands the device's seat back. |

Activate and validate answer with a **signed lease** the app keeps for the offline grace period.

**Management** (admin key; this is what the admin console uses)

| Call | Does |
|---|---|
| `GET /admin/overview` | Counts by status, devices, licences expiring in 30 days, 7 days of activity. |
| `GET /admin/apps`, `PATCH /admin/apps/{app_id}`, `POST /admin/apps/{app_id}/rotate-secret` | List apps, rename, disable or enable, make a new secret. |
| `GET /admin/search/licenses?app_id=&status=&q=` | Find licences by app, status, a whole key, the start of a key, e-mail or plan. |
| `GET /admin/license/{id}`, `PATCH /admin/license/{id}` | One licence with its devices and history; change plan, seats, expiry, e-mail or status. |
| `DELETE /admin/activations/{id}` | Remove one device and free its seat. |
| `GET /admin/audit` | What happened, newest first. |

## Admin console

A web page for the daily work: an overview, creating keys (copy or CSV), finding and editing licences, removing
devices, suspending and revoking, registering apps, and the activity log.

- It is **off by default**. Set `LKMS_ADMIN_UI=1` and open `https://your-server/manage`.
- Sign in with the admin key. The key stays in that browser tab and is forgotten when the tab closes; no cookie is set.
- The page loads no outside files and its security policy forbids inline code and framing.
- **Do not expose it to the public.** Limit `/manage` and `/admin/*` in your reverse proxy to your own addresses or a VPN.

## How it is protected

- **Keys:** 125 random bits from the system's secure source, in a 25-character form without look-alike letters.
- **Nothing secret in the database:** app secrets as SHA-256, licence keys as HMAC-SHA256 with a server pepper,
  device ids as SHA-256. A copy of the database does not reveal a key or a secret.
- **Signed leases (Ed25519):** an app believes an answer only if your server signed it for that app, that device
  and that request. An edited cache file or a fake server is refused.
- **Replay:** each request carries a timestamp (5 minutes tolerance) and a nonce that is accepted once.
- **Guessing:** 20 wrong keys or secrets from one address in 15 minutes and the address is refused; 300 requests
  a minute per address at most.
- **No leaks between apps:** a key of another app looks exactly like a key that does not exist.
- **Race for the last seat:** the licence row is locked, so two devices cannot both take it (tested with 12 at once).

## Deployment and security checklist

**Database**
- [ ] Keep PostgreSQL on a private network. Never publish port 5432 to the internet.
- [ ] Give the API its own database user with rights on these tables only; not the superuser.
- [ ] Require TLS between API and database if they are on different machines.
- [ ] Turn on disk encryption and automatic encrypted backups; test a restore.
- [ ] Back up `LKMS_KEY_PEPPER` and `LKMS_SIGNING_KEY` separately from the database. Without the pepper no issued
      key can be recognised again; without the signing key every app needs a new public key.

**Secrets**
- [ ] Make them with `python -m lkms.keygen` on your own machine. Keep the admin key in a password manager only.
- [ ] Put `.env` on the server with file mode 600, or use the host's secret store. Never commit it.
- [ ] Rotate the admin key if anyone who knew it leaves: make a new one, replace `LKMS_ADMIN_KEY_HASH`, restart.

**Network**
- [ ] Serve only over HTTPS, behind a reverse proxy (Caddy or nginx) that ends TLS. Run uvicorn on 127.0.0.1.
- [ ] Set `LKMS_TRUST_PROXY=1` only behind that proxy, so the client address is read correctly.
- [ ] Restrict `/admin/*` in the proxy to your own addresses or a VPN. The admin API should not be reachable by the public.
- [ ] Add a request-rate limit in the proxy as well. The built-in limiter is per process and forgets on restart.

**Operation**
- [ ] Run one API process, or move the limiter to Redis before running several.
- [ ] Watch `audit_events` for bursts of refusals, and alert on HTTP 429 and 5xx.
- [ ] Keep Python packages updated; pin versions for production builds.
- [ ] Decide how long to keep IP addresses in `activations` and `audit_events` (they are personal data).

## Limits to know

- **An app secret inside a shipped program can be extracted.** It identifies the app; it does not protect a licence.
  Protection comes from the key, the seat limit, the rate limits and the signed lease.
- **A device id is whatever the app sends.** Someone who copies a device id and a key to a second computer shares one seat.
- **Offline grace** is 7 days by default (`LKMS_LEASE_HOURS`). A revoked key keeps working on an offline device until
  its lease runs out; shorten the lease if that matters more than tolerance of outages.
- **The key in the revoke URL** can end up in proxy logs. Keep those logs private, or turn off path logging for `/admin`.

## AmF X1

AmF X1 still uses its own vendor-signed offline keys. Moving it to this service is the next stage and needs this
server running at an address first; until then no copy of AmF X1 could be licensed by it.
