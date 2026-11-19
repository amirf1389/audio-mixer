"""The site host (volume licences): the central server certifies a host, the host activates the site's computers."""
from __future__ import annotations

import secrets
import socket
import sys
import threading
import time
from pathlib import Path

import pytest
import uvicorn

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "host"))
import sitehost  # noqa: E402
from lkms import security  # noqa: E402
from lkms_client import LicenseClient  # noqa: E402


def _serve(app):
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0)); port = s.getsockname()[1]
    server = uvicorn.Server(uvicorn.Config(app, host="127.0.0.1", port=port, log_level="error"))
    t = threading.Thread(target=server.run, daemon=True); t.start()
    for _ in range(200):
        if server.started: break
        time.sleep(0.02)
    return f"http://127.0.0.1:{port}", lambda: (setattr(server, "should_exit", True), t.join(5))


@pytest.fixture()
def site(env, tmp_path):
    client, admin, settings = env
    central, stop_central = _serve(client.app)
    a = client.post("/admin/apps", json={"name": "Desk"}, headers=admin).json()
    vol = client.post("/admin/licenses", json={"app_id": a["app_id"], "plan": "site", "volume_seats": 3}, headers=admin).json()["licenses"][0]
    clock = {"t": None}
    now = lambda: clock["t"] or time.time()  # noqa: E731
    hs = sitehost.HostSettings(central_url=central, app_id=a["app_id"], app_secret=a["api_secret"], license_key=vol["license_key"],
                               central_public_key=security.public_key_b64(settings.signing_key), data_dir=str(tmp_path / "host"))
    happ = sitehost.create_app(hs, background=False, now=now)
    host_url, stop_host = _serve(happ)
    def pc(n, base=None, pub=None):
        return LicenseClient(base_url=base or host_url, app_id=a["app_id"], app_secret=a["api_secret"], public_key=pub or security.public_key_b64(settings.signing_key),
                             cache_path=str(tmp_path / f"pc{n}.json"), device_id=f"site-computer-{n:04d}", timeout=3, _now=now)
    yield {"client": client, "admin": admin, "settings": settings, "a": a, "vol": vol, "host": happ.state.host, "host_url": host_url, "hs": hs, "pc": pc, "clock": clock, "tmp": tmp_path, "central": central}
    stop_host(); stop_central()


def test_a_volume_licence_is_made_and_cannot_be_used_on_a_device(site):
    c, admin, a, vol = site["client"], site["admin"], site["a"], site["vol"]
    assert vol["kind"] == "volume" and vol["volume_seats"] == 3
    h = {"X-App-Id": a["app_id"], "X-App-Secret": a["api_secret"]}
    body = lambda k, **kw: {"license_key": k, "device_id": "device-0001", "timestamp": int(time.time()), "nonce": secrets.token_urlsafe(24), **kw}  # noqa: E731
    assert c.post("/v1/activate", json=body(vol["license_key"]), headers=h).json()["error"] == "use_site_host"
    dev = c.post("/admin/licenses", json={"app_id": a["app_id"]}, headers=admin).json()["licenses"][0]
    assert dev["kind"] == "device" and dev["volume_seats"] is None
    no = c.post("/v1/host/grant", json=body(dev["license_key"], host_public_key=site["host"].public_key), headers=h)
    assert no.status_code == 409 and no.json()["error"] == "not_a_volume_license"
    assert c.post("/v1/host/grant", json=body(vol["license_key"], host_public_key="not-a-key-not-a-key-not-a-key-not-a-key-00"), headers=h).status_code == 400
    listed = c.get("/admin/search/licenses", params={"q": vol["license_key"]}, headers=admin).json()["licenses"][0]
    assert listed["kind"] == "volume" and listed["volume_seats"] == 3


def test_the_host_gets_a_grant_and_activates_the_sites_computers(site):
    host, pc = site["host"], site["pc"]
    assert pc(1).activate("SITE").reason == "offline_no_lease"            # before the host is licensed it answers 503: nothing to run from yet
    assert host.renew() == "ok"
    g = host.grant()
    assert g["seats"] == 3 and g["plan"] == "site" and g["hpk"] == host.public_key
    r = pc(1).activate("SITE")
    assert r.ok and r.plan == "site" and r.source == "server" and r.detail["activations_used"] == 1 and r.detail["max_activations"] == 3
    assert pc(1).activate("SITE").detail["already_activated"]
    assert pc(2).activate("SITE").ok and pc(3).activate("SITE").ok
    full = pc(4).activate("SITE")
    assert not full.ok and full.reason == "activation_limit_reached"
    assert pc(1).check().ok and pc(4).check("SITE").reason == "device_not_activated"
    assert pc(2).deactivate().ok and pc(4).activate("SITE").ok              # the seat that was handed back is taken
    # the central server sees one activation (the host), and how many seats the host reports in use
    d = site["client"].get(f"/admin/license/{site['vol']['license_id']}", headers=site["admin"]).json()
    assert len(d["activations"]) == 1 and d["history"][0]["action"] == "host.grant"
    assert host.renew() == "ok"
    d = site["client"].get(f"/admin/license/{site['vol']['license_id']}", headers=site["admin"]).json()
    assert d["history"][0]["detail"]["seats_in_use"] == 3 and d["history"][0]["detail"]["renewed"] is True and len(d["activations"]) == 1


