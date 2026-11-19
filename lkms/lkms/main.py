"""The HTTP service:  uvicorn lkms.main:app --host 127.0.0.1 --port 8088   (behind your TLS reverse proxy)"""
from __future__ import annotations

import ipaddress
import logging
from contextlib import asynccontextmanager
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Literal

from fastapi import Depends, FastAPI, Header, Request
from fastapi.responses import JSONResponse, Response
from psycopg_pool import ConnectionPool
from pydantic import BaseModel, Field, field_validator

from . import security, service
from .config import Settings
from .ratelimit import RateLimiter
from .service import LicenseError

log = logging.getLogger("lkms")


# ---------------------------------------------------------------- request bodies

class AppIn(BaseModel):
    name: str = Field(min_length=1, max_length=120)


class LicensesIn(BaseModel):
    app_id: str
    count: int = Field(default=1, ge=1)
    plan: str = Field(default="standard", min_length=1, max_length=60)
    max_activations: int = Field(default=1, ge=1, le=100000)
    expires_at: datetime | None = None
    customer_email: str | None = Field(default=None, max_length=254, pattern=r"^[^@\s]+@[^@\s]+\.[^@\s]+$")
    metadata: dict[str, Any] = Field(default_factory=dict)

    @field_validator("expires_at")
    @classmethod
    def _future(cls, v: datetime | None) -> datetime | None:
        if v is None:
            return v
        if v.tzinfo is None:
            v = v.replace(tzinfo=timezone.utc)
        if v <= datetime.now(timezone.utc):
            raise ValueError("expires_at must be in the future")
        return v


class StatusIn(BaseModel):
    action: Literal["revoke", "suspend", "reinstate"] = "revoke"
    reason: str | None = Field(default=None, max_length=300)


class AppPatch(BaseModel):
    name: str | None = Field(default=None, min_length=1, max_length=120)
    status: Literal["active", "disabled"] | None = None


class LicensePatch(BaseModel):
    plan: str | None = Field(default=None, min_length=1, max_length=60)
    max_activations: int | None = Field(default=None, ge=1, le=100000)
    expires_at: datetime | None = None
    never_expires: bool = False                      # true clears the date (expires_at: null cannot be told from "not sent")
    customer_email: str | None = Field(default=None, max_length=254, pattern=r"^[^@\s]+@[^@\s]+\.[^@\s]+$")
    metadata: dict[str, Any] | None = None
    action: Literal["revoke", "suspend", "reinstate"] | None = None
    reason: str | None = Field(default=None, max_length=300)


class ClientIn(BaseModel):
    license_key: str = Field(min_length=1, max_length=64)
    device_id: str = Field(min_length=8, max_length=256)
    device_name: str | None = Field(default=None, max_length=120)
    timestamp: int
    nonce: str = Field(min_length=16, max_length=128)


# ---------------------------------------------------------------- the app

