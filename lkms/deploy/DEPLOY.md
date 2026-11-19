# Deploying LKMS

Two things can be deployed. Most people only need the first.

| What | Who runs it | Where |
|---|---|---|
| **Central server** (database, API, admin console) | You, the vendor | One internet server with a domain name |
| **Site host** (volume activation inside a customer's network) | A customer with a volume licence | A machine on their own network |

The Docker files below were written carefully but could not be run in the environment that built this project.
Expect to fix small things the first time, and test with a throw-away key before you issue real ones.

## 1. Central server

You need: a Linux server with Docker, a domain name pointing at it (for example `licence.example.com`), and ports 80 and 443 open.

1. **Copy this whole `lkms` folder to the server.**
2. **Make the secrets on your own computer**, not on the server:
   `pip install cryptography && python -m lkms.keygen`
   It prints three lines for the server, your admin key, and the public key for your apps.
   Put the admin key in a password manager. It is not stored anywhere else.
3. **Write `deploy/.env`** from `deploy/.env.example`: the domain, your own address in `LKMS_ADMIN_ALLOW`, a database
   password, and the three secret lines. Then `chmod 600 deploy/.env`.
4. **Start it:** `cd deploy && docker compose up -d`
5. **Check it:** `curl https://licence.example.com/healthz` answers `{"ok":true}`.
   From an address that is not in `LKMS_ADMIN_ALLOW`, `https://licence.example.com/manage` must answer 403.
6. **Open the console** from an allowed address: `https://licence.example.com/manage`, sign in with the admin key.
   Register your first app and save its `app_id` and `api_secret`.
7. **Schedule the backup:** `0 3 * * * /path/to/lkms/deploy/backup.sh /var/backups/lkms` and copy that folder off the server.

Without Docker, use `deploy/lkms.service` with your own PostgreSQL and reverse proxy; `psql "$LKMS_DATABASE_URL" -f schema.sql` creates the tables.

## 2. Put it into an app

Give the app four values: the server address, `app_id`, `api_secret`, and the public key from step 2.
With `sdk/lkms_client.py`:

```python
lic = LicenseClient(base_url="https://licence.example.com", app_id=APP_ID, app_secret=APP_SECRET,
                    public_key=PUBLIC_KEY, cache_path="~/.myapp/licence.json")
result = lic.activate(key_the_customer_typed)   # once
result = lic.check()                            # at every start
```

## 3. Site host for a volume customer

1. In the console, create a key for the app with **Site seats** filled in (for example 50). That is a volume licence.
2. Send the customer the `host` and `deploy/site-host` folders and five values: your server address, `app_id`,
   `api_secret`, the volume key, and your public key.
3. They write `deploy/site-host/.env` and run `docker compose up -d` on a machine inside their network.
4. Their computers use `http://that-machine:8090` as the licence address and the word `SITE` as the key.
5. They can check the host with: `curl -H "X-App-Secret: ..." http://that-machine:8090/status`

How it behaves:

| Situation | What happens |
|---|---|
| The site's internet is down | The host keeps activating for up to 30 days (`LKMS_GRANT_HOURS`). |
| A laptop leaves the site | It runs for 7 days from its saved licence, then must see the host again. |
| A computer is retired without deactivating | Its seat is freed after 30 days unseen. |
| You revoke the volume licence | At its next renewal (within 6 hours) the host stops and tells the computers why. |
| Someone copies the host to another machine | The copy is refused: a volume licence allows one host unless you raise "Devices per key". |

The link between the site's computers and the host is plain HTTP inside their network. Leases are signed, so they
cannot be forged or changed on the way, but the app secret travels in the clear there. Put the host behind HTTPS
if their network is not trusted.

## After deployment

- Keep `/admin` and `/manage` limited to your addresses. Review `LKMS_ADMIN_ALLOW` when your address changes.
- Watch the Activity tab for bursts of refusals.
- Update with `git pull && docker compose up -d --build`. The schema file is safe to apply again.
- Test a restore of a backup before you depend on it.
