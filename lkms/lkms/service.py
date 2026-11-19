"""The licence rules. Every function takes an open database connection; the caller owns the transaction."""
from __future__ import annotations

import json
import time
from dataclasses import dataclass
from datetime import datetime, timezone

import psycopg
from psycopg.rows import dict_row

from . import security
from .config import Settings


class LicenseError(Exception):
    """A refusal the client may see. `code` is stable and safe to show; it never says more than the caller may know."""

    def __init__(self, code: str, http: int = 403, **extra) -> None:
        super().__init__(code)
        self.code, self.http, self.extra = code, http, extra


@dataclass(frozen=True)
class App:
    app_id: str
    name: str


def _now() -> datetime:
    return datetime.now(timezone.utc)


def audit(conn: psycopg.Connection, actor: str, action: str, *, license_id=None, app_id=None, ip=None, **detail) -> None:
    conn.execute(
        "INSERT INTO audit_events (actor, action, license_id, app_id, ip_address, detail) VALUES (%s, %s, %s, %s, %s, %s)",
        (actor, action, license_id, app_id, ip, json.dumps(detail)),
    )


# ---------------------------------------------------------------- apps

def create_app(conn: psycopg.Connection, name: str) -> dict:
    secret = security.new_secret()
    try:
        row = conn.execute(
            "INSERT INTO apps (name, api_secret_hash) VALUES (%s, %s) RETURNING app_id, name, status, created_at",
            (name.strip(), security.sha256_hex(secret)),
        ).fetchone()
    except psycopg.errors.UniqueViolation as exc:
        raise LicenseError("app_name_taken", 409) from exc
    audit(conn, "admin", "app.create", app_id=row[0], name=row[1])
    return {"app_id": str(row[0]), "name": row[1], "status": row[2], "created_at": row[3].isoformat(), "api_secret": secret}


def authenticate_app(conn: psycopg.Connection, app_id: str, secret: str) -> App:
    """The app behind a request, or a refusal that does not say whether the id or the secret was wrong."""
    try:
        row = conn.execute("SELECT app_id, name, api_secret_hash, status FROM apps WHERE app_id = %s", (app_id,)).fetchone()
    except (psycopg.errors.InvalidTextRepresentation, psycopg.DataError):
        conn.rollback()
        row = None
    # always compare something, so a wrong id takes as long as a wrong secret
    stored = row[2] if row else "0" * 64
    ok = security.same(stored, security.sha256_hex(secret)) and row is not None and row[3] == "active"
    if not ok:
        raise LicenseError("app_not_authorized", 401)
    return App(str(row[0]), row[1])


def use_nonce(conn: psycopg.Connection, settings: Settings, app: App, timestamp: int, nonce: str) -> None:
    """Replay protection: the request must be fresh, and its nonce must not have been used by this app before."""
    if abs(time.time() - timestamp) > settings.timestamp_skew_s:
        raise LicenseError("stale_request", 401)
    if not 16 <= len(nonce) <= 128:
        raise LicenseError("bad_nonce", 400)
    done = conn.execute(
        "INSERT INTO request_nonces (app_id, nonce) VALUES (%s, %s) ON CONFLICT DO NOTHING RETURNING 1", (app.app_id, nonce)
    ).fetchone()
    if not done:
        raise LicenseError("replayed_request", 409)
    # old nonces can never be accepted again (their timestamp is stale), so they can go
    conn.execute("DELETE FROM request_nonces WHERE seen_at < now() - make_interval(secs => %s)", (settings.timestamp_skew_s * 4,))


# ---------------------------------------------------------------- licences (admin)

