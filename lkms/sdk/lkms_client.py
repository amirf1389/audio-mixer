"""LKMS client helper for your apps. One file; needs only `cryptography` (to check the server's signature).

    from lkms_client import LicenseClient
    lic = LicenseClient(base_url="https://licence.example.com", app_id="…", app_secret="…",
                        public_key="…", cache_path="~/.myapp/licence.json")
    result = lic.activate("7K2QD-0XWMA-9F3TZ-B6HNC-R41JE")     # once, when the customer enters the key
    result = lic.check()                                       # at every start, and now and then while running
    if result.ok: ...   # result.plan, result.source ("server" or "cache"), result.reason

On a customer site with a SITE HOST (volume licence), give the host's address as base_url and use activate("SITE"):
the helper then checks that your central server certified that host before it believes it.

How it behaves:
- Every answer of the server carries a signed lease. The helper checks the signature with the public key built
  into your app, so a fake server or an edited cache file is not believed.
- The lease is kept on disk. When the server cannot be reached, the app runs from the lease until it runs out
  (the grace period the server sets, 7 days by default). A clear refusal from the server always wins over the cache.
- Setting the computer's clock back does not extend a lease: the helper remembers the latest time it has seen.

Note: an app secret inside a program you ship can be read out by a determined user. It identifies your app and
keeps casual callers out; it is not what protects a licence. The key's randomness, the seat limit, the rate limits
and the signed lease do that.
"""
from __future__ import annotations

import base64
import hashlib
import json
import os
import platform
import secrets
import time
import urllib.error
import urllib.request
import uuid
from dataclasses import dataclass
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey


@dataclass
class LicenseResult:
    ok: bool
    reason: str = ""            # "" when ok; else the server's code, "offline_no_lease", "lease_expired", "clock_moved_back", …
    plan: str | None = None
    source: str = "server"      # "server" or "cache"
    lease_expires_at: float | None = None
    detail: dict | None = None


def default_device_id() -> str:
    """A stable id for this computer. Replace it with your own if you already have one (hardware id, install id)."""
    raw = f"{platform.node()}|{platform.system()}|{platform.machine()}|{uuid.getnode()}"
    return hashlib.sha256(raw.encode()).hexdigest()


