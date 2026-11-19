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
                    max_activations: int, expires_at: datetime | None, customer_email: str | None, metadata: dict,
                    volume_seats: int | None = None) -> list[dict]:
    if not conn.execute("SELECT 1 FROM apps WHERE app_id = %s", (app_id,)).fetchone():
        raise LicenseError("app_not_found", 404)
    out = []
    for _ in range(count):
        key = security.new_license_key()
        row = conn.execute(
            """INSERT INTO licenses (key_hash, key_hint, app_id, customer_email, plan, max_activations, expires_at, metadata, kind, volume_seats)
               VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s) RETURNING license_id, created_at""",
            (security.key_hash(settings.key_pepper, key), key[:5], app_id, customer_email, plan, max_activations, expires_at, json.dumps(metadata),
             "volume" if volume_seats else "device", volume_seats),
        ).fetchone()
        out.append({"license_id": str(row[0]), "license_key": key, "app_id": app_id, "plan": plan, "max_activations": max_activations,
                    "expires_at": expires_at.isoformat() if expires_at else None, "customer_email": customer_email, "status": "active",
                    "kind": "volume" if volume_seats else "device", "volume_seats": volume_seats})
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
            "SELECT license_id, app_id, plan, max_activations, expires_at, status, metadata, kind, volume_seats FROM licenses WHERE key_hash = %s"
            + (" FOR UPDATE" if lock else ""),
            (security.key_hash(settings.key_pepper, norm),),
        ).fetchone()
    if not row or str(row[1]) != app.app_id:
        raise LicenseError("invalid_license", 404)
    lic = {"license_id": row[0], "plan": row[2], "max_activations": row[3], "expires_at": row[4], "status": row[5], "metadata": row[6],
           "kind": row[7], "volume_seats": row[8]}
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
    if lic["kind"] == "volume":
        raise LicenseError("use_site_host", 409)              # a volume key lives on the customer's site host, not on a device
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
    if lic["kind"] == "volume":
        raise LicenseError("use_site_host", 409)
    dev = security.sha256_hex(device_id)
    seen = conn.execute(
        "UPDATE activations SET last_seen_at = now(), ip_address = %s WHERE license_id = %s AND device_hash = %s RETURNING 1", (ip, lic["license_id"], dev)
    ).fetchone()
    if not seen:
        raise LicenseError("device_not_activated", 403)
    return _lease(settings, app, lic, dev, nonce)


def host_grant(conn: psycopg.Connection, settings: Settings, app: App, *, key: str, device_id: str, device_name: str | None, host_public_key: str,
               seats_in_use: int | None, ip: str | None, nonce: str) -> dict:
    """A site host asks to serve its network. It gets a signed grant: how many seats it may hand out, until when, and
    its own public key certified by this server, so the devices there can trust the leases the host signs."""
    import base64
    try:
        if len(base64.b64decode(host_public_key, validate=True)) != 32:
            raise ValueError
    except Exception as exc:  # noqa: BLE001
        raise LicenseError("bad_host_key", 400) from exc
    lic = _load(conn, settings, app, key, lock=True)
    if lic["kind"] != "volume":
        raise LicenseError("not_a_volume_license", 409)
    dev = security.sha256_hex(device_id)
    again = conn.execute(
        "UPDATE activations SET last_seen_at = now(), ip_address = %s, device_name = COALESCE(%s, device_name) WHERE license_id = %s AND device_hash = %s RETURNING 1",
        (ip, device_name, lic["license_id"], dev)).fetchone()
    if not again:
        used = conn.execute("SELECT count(*) FROM activations WHERE license_id = %s", (lic["license_id"],)).fetchone()[0]
        if used >= lic["max_activations"]:
            raise LicenseError("host_limit_reached", 409, max_hosts=lic["max_activations"])
        conn.execute("INSERT INTO activations (license_id, device_hash, device_name, ip_address) VALUES (%s, %s, %s, %s)", (lic["license_id"], dev, device_name, ip))
    audit(conn, "app:" + app.app_id, "host.grant", license_id=lic["license_id"], app_id=app.app_id, ip=ip, seats=lic["volume_seats"], seats_in_use=seats_in_use, renewed=bool(again))
    now = int(time.time())
    exp = now + settings.grant_hours * 3600
    if lic["expires_at"]:
        exp = min(exp, int(lic["expires_at"].timestamp()))
    payload = {"typ": "grant", "lic": str(lic["license_id"]), "app": app.app_id, "host": dev, "hpk": host_public_key, "seats": lic["volume_seats"],
               "plan": lic["plan"], "iat": now, "exp": exp, "nonce": nonce}
    return {"valid": True, "plan": lic["plan"], "seats": lic["volume_seats"], "grant_expires_at": datetime.fromtimestamp(exp, timezone.utc).isoformat(),
            "metadata": lic["metadata"], "grant": security.sign_lease(settings.signing_key, payload)}


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


# ---------------------------------------------------------------- management (admin console)