def create_licenses(conn: psycopg.Connection, settings: Settings, *, app_id: str, count: int, plan: str,
                    max_activations: int, expires_at: datetime | None, customer_email: str | None, metadata: dict) -> list[dict]:
    if not conn.execute("SELECT 1 FROM apps WHERE app_id = %s", (app_id,)).fetchone():
        raise LicenseError("app_not_found", 404)
    out = []
    for _ in range(count):
        key = security.new_license_key()
        row = conn.execute(
            """INSERT INTO licenses (key_hash, key_hint, app_id, customer_email, plan, max_activations, expires_at, metadata)
               VALUES (%s, %s, %s, %s, %s, %s, %s, %s) RETURNING license_id, created_at""",
            (security.key_hash(settings.key_pepper, key), key[:5], app_id, customer_email, plan, max_activations, expires_at, json.dumps(metadata)),
        ).fetchone()
        out.append({"license_id": str(row[0]), "license_key": key, "app_id": app_id, "plan": plan, "max_activations": max_activations,
                    "expires_at": expires_at.isoformat() if expires_at else None, "customer_email": customer_email, "status": "active"})
    audit(conn, "admin", "license.create", app_id=app_id, count=count, plan=plan)
    return out


_TRANSITIONS = {"revoke": "revoked", "suspend": "suspended", "reinstate": "active"}


def set_license_status(conn: psycopg.Connection, settings: Settings, key: str, action: str, reason: str | None) -> dict:
    norm = security.normalize_key(key)
    row = conn.execute(
        "SELECT license_id, status FROM licenses WHERE key_hash = %s FOR UPDATE", (security.key_hash(settings.key_pepper, norm or ""),)
    ).fetchone() if norm else None
    if not row:
        raise LicenseError("license_not_found", 404)
    if row[1] == "revoked" and action != "revoke":
        raise LicenseError("license_revoked_for_good", 409)       # a revoked key never comes back; issue a new one
    new = _TRANSITIONS[action]
    conn.execute("UPDATE licenses SET status = %s, status_reason = %s, updated_at = now() WHERE license_id = %s", (new, reason, row[0]))
    freed = 0
    if new == "revoked":
        freed = conn.execute("DELETE FROM activations WHERE license_id = %s", (row[0],)).rowcount
    audit(conn, "admin", "license." + action, license_id=row[0], reason=reason, seats_freed=freed)
    return {"license_id": str(row[0]), "status": new, "seats_freed": freed}


def list_licenses(conn: psycopg.Connection, app_id: str | None, limit: int, offset: int) -> list[dict]:
    with conn.cursor(row_factory=dict_row) as cur:
        cur.execute(
            """SELECT l.license_id, l.key_hint, l.app_id, l.customer_email, l.plan, l.max_activations, l.expires_at,
                      CASE WHEN l.status = 'active' AND l.expires_at IS NOT NULL AND l.expires_at <= now() THEN 'expired' ELSE l.status END AS status,
                      l.created_at, (SELECT count(*) FROM activations a WHERE a.license_id = l.license_id) AS activations
               FROM licenses l WHERE (%s::uuid IS NULL OR l.app_id = %s::uuid) ORDER BY l.created_at DESC LIMIT %s OFFSET %s""",
            (app_id, app_id, limit, offset),
        )
        rows = cur.fetchall()
    for r in rows:
        for k in ("license_id", "app_id"):
            r[k] = str(r[k])
        for k in ("expires_at", "created_at"):
            r[k] = r[k].isoformat() if r[k] else None
        r["key_hint"] += "-…"
    return rows


# ---------------------------------------------------------------- licences (client)

def _load(conn: psycopg.Connection, settings: Settings, app: App, key: str, lock: bool):
    """The licence a client named, only if it belongs to the calling app. Every other case looks the same to the caller."""
    norm = security.normalize_key(key)
    row = None
    if norm:
        row = conn.execute(
            "SELECT license_id, app_id, plan, max_activations, expires_at, status, metadata FROM licenses WHERE key_hash = %s"
            + (" FOR UPDATE" if lock else ""),
            (security.key_hash(settings.key_pepper, norm),),
        ).fetchone()
    if not row or str(row[1]) != app.app_id:
        raise LicenseError("invalid_license", 404)
    lic = {"license_id": row[0], "plan": row[2], "max_activations": row[3], "expires_at": row[4], "status": row[5], "metadata": row[6]}
    if lic["status"] == "active" and lic["expires_at"] and lic["expires_at"] <= _now():
        lic["status"] = "expired"
        conn.execute("UPDATE licenses SET status = 'expired', updated_at = now() WHERE license_id = %s AND status = 'active'", (lic["license_id"],))
    if lic["status"] != "active":
        raise LicenseError("license_" + lic["status"], 403)
    return lic


