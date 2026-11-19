"""The management API behind the admin console, and the console's own files."""
from __future__ import annotations

import dataclasses
import secrets
import time
from datetime import datetime, timedelta, timezone

from fastapi.testclient import TestClient

from lkms.main import create_app


def _setup(client, admin, seats=2):
    a = client.post("/admin/apps", json={"name": "Desk"}, headers=admin).json()
    h = {"X-App-Id": a["app_id"], "X-App-Secret": a["api_secret"]}
    made = client.post("/admin/licenses", json={"app_id": a["app_id"], "count": 3, "plan": "pro", "max_activations": seats, "customer_email": "ana@example.com"}, headers=admin).json()["licenses"]
    return a, h, made


def _act(client, h, key, device):
    return client.post("/v1/activate", json={"license_key": key, "device_id": device, "device_name": "PC " + device[-1], "timestamp": int(time.time()), "nonce": secrets.token_urlsafe(24)}, headers=h)


def test_every_management_call_needs_the_admin_key(env):
    client, admin, _ = env
    x = "11111111-1111-1111-1111-111111111111"
    for method, path in [("GET", "/admin/overview"), ("GET", "/admin/apps"), ("PATCH", f"/admin/apps/{x}"), ("POST", f"/admin/apps/{x}/rotate-secret"), ("GET", "/admin/search/licenses"),
                         ("GET", f"/admin/license/{x}"), ("PATCH", f"/admin/license/{x}"), ("DELETE", f"/admin/activations/{x}"), ("GET", "/admin/audit")]:
        assert client.request(method, path, json={} if method in ("PATCH", "POST") else None).status_code == 401, path


def test_overview_apps_and_search(env):
    client, admin, _ = env
    a, h, made = _setup(client, admin)
    assert _act(client, h, made[0]["license_key"], "device-0001").status_code == 200
    client.patch(f"/admin/license/{made[1]['license_id']}", json={"action": "suspend"}, headers=admin)
    o = client.get("/admin/overview", headers=admin).json()
    assert o["apps"] == 1 and o["licenses"] == 3 and o["licenses_by_status"] == {"active": 2, "suspended": 1, "revoked": 0, "expired": 0}
    assert o["activations"] == 1 and o["devices_seen_24h"] == 1 and o["activity_7_days"][0]["activations"] == 1 and o["activity_7_days"][0]["issued"] == 1
    apps = client.get("/admin/apps", headers=admin).json()["apps"]
    assert apps[0]["name"] == "Desk" and apps[0]["licenses"] == 3 and apps[0]["activations"] == 1 and "api_secret" not in str(apps)
    find = lambda **p: client.get("/admin/search/licenses", params=p, headers=admin).json()  # noqa: E731
    assert find()["total"] == 3 and find(status="suspended")["total"] == 1 and find(app_id=a["app_id"], status="active")["total"] == 2
    whole = find(q=made[0]["license_key"].lower())
    assert whole["total"] == 1 and whole["licenses"][0]["license_id"] == made[0]["license_id"] and whole["licenses"][0]["activations"] == 1
    assert find(q="ANA@")["total"] == 3 and find(q="nobody")["total"] == 0 and find(q="100%_")["total"] == 0
    assert find(q=made[2]["license_key"][:5])["licenses"][0]["license_id"] in [m["license_id"] for m in made]
    assert find(limit=2)["total"] == 3 and len(find(limit=2)["licenses"]) == 2 and len(find(limit=2, offset=2)["licenses"]) == 1
    assert made[0]["license_key"] not in str(find())                                       # a list never shows a whole key
    assert client.get("/admin/search/licenses", params={"status": "nonsense"}, headers=admin).status_code == 422