def test_a_site_survives_an_outage_but_not_for_ever(site):
    host, pc, clock = site["host"], site["pc"], site["clock"]
    assert host.renew() == "ok" and pc(1).activate("SITE").ok
    host.s.central_url = "http://127.0.0.1:9"                               # the internet is gone
    assert host.renew() == "unreachable"
    clock["t"] = time.time() + 20 * 86400
    assert pc(1).check().ok                                                 # day 20: the host still serves from its grant
    lease_exp = pc(1).check().lease_expires_at
    assert lease_exp <= host.grant()["exp"]                                 # a lease never outlives the host's grant
    clock["t"] = time.time() + 31 * 86400
    assert host.grant() is None                                             # day 31: the grant has run out
    assert pc(1).check().reason == "lease_expired"                          # and so has the computer's last lease
    clock["t"] = None


def test_a_computer_that_leaves_the_site_runs_from_its_lease_and_its_seat_comes_back(site):
    host, pc, clock = site["host"], site["pc"], site["clock"]
    assert host.renew() == "ok" and pc(1).activate("SITE").ok
    away = lambda: site["pc"](1, base="http://127.0.0.1:9")                 # noqa: E731  the laptop is on the road
    assert away().check().ok and away().check().source == "cache"
    clock["t"] = time.time() + 8 * 86400
    assert away().check().reason == "lease_expired"                         # after 7 days away it must come back
    clock["t"] = None
    assert host.seats_used() == 1
    clock["t"] = time.time() + 31 * 86400
    host._put("grant", ""); assert host.seats_used() == 0                   # not seen for 30 days: the seat is free again
    clock["t"] = None


def test_revoking_the_volume_licence_stops_the_site(site):
    host, pc = site["host"], site["pc"]
    assert host.renew() == "ok" and pc(1).activate("SITE").ok
    site["client"].patch(f"/admin/licenses/{site['vol']['license_key']}/revoke", headers=site["admin"])
    assert host.renew() == "license_revoked" and host.grant() is None
    r = pc(1).check()
    assert not r.ok and r.reason == "license_revoked"                       # the computers are told why, and drop their lease
    assert site["pc"](1, base="http://127.0.0.1:9").check().reason == "offline_no_lease"


def test_only_a_host_certified_by_the_central_server_is_believed(site, tmp_path):
    host, pc, hs, settings = site["host"], site["pc"], site["hs"], site["settings"]
    assert host.renew() == "ok"
    # a home-made host with its own key and a grant it signed itself
    fake_central = secrets.token_bytes(32)
    rogue = sitehost.create_app(sitehost.HostSettings(central_url="http://127.0.0.1:9", app_id=hs.app_id, app_secret=hs.app_secret, license_key="x",
                                                      central_public_key=security.public_key_b64(fake_central), data_dir=str(tmp_path / "rogue")), background=False)
    rh = rogue.state.host
    rh._put("grant", security.sign_lease(fake_central, {"typ": "grant", "lic": "x", "app": hs.app_id, "host": security.sha256_hex(rh.host_id), "hpk": rh.public_key,
                                                        "seats": 100000, "plan": "ultimate", "iat": time.time(), "exp": time.time() + 10**8}))
    assert rh.grant()["seats"] == 100000                                    # the rogue host believes itself…
    url, stop = _serve(rogue)
    try:
        r = pc(9, base=url).activate("SITE")
        assert not r.ok and r.reason == "bad_server_signature"              # …the computers do not
    finally:
        stop()
    # a real grant copied to another machine does not work there: it names the first host's key and id
    copy = sitehost.create_app(sitehost.HostSettings(**{**hs.__dict__, "data_dir": str(tmp_path / "copy")}), background=False)
    copy.state.host._put("grant", host._get("grant"))
    assert copy.state.host.grant() is None
    # and a second host on the same one-host licence is refused by the central server
    assert copy.state.host.renew() == "host_limit_reached"
    # wrong app secret, replay and a stale clock at the host
    import httpx
    h = {"X-App-Id": hs.app_id, "X-App-Secret": hs.app_secret}
    body = {"license_key": "SITE", "device_id": "site-computer-0042", "timestamp": int(time.time()), "nonce": secrets.token_urlsafe(24)}
    assert httpx.post(site["host_url"] + "/v1/activate", json=body, headers={**h, "X-App-Secret": "wrong"}).status_code == 401
    assert httpx.post(site["host_url"] + "/v1/activate", json=body, headers=h).status_code == 200
    assert httpx.post(site["host_url"] + "/v1/activate", json=body, headers=h).json()["error"] == "replayed_request"
    assert httpx.post(site["host_url"] + "/v1/validate", json={**body, "nonce": secrets.token_urlsafe(24), "timestamp": 5}, headers=h).json()["error"] == "stale_request"
    st = httpx.get(site["host_url"] + "/status", headers={"X-App-Secret": hs.app_secret}).json()
    assert st["licensed"] and st["seats"] == 3 and st["seats_used"] == 1 and st["plan"] == "site"
    assert httpx.get(site["host_url"] + "/status").status_code == 401
    assert (Path(hs.data_dir) / "host.key").stat().st_mode & 0o077 == 0     # the host's key is readable by its owner only