def _lease(settings: Settings, app: App, lic: dict, device_hash: str, nonce: str) -> dict:
    """What the app may rely on until `exp`, signed so that neither the user nor a fake server can make one."""
    now = int(time.time())
    exp = now + settings.lease_hours * 3600
    if lic["expires_at"]:
        exp = min(exp, int(lic["expires_at"].timestamp()))
    payload = {"lic": str(lic["license_id"]), "app": app.app_id, "dev": device_hash, "plan": lic["plan"], "iat": now, "exp": exp,
               "lexp": int(lic["expires_at"].timestamp()) if lic["expires_at"] else None, "nonce": nonce}
    return {"valid": True, "plan": lic["plan"], "license_expires_at": lic["expires_at"].isoformat() if lic["expires_at"] else None,
            "lease_expires_at": datetime.fromtimestamp(exp, timezone.utc).isoformat(), "metadata": lic["metadata"],
            "lease": security.sign_lease(settings.signing_key, payload)}


def activate(conn: psycopg.Connection, settings: Settings, app: App, *, key: str, device_id: str, device_name: str | None, ip: str | None, nonce: str) -> dict:
    lic = _load(conn, settings, app, key, lock=True)          # the row lock makes two devices racing for the last seat take turns
    dev = security.sha256_hex(device_id)
    again = conn.execute(
        "UPDATE activations SET last_seen_at = now(), ip_address = %s, device_name = COALESCE(%s, device_name) WHERE license_id = %s AND device_hash = %s RETURNING activation_id",
        (ip, device_name, lic["license_id"], dev),
    ).fetchone()
    used = conn.execute("SELECT count(*) FROM activations WHERE license_id = %s", (lic["license_id"],)).fetchone()[0]
    if not again:
        if used >= lic["max_activations"]:
            raise LicenseError("activation_limit_reached", 409, max_activations=lic["max_activations"], used=used)
        conn.execute("INSERT INTO activations (license_id, device_hash, device_name, ip_address) VALUES (%s, %s, %s, %s)", (lic["license_id"], dev, device_name, ip))
        used += 1
        audit(conn, "app:" + app.app_id, "activate", license_id=lic["license_id"], app_id=app.app_id, ip=ip, used=used)
    return {**_lease(settings, app, lic, dev, nonce), "activations_used": used, "max_activations": lic["max_activations"], "already_activated": bool(again)}


def validate(conn: psycopg.Connection, settings: Settings, app: App, *, key: str, device_id: str, ip: str | None, nonce: str) -> dict:
    lic = _load(conn, settings, app, key, lock=False)
    dev = security.sha256_hex(device_id)
    seen = conn.execute(
        "UPDATE activations SET last_seen_at = now(), ip_address = %s WHERE license_id = %s AND device_hash = %s RETURNING 1", (ip, lic["license_id"], dev)
    ).fetchone()
    if not seen:
        raise LicenseError("device_not_activated", 403)
    return _lease(settings, app, lic, dev, nonce)


def deactivate(conn: psycopg.Connection, settings: Settings, app: App, *, key: str, device_id: str, ip: str | None) -> dict:
    norm = security.normalize_key(key)
    row = conn.execute("SELECT license_id, app_id FROM licenses WHERE key_hash = %s FOR UPDATE",
                       (security.key_hash(settings.key_pepper, norm or ""),)).fetchone() if norm else None
    if not row or str(row[1]) != app.app_id:
        raise LicenseError("invalid_license", 404)
    gone = conn.execute("DELETE FROM activations WHERE license_id = %s AND device_hash = %s", (row[0], security.sha256_hex(device_id))).rowcount
    if gone:
        audit(conn, "app:" + app.app_id, "deactivate", license_id=row[0], app_id=app.app_id, ip=ip)
    return {"deactivated": bool(gone)}     # a seat of a suspended or expired licence can still be handed back