def _rows(conn: psycopg.Connection, sql: str, params: tuple = ()) -> list[dict]:
    """Rows as plain dicts that turn into JSON as they are (ids and times as text)."""
    with conn.cursor(row_factory=dict_row) as cur:
        cur.execute(sql, params)
        rows = cur.fetchall()
    for r in rows:
        for k, v in r.items():
            if isinstance(v, datetime):
                r[k] = v.isoformat()
            elif v is not None and not isinstance(v, (str, int, float, bool, dict, list)):
                r[k] = str(v)
    return rows


_EFFECTIVE = "CASE WHEN l.status = 'active' AND l.expires_at IS NOT NULL AND l.expires_at <= now() THEN 'expired' ELSE l.status END"


def overview(conn: psycopg.Connection) -> dict:
    by_status = {r["status"]: r["n"] for r in _rows(conn, f"SELECT {_EFFECTIVE} AS status, count(*) AS n FROM licenses l GROUP BY 1")}
    one = lambda sql: conn.execute(sql).fetchone()[0]  # noqa: E731
    return {
        "apps": one("SELECT count(*) FROM apps"),
        "apps_disabled": one("SELECT count(*) FROM apps WHERE status = 'disabled'"),
        "licenses": sum(by_status.values()),
        "licenses_by_status": {k: by_status.get(k, 0) for k in ("active", "suspended", "revoked", "expired")},
        "activations": one("SELECT count(*) FROM activations"),
        "devices_seen_24h": one("SELECT count(*) FROM activations WHERE last_seen_at > now() - interval '24 hours'"),
        "expiring_30_days": one("SELECT count(*) FROM licenses WHERE status = 'active' AND expires_at > now() AND expires_at <= now() + interval '30 days'"),
        "activity_7_days": _rows(conn, "SELECT to_char(date_trunc('day', at), 'YYYY-MM-DD') AS day, count(*) FILTER (WHERE action = 'activate') AS activations, "
                                       "count(*) FILTER (WHERE action = 'license.create') AS issued FROM audit_events WHERE at > now() - interval '7 days' GROUP BY 1 ORDER BY 1"),
    }


def list_apps(conn: psycopg.Connection) -> list[dict]:
    return _rows(conn, """SELECT a.app_id, a.name, a.status, a.created_at,
                                 (SELECT count(*) FROM licenses l WHERE l.app_id = a.app_id) AS licenses,
                                 (SELECT count(*) FROM activations x JOIN licenses l ON l.license_id = x.license_id WHERE l.app_id = a.app_id) AS activations
                          FROM apps a ORDER BY a.created_at""")


def _uuid(value: str, code: str) -> str:
    import uuid
    try:
        return str(uuid.UUID(value))
    except (ValueError, AttributeError, TypeError) as exc:
        raise LicenseError(code, 404) from exc


def update_app(conn: psycopg.Connection, app_id: str, *, name: str | None, status: str | None) -> dict:
    app_id = _uuid(app_id, "app_not_found")
    try:
        row = conn.execute("UPDATE apps SET name = COALESCE(%s, name), status = COALESCE(%s, status) WHERE app_id = %s RETURNING app_id, name, status",
                           (name.strip() if name else None, status, app_id)).fetchone()
    except psycopg.errors.UniqueViolation as exc:
        raise LicenseError("app_name_taken", 409) from exc
    if not row:
        raise LicenseError("app_not_found", 404)
    audit(conn, "admin", "app.update", app_id=row[0], name=row[1], status=row[2])
    return {"app_id": str(row[0]), "name": row[1], "status": row[2]}


def rotate_app_secret(conn: psycopg.Connection, app_id: str) -> dict:
    """A new secret for an app; the old one stops working at once. Shown once."""
    app_id = _uuid(app_id, "app_not_found")
    secret = security.new_secret()
    row = conn.execute("UPDATE apps SET api_secret_hash = %s WHERE app_id = %s RETURNING app_id, name", (security.sha256_hex(secret), app_id)).fetchone()
    if not row:
        raise LicenseError("app_not_found", 404)
    audit(conn, "admin", "app.rotate_secret", app_id=row[0])
    return {"app_id": str(row[0]), "name": row[1], "api_secret": secret}


def search_licenses(conn: psycopg.Connection, settings: Settings, *, app_id: str | None, status: str | None, q: str | None, limit: int, offset: int) -> dict:
    """Licences by app, status and a search text: a whole key (looked up by its hash), the start of a key, or part of an e-mail."""
    where, params = ["TRUE"], []
    if app_id:
        where.append("l.app_id = %s"); params.append(_uuid(app_id, "app_not_found"))
    if status:
        where.append(f"({_EFFECTIVE}) = %s"); params.append(status)
    if q and q.strip():
        norm = security.normalize_key(q)
        if norm:
            where.append("l.key_hash = %s"); params.append(security.key_hash(settings.key_pepper, norm))
        else:
            like = "%" + q.strip().replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_") + "%"
            where.append("(l.customer_email ILIKE %s OR l.key_hint ILIKE %s OR l.plan ILIKE %s)"); params += [like, q.strip().upper()[:5] + "%", like]
    cond = " AND ".join(where)
    total = conn.execute(f"SELECT count(*) FROM licenses l WHERE {cond}", tuple(params)).fetchone()[0]
    rows = _rows(conn, f"""SELECT l.license_id, l.key_hint || '-…' AS key_hint, l.app_id, a.name AS app_name, l.customer_email, l.plan, l.max_activations,
                                  l.expires_at, {_EFFECTIVE} AS status, l.created_at, l.kind, l.volume_seats,
                                  (SELECT count(*) FROM activations x WHERE x.license_id = l.license_id) AS activations
                           FROM licenses l JOIN apps a ON a.app_id = l.app_id WHERE {cond} ORDER BY l.created_at DESC, l.license_id LIMIT %s OFFSET %s""",
                 tuple(params) + (limit, offset))
    return {"total": total, "licenses": rows}