def create_app(settings: Settings | None = None) -> FastAPI:
    settings = settings or Settings.from_env()
    pool = ConnectionPool(settings.database_url, min_size=1, max_size=10, open=False)
    limiter = RateLimiter()

    @asynccontextmanager
    async def lifespan(_: FastAPI):
        pool.open(wait=True)
        yield
        pool.close()

    # no interactive docs in production: the API's shape is nobody else's business
    app = FastAPI(title="LKMS", docs_url=None, redoc_url=None, openapi_url=None, lifespan=lifespan)
    app.state.settings, app.state.limiter, app.state.pool = settings, limiter, pool

    def client_ip(request: Request) -> str:
        raw = request.client.host if request.client else ""
        if settings.trust_proxy:
            fwd = request.headers.get("x-forwarded-for", "")
            if fwd:
                raw = fwd.split(",")[-1].strip()         # the address our own proxy saw, not what the client claims
        try:
            return str(ipaddress.ip_address(raw))        # only a real address is kept (it goes into an inet column)
        except ValueError:
            return "0.0.0.0"

    @app.exception_handler(LicenseError)
    async def _refused(_: Request, exc: LicenseError):
        return JSONResponse(status_code=exc.http, content={"valid": False, "error": exc.code, **exc.extra})

    @app.middleware("http")
    async def _guard(request: Request, call_next):
        ip = client_ip(request)
        if int(request.headers.get("content-length") or 0) > 256_000:
            return JSONResponse(status_code=413, content={"error": "body_too_large"})
        # a ceiling for everything from one address, and an early stop for an address that keeps failing
        if not limiter.allow("ip:" + ip, 300, 60) or limiter.count("fail:" + ip, 900) >= 20:
            return JSONResponse(status_code=429, content={"error": "too_many_requests"}, headers={"Retry-After": "60"})
        response = await call_next(request)
        if response.status_code in (401, 404):           # a wrong secret or an unknown key: what guessing looks like
            limiter.allow("fail:" + ip, 10_000, 900)     # counted; after 20 in 15 minutes the address is refused for a while
        response.headers["Cache-Control"] = "no-store"
        response.headers["X-Content-Type-Options"] = "nosniff"
        return response

    # ---- who is calling

    def admin(authorization: str = Header(default="")) -> None:
        token = authorization[7:] if authorization.lower().startswith("bearer ") else ""
        if not security.same(security.sha256_hex(token), settings.admin_key_hash):
            raise LicenseError("admin_not_authorized", 401)

    def app_auth(request: Request, x_app_id: str = Header(default=""), x_app_secret: str = Header(default="")) -> service.App:
        with pool.connection() as conn:
            found = service.authenticate_app(conn, x_app_id, x_app_secret)
        if not limiter.allow("app:" + found.app_id, 6000, 60):
            raise LicenseError("too_many_requests", 429)
        return found

    # ---- admin

    @app.post("/admin/apps", status_code=201, dependencies=[Depends(admin)])
    def admin_create_app(body: AppIn):
        with pool.connection() as conn:
            return service.create_app(conn, body.name)

    @app.post("/admin/licenses", status_code=201, dependencies=[Depends(admin)])
    def admin_create_licenses(body: LicensesIn):
        if body.count > settings.max_bulk:
            raise LicenseError("too_many_at_once", 422, max=settings.max_bulk)
        with pool.connection() as conn:
            made = service.create_licenses(conn, settings, app_id=body.app_id, count=body.count, plan=body.plan, max_activations=body.max_activations,
                                           expires_at=body.expires_at, customer_email=body.customer_email, metadata=body.metadata)
        return {"count": len(made), "licenses": made}

    @app.patch("/admin/licenses/{key}/revoke", dependencies=[Depends(admin)])
    def admin_revoke(key: str, body: StatusIn | None = None):
        body = body or StatusIn()
        with pool.connection() as conn:
            return service.set_license_status(conn, settings, key, body.action, body.reason)

    @app.get("/admin/licenses", dependencies=[Depends(admin)])
    def admin_list(app_id: str | None = None, limit: int = 100, offset: int = 0):
        with pool.connection() as conn:
            return {"licenses": service.list_licenses(conn, app_id, max(1, min(limit, 500)), max(0, offset))}

    # ---- management (what the admin console uses)

    @app.get("/admin/overview", dependencies=[Depends(admin)])
    def admin_overview():
        with pool.connection() as conn:
            return service.overview(conn)

    @app.get("/admin/apps", dependencies=[Depends(admin)])
    def admin_apps():
        with pool.connection() as conn:
            return {"apps": service.list_apps(conn)}

    @app.patch("/admin/apps/{app_id}", dependencies=[Depends(admin)])
    def admin_update_app(app_id: str, body: AppPatch):
        with pool.connection() as conn:
            return service.update_app(conn, app_id, name=body.name, status=body.status)

    @app.post("/admin/apps/{app_id}/rotate-secret", dependencies=[Depends(admin)])
    def admin_rotate(app_id: str):
        with pool.connection() as conn:
            return service.rotate_app_secret(conn, app_id)

    @app.get("/admin/search/licenses", dependencies=[Depends(admin)])
    def admin_search(app_id: str | None = None, status: Literal["active", "suspended", "revoked", "expired"] | None = None,
                     q: str | None = None, limit: int = 50, offset: int = 0):
        with pool.connection() as conn:
            return service.search_licenses(conn, settings, app_id=app_id, status=status, q=q, limit=max(1, min(limit, 200)), offset=max(0, offset))

    @app.get("/admin/license/{license_id}", dependencies=[Depends(admin)])
    def admin_license(license_id: str):
        with pool.connection() as conn:
            return service.license_detail(conn, license_id)

    @app.patch("/admin/license/{license_id}", dependencies=[Depends(admin)])
    def admin_update_license(license_id: str, body: LicensePatch):
        fields = body.model_dump(exclude_unset=True)
        if fields.pop("never_expires", False):
            fields["expires_at"] = None
        elif "expires_at" in fields:
            when = fields["expires_at"]
            if when is None:
                fields.pop("expires_at")
            else:
                fields["expires_at"] = when if when.tzinfo else when.replace(tzinfo=timezone.utc)
        for k in ("plan", "max_activations", "customer_email", "metadata"):
            if k in fields and fields[k] is None:
                fields.pop(k)
        with pool.connection() as conn:
            return service.update_license(conn, license_id, fields)

    @app.delete("/admin/activations/{activation_id}", dependencies=[Depends(admin)])
    def admin_remove_activation(activation_id: str):
        with pool.connection() as conn:
            return service.remove_activation(conn, activation_id)

    @app.get("/admin/audit", dependencies=[Depends(admin)])
    def admin_audit(limit: int = 100, offset: int = 0):
        with pool.connection() as conn:
            return {"events": service.audit_log(conn, max(1, min(limit, 500)), max(0, offset))}

    # ---- the admin console's own files (only when switched on; see README)

    if settings.admin_ui:
        ui = Path(__file__).parent / "ui"
        types = {"index.html": "text/html; charset=utf-8", "app.js": "text/javascript; charset=utf-8", "style.css": "text/css; charset=utf-8"}

        @app.get("/manage")
        @app.get("/manage/{name}")
        def manage(name: str = "index.html"):
            if name not in types:
                raise LicenseError("not_found", 404)
            return Response((ui / name).read_bytes(), media_type=types[name], headers={
                # the page may load only its own script and style and talk only to this server; it cannot be framed
                "Content-Security-Policy": "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self' data:; "
                                           "base-uri 'none'; form-action 'none'; frame-ancestors 'none'",
                "X-Frame-Options": "DENY", "Referrer-Policy": "no-referrer"})

    # ---- clients

    @app.post("/v1/activate")
    def v1_activate(body: ClientIn, request: Request, caller: service.App = Depends(app_auth)):
        with pool.connection() as conn:
            service.use_nonce(conn, settings, caller, body.timestamp, body.nonce)
            return service.activate(conn, settings, caller, key=body.license_key, device_id=body.device_id, device_name=body.device_name,
                                    ip=client_ip(request), nonce=body.nonce)

    @app.post("/v1/validate")
    def v1_validate(body: ClientIn, request: Request, caller: service.App = Depends(app_auth)):
        with pool.connection() as conn:
            service.use_nonce(conn, settings, caller, body.timestamp, body.nonce)
            return service.validate(conn, settings, caller, key=body.license_key, device_id=body.device_id, ip=client_ip(request), nonce=body.nonce)

    @app.post("/v1/deactivate")
    def v1_deactivate(body: ClientIn, request: Request, caller: service.App = Depends(app_auth)):
        with pool.connection() as conn:
            service.use_nonce(conn, settings, caller, body.timestamp, body.nonce)
            return service.deactivate(conn, settings, caller, key=body.license_key, device_id=body.device_id, ip=client_ip(request))

    @app.get("/healthz")
    def healthz():
        with pool.connection() as conn:
            conn.execute("SELECT 1")
        return {"ok": True}

    return app


def app() -> FastAPI:      # uvicorn --factory lkms.main:app
    return create_app()