def test_edit_a_licence_and_manage_its_devices(env):
    client, admin, _ = env
    a, h, made = _setup(client, admin, seats=2)
    lid, key = made[0]["license_id"], made[0]["license_key"]
    assert _act(client, h, key, "device-0001").status_code == 200 and _act(client, h, key, "device-0002").status_code == 200
    d = client.get(f"/admin/license/{lid}", headers=admin).json()
    assert d["plan"] == "pro" and len(d["activations"]) == 2 and d["activations"][0]["device_name"] == "PC 1" and [e["action"] for e in d["history"]] == ["activate", "activate"]
    low = client.patch(f"/admin/license/{lid}", json={"max_activations": 1}, headers=admin)
    assert low.status_code == 409 and low.json()["error"] == "seats_in_use" and low.json()["used"] == 2
    gone = client.delete(f"/admin/activations/{d['activations'][0]['activation_id']}", headers=admin).json()
    assert gone == {"removed": True, "license_id": lid}
    assert client.delete(f"/admin/activations/{d['activations'][0]['activation_id']}", headers=admin).status_code == 404
    when = (datetime.now(timezone.utc) + timedelta(days=30)).isoformat()
    d = client.patch(f"/admin/license/{lid}", json={"max_activations": 1, "plan": "studio", "expires_at": when, "customer_email": "bo@example.com"}, headers=admin).json()
    assert d["max_activations"] == 1 and d["plan"] == "studio" and d["customer_email"] == "bo@example.com" and d["expires_at"][:10] == when[:10] and len(d["activations"]) == 1
    assert _act(client, h, key, "device-0003").json()["error"] == "activation_limit_reached"
    assert client.patch(f"/admin/license/{lid}", json={"never_expires": True}, headers=admin).json()["expires_at"] is None
    assert client.patch(f"/admin/license/{lid}", json={"customer_email": "nope"}, headers=admin).status_code == 422
    assert client.patch(f"/admin/license/{lid}", json={"action": "revoke", "reason": "refund"}, headers=admin).json()["status"] == "revoked"
    assert client.patch(f"/admin/license/{lid}", json={"plan": "x"}, headers=admin).status_code == 409
    assert client.get("/admin/license/not-a-uuid", headers=admin).status_code == 404
    audit = [e["action"] for e in client.get("/admin/audit", headers=admin).json()["events"]]
    assert audit[:4] == ["license.revoke", "license.update", "license.update", "activation.remove"]


def test_a_new_date_brings_an_expired_licence_back(env):
    import psycopg
    client, admin, settings = env
    a, h, made = _setup(client, admin)
    lid, key = made[0]["license_id"], made[0]["license_key"]
    assert _act(client, h, key, "device-0001").status_code == 200
    with psycopg.connect(settings.database_url, autocommit=True) as c:
        c.execute("UPDATE licenses SET expires_at = now() - interval '1 hour' WHERE license_id = %s", (lid,))
    assert _act(client, h, key, "device-0001").json()["error"] == "license_expired"
    assert client.get(f"/admin/license/{lid}", headers=admin).json()["status"] == "expired"
    later = (datetime.now(timezone.utc) + timedelta(days=365)).isoformat()
    assert client.patch(f"/admin/license/{lid}", json={"expires_at": later}, headers=admin).json()["status"] == "active"
    assert _act(client, h, key, "device-0001").status_code == 200


def test_disable_an_app_and_change_its_secret(env):
    client, admin, _ = env
    a, h, made = _setup(client, admin)
    key = made[0]["license_key"]
    assert client.patch(f"/admin/apps/{a['app_id']}", json={"status": "disabled"}, headers=admin).json()["status"] == "disabled"
    assert _act(client, h, key, "device-0001").status_code == 401
    assert client.patch(f"/admin/apps/{a['app_id']}", json={"status": "active", "name": "Desk Pro"}, headers=admin).json() == {"app_id": a["app_id"], "name": "Desk Pro", "status": "active"}
    assert _act(client, h, key, "device-0001").status_code == 200
    new = client.post(f"/admin/apps/{a['app_id']}/rotate-secret", headers=admin).json()
    assert new["api_secret"] != a["api_secret"] and _act(client, h, key, "device-0001").status_code == 401            # the old secret stopped at once
    assert _act(client, {**h, "X-App-Secret": new["api_secret"]}, key, "device-0001").status_code == 200
    assert client.patch("/admin/apps/11111111-1111-1111-1111-111111111111", json={"status": "disabled"}, headers=admin).status_code == 404
    assert client.patch(f"/admin/apps/{a['app_id']}", json={"status": "gone"}, headers=admin).status_code == 422


def test_the_console_files_are_served_only_when_switched_on(env):
    client, admin, settings = env
    assert client.get("/manage").status_code == 404                                        # off unless LKMS_ADMIN_UI=1
    with TestClient(create_app(dataclasses.replace(settings, admin_ui=True))) as ui:
        page = ui.get("/manage")
        assert page.status_code == 200 and "Licence Manager" in page.text and "<script src=\"/manage/app.js\">" in page.text
        csp = page.headers["content-security-policy"]
        assert "default-src 'none'" in csp and "script-src 'self'" in csp and "frame-ancestors 'none'" in csp and "unsafe-inline" not in csp
        assert ui.get("/manage/app.js").headers["content-type"].startswith("text/javascript") and ui.get("/manage/style.css").status_code == 200
        assert ui.get("/manage/../config.py").status_code == 404 and ui.get("/manage/secret.txt").status_code == 404
        js = ui.get("/manage/app.js").text
        assert "innerHTML" not in js and "eval(" not in js and "localStorage" not in js      # built with textContent; the key is not kept on disk
        assert ui.get("/admin/overview").status_code == 401                                # the page gives no access by itself