def license_detail(conn: psycopg.Connection, license_id: str) -> dict:
    license_id = _uuid(license_id, "license_not_found")
    lic = _rows(conn, f"""SELECT l.license_id, l.key_hint || '-…' AS key_hint, l.app_id, a.name AS app_name, l.customer_email, l.plan, l.max_activations, l.expires_at,
                                 {_EFFECTIVE} AS status, l.status_reason, l.metadata, l.kind, l.volume_seats, l.created_at, l.updated_at
                          FROM licenses l JOIN apps a ON a.app_id = l.app_id WHERE l.license_id = %s""", (license_id,))
    if not lic:
        raise LicenseError("license_not_found", 404)
    return {**lic[0],
            "activations": _rows(conn, "SELECT activation_id, left(device_hash, 12) AS device, device_name, host(ip_address) AS ip_address, activated_at, last_seen_at "
                                       "FROM activations WHERE license_id = %s ORDER BY activated_at", (license_id,)),
            "history": _rows(conn, "SELECT at, actor, action, host(ip_address) AS ip_address, detail FROM audit_events WHERE license_id = %s ORDER BY event_id DESC LIMIT 50", (license_id,))}


def update_license(conn: psycopg.Connection, license_id: str, fields: dict) -> dict:
    """Change a licence's plan, seats, expiry, e-mail or metadata, and/or its status (revoke, suspend, reinstate)."""
    license_id = _uuid(license_id, "license_not_found")
    row = conn.execute("SELECT status FROM licenses WHERE license_id = %s FOR UPDATE", (license_id,)).fetchone()
    if not row:
        raise LicenseError("license_not_found", 404)
    action = fields.pop("action", None)
    reason = fields.pop("reason", None)
    if row[0] == "revoked" and (action not in (None, "revoke") or fields):
        raise LicenseError("license_revoked_for_good", 409)
    sets, params, changed = [], [], {}
    for col in ("plan", "max_activations", "expires_at", "customer_email", "metadata"):
        if col in fields:
            v = fields[col]
            sets.append(f"{col} = %s"); params.append(json.dumps(v) if col == "metadata" else v)
            changed[col] = v.isoformat() if isinstance(v, datetime) else v
    if "max_activations" in fields:
        used = conn.execute("SELECT count(*) FROM activations WHERE license_id = %s", (license_id,)).fetchone()[0]
        if fields["max_activations"] < used:
            raise LicenseError("seats_in_use", 409, used=used)       # free seats first; a limit is never set below what is taken
    freed = 0
    if action:
        new = _TRANSITIONS[action]
        sets += ["status = %s", "status_reason = %s"]; params += [new, reason]
        if new == "revoked":
            freed = conn.execute("DELETE FROM activations WHERE license_id = %s", (license_id,)).rowcount
    elif "expires_at" in fields and row[0] == "expired":
        sets.append("status = 'active'")                             # a new date brings an expired licence back
    if sets:
        conn.execute(f"UPDATE licenses SET {', '.join(sets)}, updated_at = now() WHERE license_id = %s", tuple(params) + (license_id,))
        audit(conn, "admin", "license." + (action or "update"), license_id=license_id, reason=reason, seats_freed=freed, changed=changed)
    return license_detail(conn, license_id)


def remove_activation(conn: psycopg.Connection, activation_id: str) -> dict:
    activation_id = _uuid(activation_id, "activation_not_found")
    row = conn.execute("DELETE FROM activations WHERE activation_id = %s RETURNING license_id", (activation_id,)).fetchone()
    if not row:
        raise LicenseError("activation_not_found", 404)
    audit(conn, "admin", "activation.remove", license_id=row[0])
    return {"removed": True, "license_id": str(row[0])}


def audit_log(conn: psycopg.Connection, limit: int, offset: int) -> list[dict]:
    return _rows(conn, """SELECT e.event_id, e.at, e.actor, e.action, e.license_id, l.key_hint || '-…' AS key_hint, e.app_id, a.name AS app_name,
                                 host(e.ip_address) AS ip_address, e.detail
                          FROM audit_events e LEFT JOIN licenses l ON l.license_id = e.license_id LEFT JOIN apps a ON a.app_id = e.app_id
                          ORDER BY e.event_id DESC LIMIT %s OFFSET %s""", (limit, offset))