class LicenseClient:
    def __init__(self, *, base_url: str, app_id: str, app_secret: str, public_key: str, cache_path: str,
                 device_id: str | None = None, timeout: float = 8.0, _now=time.time) -> None:
        self.base_url = base_url.rstrip("/")
        self.app_id, self.app_secret, self.public_key = app_id, app_secret, public_key
        self.cache_path = Path(os.path.expanduser(cache_path))
        self.device_id = device_id or default_device_id()
        self.timeout, self._now = timeout, _now

    # ---- public

    def activate(self, license_key: str, device_name: str | None = None) -> LicenseResult:
        """Bind this device to the key. Safe to call again: the same device keeps its one seat."""
        return self._ask("/v1/activate", license_key, {"device_name": device_name or platform.node()[:120]})

    def check(self, license_key: str | None = None) -> LicenseResult:
        """Is this device licensed now? Asks the server; falls back to the saved lease when it cannot be reached."""
        key = license_key or self._cache().get("license_key")
        if not key:
            return LicenseResult(False, "not_activated")
        return self._ask("/v1/validate", key, {})

    def deactivate(self) -> LicenseResult:
        """Hand this device's seat back (before moving the licence to another computer)."""
        key = self._cache().get("license_key")
        if not key:
            return LicenseResult(False, "not_activated")
        status, body = self._post("/v1/deactivate", self._body(key, {}))
        if status == 200:
            self._forget()
            return LicenseResult(True, detail=body)
        return LicenseResult(False, body.get("error", "unreachable") if body else "unreachable")

    # ---- inside

    def _body(self, key: str, extra: dict) -> dict:
        return {"license_key": key, "device_id": self.device_id, "timestamp": int(self._now()), "nonce": secrets.token_urlsafe(24), **extra}

    def _post(self, path: str, body: dict) -> tuple[int, dict | None]:
        """(status, json) from the server; (0, None) when it could not be reached or did not answer properly."""
        req = urllib.request.Request(self.base_url + path, data=json.dumps(body).encode(), method="POST",
                                     headers={"Content-Type": "application/json", "X-App-Id": self.app_id, "X-App-Secret": self.app_secret})
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as r:
                return r.status, json.loads(r.read())
        except urllib.error.HTTPError as e:
            try:
                return e.code, json.loads(e.read())
            except Exception:  # noqa: BLE001
                return e.code, None
        except Exception:  # noqa: BLE001  (no network, DNS, timeout, TLS)
            return 0, None

    def _ask(self, path: str, key: str, extra: dict) -> LicenseResult:
        body = self._body(key, extra)
        status, answer = self._post(path, body)
        if status == 200 and answer:
            payload = self._verify(answer.get("lease", ""), grant=answer.get("grant"))
            # the lease must be for this app, this device and THIS request (its nonce): an old answer cannot be replayed to us
            if not payload or payload.get("app") != self.app_id or payload.get("nonce") != body["nonce"] \
                    or payload.get("dev") != hashlib.sha256(self.device_id.encode()).hexdigest():
                return LicenseResult(False, "bad_server_signature")
            self._save({"license_key": key, "lease": answer["lease"], "grant": answer.get("grant"), "seen": self._now()})
            return LicenseResult(True, plan=payload.get("plan"), lease_expires_at=payload.get("exp"), detail=answer)
        if status in (403, 404, 409) and answer and answer.get("error"):
            # the server said no (revoked, expired, limit reached, unknown key): the saved lease is not used
            if status != 409:
                self._forget(keep_key=True)
            return LicenseResult(False, answer["error"], detail=answer)
        if status == 401 and answer and answer.get("error") == "app_not_authorized":
            return LicenseResult(False, "app_not_authorized", detail=answer)      # this app was switched off at the server
        # could not reach the server, or it is in trouble (5xx, 429): run from the saved lease while it lasts
        return self._from_cache()

    def _from_cache(self) -> LicenseResult:
        c = self._cache()
        lease = c.get("lease")
        if not lease:
            return LicenseResult(False, "offline_no_lease", source="cache")
        now = self._now()
        if now + 300 < float(c.get("seen", 0)):
            return LicenseResult(False, "clock_moved_back", source="cache")
        payload = self._verify(lease, now, c.get("grant"))
        if not payload or payload.get("app") != self.app_id or payload.get("dev") != hashlib.sha256(self.device_id.encode()).hexdigest():
            return LicenseResult(False, "lease_expired", source="cache")
        self._save({**c, "seen": max(now, float(c.get("seen", 0)))})
        return LicenseResult(True, plan=payload.get("plan"), source="cache", lease_expires_at=payload.get("exp"))

    def _signed(self, public_key: str, token: str, now: float | None) -> dict | None:
        """The payload of <payload>.<signature> when that key signed it and it has not run out; otherwise None."""
        try:
            body, sig = token.split(".")
            pad = lambda s: s + "=" * (-len(s) % 4)  # noqa: E731
            Ed25519PublicKey.from_public_bytes(base64.b64decode(public_key)).verify(base64.urlsafe_b64decode(pad(sig)), body.encode())
            payload = json.loads(base64.urlsafe_b64decode(pad(body)))
        except (ValueError, InvalidSignature, TypeError, AttributeError):
            return None
        if not isinstance(payload, dict) or float(payload.get("exp", 0)) < (self._now() if now is None else now):
            return None
        return payload

    def _verify(self, lease: str, now: float | None = None, grant: str | None = None) -> dict | None:
        """A lease is believed when the central server signed it, or when a site host signed it AND the central server
        signed a grant naming that host's key for this app. Anything else (a home-made host, an edited file) is refused."""
        if not grant:
            return self._signed(self.public_key, lease, now)
        g = self._signed(self.public_key, grant, now)
        if not g or g.get("typ") != "grant" or g.get("app") != self.app_id or not g.get("hpk"):
            return None
        payload = self._signed(g["hpk"], lease, now)
        if not payload or payload.get("lic") != g.get("lic") or float(payload.get("exp", 0)) > float(g.get("exp", 0)):
            return None
        return payload

    def _cache(self) -> dict:
        try:
            data = json.loads(self.cache_path.read_text())
            return data if isinstance(data, dict) else {}
        except Exception:  # noqa: BLE001
            return {}

    def _save(self, data: dict) -> None:
        self.cache_path.parent.mkdir(parents=True, exist_ok=True)
        tmp = self.cache_path.with_suffix(".tmp")
        tmp.write_text(json.dumps(data))
        try:
            os.chmod(tmp, 0o600)
        except OSError:
            pass
        os.replace(tmp, self.cache_path)       # written whole or not at all

    def _forget(self, keep_key: bool = False) -> None:
        c = self._cache()
        if keep_key and c.get("license_key"):
            self._save({"license_key": c["license_key"], "seen": self._now()})
        else:
            try:
                self.cache_path.unlink()
            except FileNotFoundError:
                pass
