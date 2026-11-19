"""LKMS SITE HOST: activates the computers of one customer site on their own network.

The idea is the same as a volume activation host: the customer installs this once inside their network and gives it
their volume licence key. The host proves the licence to your central server and gets a signed GRANT ("this host may
hand out N seats until date D"). The computers on the site then activate against the host, not the internet.

- The host renews its grant every few hours. If it cannot reach the central server it keeps serving until the grant
  runs out (30 days by default), so a site survives an internet outage.
- A computer's lease from the host lasts 7 days by default and is renewed at every check, so a computer that leaves
  the site keeps working for that long.
- A seat that has not been seen for 30 days is given back automatically.
- The devices trust the host because the grant carries the host's public key, signed by the central server. A copied
  or home-made host cannot make leases the devices accept.

Run:  uvicorn --factory sitehost:app --host 0.0.0.0 --port 8090      (settings: see deploy/site-host.env.example)
"""
from __future__ import annotations

import base64
import hashlib
import hmac
import json
import os
import platform
import secrets
import sqlite3
import threading
import time
import urllib.error
import urllib.request
from contextlib import asynccontextmanager
from dataclasses import dataclass
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey
from fastapi import FastAPI, Header, Request
from fastapi.responses import JSONResponse
from pydantic import BaseModel, Field


