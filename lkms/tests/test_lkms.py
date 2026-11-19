"""End-to-end tests against a real PostgreSQL: the admin API, the client API, the limits, and the client helper."""
from __future__ import annotations

import json
import secrets
import threading
import time
from datetime import datetime, timedelta, timezone

import psycopg

from lkms import security


def _app(client, admin, name="Mixer"):
    r = client.post("/admin/apps", json={"name": name}, headers=admin)
    assert r.status_code == 201, r.text
    a = r.json()
    return a, {"X-App-Id": a["app_id"], "X-App-Secret": a["api_secret"]}


def _keys(client, admin, app_id, **kw):
    r = client.post("/admin/licenses", json={"app_id": app_id, **kw}, headers=admin)
    assert r.status_code == 201, r.text
    return [x["license_key"] for x in r.json()["licenses"]]


def _body(key, device="device-0001", **kw):
    return {"license_key": key, "device_id": device, "timestamp": int(time.time()), "nonce": secrets.token_urlsafe(24), **kw}


def test_admin_needs_the_master_key(env):
    client, admin, _ = env
    assert client.post("/admin/apps", json={"name": "X"}).status_code == 401
    assert client.post("/admin/apps", json={"name": "X"}, headers={"Authorization": "Bearer wrong"}).status_code == 401
    assert client.get("/admin/licenses", headers={"Authorization": "Bearer "}).status_code == 401
    a, _h = _app(client, admin)
    assert client.post("/admin/apps", json={"name": "Mixer"}, headers=admin).status_code == 409        # the name is taken


def test_nothing_secret_is_stored_in_the_clear(env):
    client, admin, settings = env
    a, _h = _app(client, admin)
    key = _keys(client, admin, a["app_id"])[0]
    with psycopg.connect(settings.database_url) as c:
        dump = json.dumps([list(map(str, r)) for t in ("apps", "licenses", "audit_events") for r in c.execute(f"SELECT * FROM {t}").fetchall()])
    assert a["api_secret"] not in dump and key not in dump and key.replace("-", "") not in dump
    assert security.sha256_hex(a["api_secret"]) in dump and security.key_hash(settings.key_pepper, key) in dump


def test_keys_are_random_well_formed_and_forgiving_to_type(env):
    client, admin, _ = env
    a, h = _app(client, admin)
    keys = _keys(client, admin, a["app_id"], count=200)
    assert len(set(keys)) == 200 and all(len(k) == 29 and k.count("-") == 4 for k in keys)
    assert not any(ch in "ILOU" for k in keys for ch in k)
    typed = " " + keys[0].lower().replace("-", " ") + " "                                              # lower case, spaces for dashes
    assert client.post("/v1/activate", json=_body(typed), headers=h).status_code == 200
    assert client.post("/admin/licenses", json={"app_id": a["app_id"], "count": 1001}, headers=admin).status_code == 422
    assert client.post("/admin/licenses", json={"app_id": a["app_id"], "count": 0}, headers=admin).status_code == 422


def test_activate_validate_deactivate_and_the_seat_limit(env):
    client, admin, settings = env
    a, h = _app(client, admin)
    key = _keys(client, admin, a["app_id"], max_activations=2, plan="pro", metadata={"channels": 30})[0]
    r = client.post("/v1/activate", json=_body(key, "device-A", device_name="Studio PC"), headers=h).json()
    assert r["valid"] and r["plan"] == "pro" and r["activations_used"] == 1 and r["metadata"] == {"channels": 30}
    lease = security.verify_lease(security.public_key_b64(settings.signing_key), r["lease"])
    assert lease and lease["app"] == a["app_id"] and lease["plan"] == "pro" and lease["dev"] == security.sha256_hex("device-A")
    again = client.post("/v1/activate", json=_body(key, "device-A"), headers=h).json()
    assert again["already_activated"] and again["activations_used"] == 1                              # the same device keeps one seat
    assert client.post("/v1/activate", json=_body(key, "device-B"), headers=h).json()["activations_used"] == 2
    full = client.post("/v1/activate", json=_body(key, "device-C"), headers=h)
    assert full.status_code == 409 and full.json() == {"valid": False, "error": "activation_limit_reached", "max_activations": 2, "used": 2}
    assert client.post("/v1/validate", json=_body(key, "device-A"), headers=h).json()["valid"]
    v = client.post("/v1/validate", json=_body(key, "device-C"), headers=h)
    assert v.status_code == 403 and v.json()["error"] == "device_not_activated"
    assert client.post("/v1/deactivate", json=_body(key, "device-A"), headers=h).json() == {"deactivated": True}
    assert client.post("/v1/deactivate", json=_body(key, "device-A"), headers=h).json() == {"deactivated": False}
    assert client.post("/v1/activate", json=_body(key, "device-C"), headers=h).status_code == 200      # the freed seat is taken


