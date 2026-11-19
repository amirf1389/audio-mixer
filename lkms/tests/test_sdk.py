"""The client helper against the real service (over a real local HTTP port), including running from the cached lease."""
from __future__ import annotations

import json
import socket
import threading
import time

import pytest
import uvicorn

from lkms import security
from lkms_client import LicenseClient


@pytest.fixture()
def live(env, tmp_path):
    client, admin, settings = env
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0)); port = s.getsockname()[1]
    server = uvicorn.Server(uvicorn.Config(client.app, host="127.0.0.1", port=port, log_level="error", lifespan="off"))
    t = threading.Thread(target=server.run, daemon=True); t.start()
    for _ in range(100):
        if server.started: break
        time.sleep(0.05)
    a = client.post("/admin/apps", json={"name": "Desk"}, headers=admin).json()
    key = client.post("/admin/licenses", json={"app_id": a["app_id"], "plan": "pro"}, headers=admin).json()["licenses"][0]["license_key"]
    clock = {"t": None}
    def make(base=f"http://127.0.0.1:{port}", device="this-computer-0001", pub=None):
        return LicenseClient(base_url=base, app_id=a["app_id"], app_secret=a["api_secret"], public_key=pub or security.public_key_b64(settings.signing_key),
                             cache_path=str(tmp_path / "licence.json"), device_id=device, timeout=2, _now=lambda: clock["t"] or time.time())
    yield make, key, clock, client, admin, tmp_path, settings
    server.should_exit = True; t.join(5)


def test_activate_then_check_then_run_offline_from_the_lease(live):
    make, key, clock, client, admin, tmp, settings = live
    c = make()
    assert c.check().reason == "not_activated"
    r = c.activate(key)
    assert r.ok and r.plan == "pro" and r.source == "server"
    assert c.check().ok
    off = make(base="http://127.0.0.1:9")                       # nothing listens there: the server "cannot be reached"
    r = off.check()
    assert r.ok and r.source == "cache" and r.plan == "pro"
    clock["t"] = time.time() + 6 * 86400
    assert off.check().ok                                       # day 6 of 7: still inside the grace period
    clock["t"] = time.time() + 8 * 86400
    assert off.check().reason == "lease_expired"                # day 8: the grace period is over
    clock["t"] = time.time() - 86400
    assert off.check().reason == "clock_moved_back"             # the clock was set back to stretch the lease
    clock["t"] = None
    assert make().check().ok                                    # back online: a fresh lease


def test_a_refusal_from_the_server_beats_the_cache(live):
    make, key, clock, client, admin, tmp, settings = live
    c = make(); assert c.activate(key).ok
    client.patch(f"/admin/licenses/{key}/revoke", headers=admin)
    assert c.check().reason == "license_revoked"
    assert make(base="http://127.0.0.1:9").check().reason == "offline_no_lease"      # the lease was dropped with the refusal


def test_an_edited_cache_or_a_fake_server_is_not_believed(live):
    make, key, clock, client, admin, tmp, settings = live
    c = make(); assert c.activate(key).ok
    cache = tmp / "licence.json"; data = json.loads(cache.read_text())
    body, sig = data["lease"].split(".")
    forged = security.sign_lease(b"\x07" * 32, {"app": c.app_id, "dev": security.sha256_hex(c.device_id), "plan": "ultimate", "exp": time.time() + 10**9})
    for bad in (forged, body + "." + sig[::-1], "junk"):
        cache.write_text(json.dumps({**data, "lease": bad}))
        assert make(base="http://127.0.0.1:9").check().ok is False
    cache.write_text(json.dumps(data))
    assert make(base="http://127.0.0.1:9", device="another-computer-02").check().ok is False     # a lease copied to another computer
    assert make(pub=security.public_key_b64(b"\x09" * 32)).check().reason == "bad_server_signature"   # answers not signed by OUR server


def test_deactivate_hands_the_seat_back(live):
    make, key, clock, client, admin, tmp, settings = live
    a = make(device="computer-number-one"); assert a.activate(key).ok
    b = make(device="computer-number-two")
    assert b.activate(key).reason == "activation_limit_reached"
    a2 = make(device="computer-number-one"); assert a2.activate(key).ok
    assert a2.deactivate().ok and not (tmp / "licence.json").exists()
    assert b.activate(key).ok