def _b64u(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode()


def _unb64u(text: str) -> bytes:
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


def verify_signed(public_key_b64: str, token: str, now: float | None = None) -> dict | None:
    """The payload of <payload>.<signature> when the key signed it and it has not run out; otherwise None."""
    try:
        body, sig = token.split(".")
        Ed25519PublicKey.from_public_bytes(base64.b64decode(public_key_b64)).verify(_unb64u(sig), body.encode())
        payload = json.loads(_unb64u(body))
    except (ValueError, InvalidSignature, TypeError):
        return None
    if not isinstance(payload, dict) or float(payload.get("exp", 0)) < (time.time() if now is None else now):
        return None
    return payload


@dataclass
class HostSettings:
    central_url: str
    app_id: str
    app_secret: str
    license_key: str
    central_public_key: str
    data_dir: str
    client_lease_hours: int = 168
    reclaim_days: int = 30
    renew_every_s: int = 6 * 3600

    @staticmethod
    def from_env() -> "HostSettings":
        need = {k: os.environ.get("LKMS_HOST_" + k, "") for k in ("CENTRAL_URL", "APP_ID", "APP_SECRET", "LICENSE_KEY", "CENTRAL_PUBLIC_KEY")}
        missing = [k for k, v in need.items() if not v]
        if missing:
            raise RuntimeError("missing settings: " + ", ".join("LKMS_HOST_" + k for k in missing))
        return HostSettings(central_url=need["CENTRAL_URL"].rstrip("/"), app_id=need["APP_ID"], app_secret=need["APP_SECRET"], license_key=need["LICENSE_KEY"],
                            central_public_key=need["CENTRAL_PUBLIC_KEY"], data_dir=os.environ.get("LKMS_HOST_DATA", "./site-host-data"),
                            client_lease_hours=int(os.environ.get("LKMS_HOST_CLIENT_LEASE_HOURS", "168")), reclaim_days=int(os.environ.get("LKMS_HOST_RECLAIM_DAYS", "30")))


class ClientIn(BaseModel):
    license_key: str = Field(default="SITE", max_length=64)     # ignored: on a site the host holds the licence
    device_id: str = Field(min_length=8, max_length=256)
    device_name: str | None = Field(default=None, max_length=120)
    timestamp: int
    nonce: str = Field(min_length=16, max_length=128)


class SiteHost:
    """The host's state: its own key, its grant from the central server, and the seats it has handed out."""

    def __init__(self, settings: HostSettings, now=time.time) -> None:
        self.s, self.now = settings, now
        self.dir = Path(settings.data_dir); self.dir.mkdir(parents=True, exist_ok=True)
        self.lock = threading.Lock()
        self.db = sqlite3.connect(self.dir / "site-host.sqlite3", check_same_thread=False, isolation_level=None)
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.execute("CREATE TABLE IF NOT EXISTS seats (device_hash TEXT PRIMARY KEY, device_name TEXT, ip TEXT, activated_at REAL NOT NULL, last_seen_at REAL NOT NULL)")
        self.db.execute("CREATE TABLE IF NOT EXISTS state (k TEXT PRIMARY KEY, v TEXT NOT NULL)")
        self.key = self._own_key()
        self.host_id = self._get("host_id") or self._put("host_id", "site-host-" + secrets.token_hex(16))
        self.nonces: dict[str, float] = {}

    # ---- small state store
    def _get(self, k: str) -> str | None:
        row = self.db.execute("SELECT v FROM state WHERE k = ?", (k,)).fetchone()
        return row[0] if row else None

    def _put(self, k: str, v: str) -> str:
        self.db.execute("INSERT INTO state (k, v) VALUES (?, ?) ON CONFLICT(k) DO UPDATE SET v = excluded.v", (k, v))
        return v

    def _own_key(self) -> Ed25519PrivateKey:
        """This host's own signing key, made here on first start and never sent anywhere (only its public half is)."""
        path = self.dir / "host.key"
        if not path.exists():
            raw = Ed25519PrivateKey.generate().private_bytes(serialization.Encoding.Raw, serialization.PrivateFormat.Raw, serialization.NoEncryption())
            fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            with os.fdopen(fd, "wb") as f:
                f.write(raw)
        return Ed25519PrivateKey.from_private_bytes(path.read_bytes())

    @property
    def public_key(self) -> str:
        return base64.b64encode(self.key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)).decode()

    # ---- the grant
    def grant(self) -> dict | None:
        """The current grant's payload, if it is the central server's, for this host, and not run out."""
        token = self._get("grant")
        p = verify_signed(self.s.central_public_key, token, self.now()) if token else None
        if not p or p.get("typ") != "grant" or p.get("app") != self.s.app_id or p.get("hpk") != self.public_key \
                or p.get("host") != hashlib.sha256(self.host_id.encode()).hexdigest():
            return None
        return p

    def renew(self) -> str:
        """Ask the central server for a fresh grant. Returns "ok", "unreachable", or the server's refusal code."""
        nonce = secrets.token_urlsafe(24)
        body = {"license_key": self.s.license_key, "device_id": self.host_id, "device_name": platform.node()[:120] or "site host", "timestamp": int(self.now()),
                "nonce": nonce, "host_public_key": self.public_key, "seats_in_use": self.seats_used()}
        req = urllib.request.Request(self.s.central_url + "/v1/host/grant", data=json.dumps(body).encode(), method="POST",
                                     headers={"Content-Type": "application/json", "X-App-Id": self.s.app_id, "X-App-Secret": self.s.app_secret})
        try:
            with urllib.request.urlopen(req, timeout=10) as r:
                answer = json.loads(r.read())
        except urllib.error.HTTPError as e:
            try:
                code = json.loads(e.read()).get("error", "")
            except Exception:  # noqa: BLE001
                code = ""
            if e.code in (403, 404, 409) and code:          # the central server said no: stop serving, and say why
                self._put("refused", code); self._put("grant", "")
                return code
            return "unreachable"
        except Exception:  # noqa: BLE001
            return "unreachable"
        p = verify_signed(self.s.central_public_key, answer.get("grant", ""), self.now())
        if not p or p.get("nonce") != nonce or p.get("hpk") != self.public_key:
            return "bad_central_signature"
        self._put("grant", answer["grant"]); self._put("refused", ""); self._put("renewed_at", str(self.now()))
        return "ok"

    # ---- seats
    def seats_used(self) -> int:
        self.db.execute("DELETE FROM seats WHERE last_seen_at < ?", (self.now() - self.s.reclaim_days * 86400,))
        return self.db.execute("SELECT count(*) FROM seats").fetchone()[0]

    def fresh(self, timestamp: int, nonce: str) -> str | None:
        now = self.now()
        if abs(now - timestamp) > 300:
            return "stale_request"
        for k in [k for k, t in self.nonces.items() if now - t > 1200]:
            del self.nonces[k]
        if nonce in self.nonces:
            return "replayed_request"
        self.nonces[nonce] = now
        return None

    def lease(self, grant: dict, device_hash: str, nonce: str) -> dict:
        now = int(self.now())
        exp = min(now + self.s.client_lease_hours * 3600, int(grant["exp"]))       # never longer than the host's own grant
        payload = {"lic": grant["lic"], "app": grant["app"], "dev": device_hash, "plan": grant.get("plan"), "iat": now, "exp": exp, "nonce": nonce, "via": "host"}
        body = _b64u(json.dumps(payload, separators=(",", ":"), sort_keys=True).encode())
        return {"valid": True, "plan": grant.get("plan"), "lease_expires_at": exp, "lease": body + "." + _b64u(self.key.sign(body.encode())), "grant": self._get("grant")}