def test_a_key_only_works_for_its_own_app(env):
    client, admin, _ = env
    a, ha = _app(client, admin, "One")
    b, hb = _app(client, admin, "Two")
    key = _keys(client, admin, a["app_id"])[0]
    other = client.post("/v1/activate", json=_body(key), headers=hb)
    unknown = client.post("/v1/activate", json=_body("00000-00000-00000-00000-00000"), headers=hb)
    assert other.status_code == unknown.status_code == 404 and other.json() == unknown.json()          # app Two cannot tell the key exists
    assert client.post("/v1/activate", json=_body(key), headers={**ha, "X-App-Secret": hb["X-App-Secret"]}).status_code == 401
    assert client.post("/v1/activate", json=_body(key), headers={"X-App-Id": "not-a-uuid", "X-App-Secret": "x"}).status_code == 401
    assert client.post("/v1/activate", json=_body(key)).status_code == 401


def test_revoke_suspend_reinstate_and_expiry(env):
    client, admin, settings = env
    a, h = _app(client, admin)
    k1, k2, k3 = _keys(client, admin, a["app_id"], count=3)
    for k in (k1, k2, k3):
        assert client.post("/v1/activate", json=_body(k), headers=h).status_code == 200
    s = client.patch(f"/admin/licenses/{k1}/revoke", json={"action": "suspend", "reason": "payment"}, headers=admin).json()
    assert s["status"] == "suspended"
    assert client.post("/v1/validate", json=_body(k1), headers=h).json()["error"] == "license_suspended"
    assert client.patch(f"/admin/licenses/{k1}/revoke", json={"action": "reinstate"}, headers=admin).json()["status"] == "active"
    assert client.post("/v1/validate", json=_body(k1), headers=h).json()["valid"]                      # the seat was kept
    r = client.patch(f"/admin/licenses/{k2}/revoke", headers=admin).json()
    assert r["status"] == "revoked" and r["seats_freed"] == 1
    assert client.post("/v1/validate", json=_body(k2), headers=h).json()["error"] == "license_revoked"
    assert client.post("/v1/activate", json=_body(k2), headers=h).json()["error"] == "license_revoked"
    assert client.patch(f"/admin/licenses/{k2}/revoke", json={"action": "reinstate"}, headers=admin).status_code == 409
    assert client.patch("/admin/licenses/AAAAA-AAAAA-AAAAA-AAAAA-AAAAA/revoke", headers=admin).status_code == 404
    assert client.patch(f"/admin/licenses/{k3}/revoke").status_code == 401
    # expiry: a date in the past is refused when a key is made; a key whose date has passed stops working
    past = (datetime.now(timezone.utc) - timedelta(days=1)).isoformat()
    assert client.post("/admin/licenses", json={"app_id": a["app_id"], "expires_at": past}, headers=admin).status_code == 422
    with psycopg.connect(settings.database_url, autocommit=True) as c:
        c.execute("UPDATE licenses SET expires_at = now() - interval '1 minute' WHERE key_hash = %s", (security.key_hash(settings.key_pepper, k3),))
    assert client.post("/v1/validate", json=_body(k3), headers=h).json()["error"] == "license_expired"
    listed = {x["key_hint"]: x["status"] for x in client.get("/admin/licenses", headers=admin).json()["licenses"]}
    assert listed[k3[:5] + "-…"] == "expired" and listed[k2[:5] + "-…"] == "revoked" and listed[k1[:5] + "-…"] == "active"


def test_a_lease_never_outlives_the_licence(env):
    client, admin, settings = env
    a, h = _app(client, admin)
    soon = datetime.now(timezone.utc) + timedelta(hours=2)
    key = _keys(client, admin, a["app_id"], expires_at=soon.isoformat())[0]
    r = client.post("/v1/activate", json=_body(key), headers=h).json()
    lease = security.verify_lease(security.public_key_b64(settings.signing_key), r["lease"])
    assert abs(lease["exp"] - soon.timestamp()) < 2 and lease["exp"] < time.time() + 3 * 3600
    assert security.verify_lease(security.public_key_b64(settings.signing_key), r["lease"], now=soon.timestamp() + 5) is None
    body, sig = r["lease"].split(".")
    assert security.verify_lease(security.public_key_b64(settings.signing_key), body[:-2] + "AA." + sig) is None       # edited: refused
    assert security.verify_lease(security.public_key_b64(secrets.token_bytes(32)), r["lease"]) is None                 # another server's key: refused


def test_replayed_and_stale_requests_are_refused(env):
    client, admin, _ = env
    a, h = _app(client, admin)
    key = _keys(client, admin, a["app_id"])[0]
    body = _body(key)
    assert client.post("/v1/activate", json=body, headers=h).status_code == 200
    again = client.post("/v1/activate", json=body, headers=h)
    assert again.status_code == 409 and again.json()["error"] == "replayed_request"
    old = _body(key); old["timestamp"] -= 3600
    assert client.post("/v1/validate", json=old, headers=h).json()["error"] == "stale_request"
    short = _body(key); short["nonce"] = "abc"
    assert client.post("/v1/validate", json=short, headers=h).status_code == 422


def test_the_last_seat_goes_to_exactly_one_of_many_racing_devices(env):
    client, admin, settings = env
    a, h = _app(client, admin)
    key = _keys(client, admin, a["app_id"], max_activations=3)[0]
    codes: list[int] = []
    def go(n: int) -> None:
        codes.append(client.post("/v1/activate", json=_body(key, f"racing-device-{n}"), headers=h).status_code)
    threads = [threading.Thread(target=go, args=(n,)) for n in range(12)]
    [t.start() for t in threads]; [t.join() for t in threads]
    assert sorted(codes) == [200] * 3 + [409] * 9
    with psycopg.connect(settings.database_url) as c:
        assert c.execute("SELECT count(*) FROM activations").fetchone()[0] == 3


def test_guessing_keys_is_stopped(env):
    client, admin, _ = env
    a, h = _app(client, admin)
    key = _keys(client, admin, a["app_id"])[0]
    codes = [client.post("/v1/activate", json=_body(security.new_license_key()), headers=h).status_code for _ in range(25)]
    assert codes[:20] == [404] * 20 and codes[20:] == [429] * 5
    assert client.post("/v1/activate", json=_body(key), headers=h).status_code == 429                 # the address is refused for a while, even with a good key
    client.app.state.limiter.reset()
    assert client.post("/v1/activate", json=_body(key), headers=h).status_code == 200


def test_a_disabled_app_is_refused_and_bad_input_is_rejected(env):
    client, admin, settings = env
    a, h = _app(client, admin)
    key = _keys(client, admin, a["app_id"])[0]
    assert client.post("/v1/activate", json={"license_key": key}, headers=h).status_code == 422
    assert client.post("/v1/activate", json=_body(key, "short"), headers=h).status_code == 422
    assert client.post("/admin/licenses", json={"app_id": a["app_id"], "customer_email": "not-an-email"}, headers=admin).status_code == 422
    assert client.post("/admin/licenses", json={"app_id": "11111111-1111-1111-1111-111111111111"}, headers=admin).status_code == 404
    assert client.get("/docs").status_code == 404 and client.get("/openapi.json").status_code == 404
    assert client.get("/healthz").json() == {"ok": True}
    with psycopg.connect(settings.database_url, autocommit=True) as c:
        c.execute("UPDATE apps SET status = 'disabled'")
    assert client.post("/v1/validate", json=_body(key), headers=h).status_code == 401


def test_the_audit_log_says_what_happened(env):
    client, admin, settings = env
    a, h = _app(client, admin)
    key = _keys(client, admin, a["app_id"])[0]
    client.post("/v1/activate", json=_body(key), headers=h)
    client.post("/v1/deactivate", json=_body(key), headers=h)
    client.patch(f"/admin/licenses/{key}/revoke", json={"reason": "refund"}, headers=admin)
    with psycopg.connect(settings.database_url) as c:
        actions = [r[0] for r in c.execute("SELECT action FROM audit_events ORDER BY event_id").fetchall()]
    assert actions == ["app.create", "license.create", "activate", "deactivate", "license.revoke"]