def create_app(settings: HostSettings | None = None, *, background: bool = True, now=time.time) -> FastAPI:
    settings = settings or HostSettings.from_env()
    host = SiteHost(settings, now)
    stop = threading.Event()

    def loop() -> None:
        while not stop.is_set():
            result = host.renew()
            stop.wait(settings.renew_every_s if result == "ok" else 300)        # in trouble: try again in 5 minutes

    @asynccontextmanager
    async def lifespan(_: FastAPI):
        if background:
            threading.Thread(target=loop, daemon=True, name="lkms-host-renew").start()
        yield
        stop.set()

    app = FastAPI(title="LKMS site host", docs_url=None, redoc_url=None, openapi_url=None, lifespan=lifespan)
    app.state.host = host

    def refuse(code: str, http: int, **extra):
        return JSONResponse(status_code=http, content={"valid": False, "error": code, **extra}, headers={"Cache-Control": "no-store"})

    def gate(app_id: str, app_secret: str, body: ClientIn):
        """(grant, None) when the caller is this site's app and the host is licensed; else (None, refusal)."""
        if not (hmac.compare_digest(app_id, settings.app_id) and hmac.compare_digest(app_secret, settings.app_secret)):
            return None, refuse("app_not_authorized", 401)
        why = host.fresh(body.timestamp, body.nonce)
        if why:
            return None, refuse(why, 401 if why == "stale_request" else 409)
        grant = host.grant()
        if not grant:
            refused = host._get("refused")
            # the central server refused the licence (revoked, expired, …): say so. Otherwise the host is merely not ready:
            # the devices then run from their saved leases.
            return None, refuse(refused, 403) if refused else refuse("host_not_licensed", 503)
        return grant, None

    @app.post("/v1/activate")
    def activate(body: ClientIn, request: Request, x_app_id: str = Header(default=""), x_app_secret: str = Header(default="")):
        with host.lock:
            grant, no = gate(x_app_id, x_app_secret, body)
            if no:
                return no
            dev, t = hashlib.sha256(body.device_id.encode()).hexdigest(), host.now()
            ip = request.client.host if request.client else None
            used = host.seats_used()
            known = host.db.execute("SELECT 1 FROM seats WHERE device_hash = ?", (dev,)).fetchone()
            if not known:
                if used >= int(grant["seats"]):
                    return refuse("activation_limit_reached", 409, max_activations=int(grant["seats"]), used=used)
                host.db.execute("INSERT INTO seats VALUES (?, ?, ?, ?, ?)", (dev, body.device_name, ip, t, t)); used += 1
            else:
                host.db.execute("UPDATE seats SET last_seen_at = ?, ip = ?, device_name = COALESCE(?, device_name) WHERE device_hash = ?", (t, ip, body.device_name, dev))
            return {**host.lease(grant, dev, body.nonce), "activations_used": used, "max_activations": int(grant["seats"]), "already_activated": bool(known)}

    @app.post("/v1/validate")
    def validate(body: ClientIn, x_app_id: str = Header(default=""), x_app_secret: str = Header(default="")):
        with host.lock:
            grant, no = gate(x_app_id, x_app_secret, body)
            if no:
                return no
            dev = hashlib.sha256(body.device_id.encode()).hexdigest()
            host.seats_used()
            if host.db.execute("UPDATE seats SET last_seen_at = ? WHERE device_hash = ?", (host.now(), dev)).rowcount == 0:
                return refuse("device_not_activated", 403)
            return host.lease(grant, dev, body.nonce)

    @app.post("/v1/deactivate")
    def deactivate(body: ClientIn, x_app_id: str = Header(default=""), x_app_secret: str = Header(default="")):
        with host.lock:
            if not (hmac.compare_digest(x_app_id, settings.app_id) and hmac.compare_digest(x_app_secret, settings.app_secret)):
                return refuse("app_not_authorized", 401)
            why = host.fresh(body.timestamp, body.nonce)
            if why:
                return refuse(why, 409)
            gone = host.db.execute("DELETE FROM seats WHERE device_hash = ?", (hashlib.sha256(body.device_id.encode()).hexdigest(),)).rowcount
            return {"deactivated": bool(gone)}

    @app.get("/status")
    def status(x_app_secret: str = Header(default="")):
        """For the site's administrator: is the host licensed, until when, and how many seats are taken."""
        if not hmac.compare_digest(x_app_secret, settings.app_secret):
            return refuse("app_not_authorized", 401)
        with host.lock:
            g = host.grant()
            return {"licensed": bool(g), "refused": host._get("refused") or None, "plan": g.get("plan") if g else None, "seats": int(g["seats"]) if g else 0,
                    "seats_used": host.seats_used(), "grant_expires_at": g["exp"] if g else None, "last_renewed_at": float(host._get("renewed_at") or 0) or None}

    @app.get("/healthz")
    def healthz():
        return {"ok": True}

    return app


def app() -> FastAPI:       # uvicorn --factory sitehost:app
    return create_app()
