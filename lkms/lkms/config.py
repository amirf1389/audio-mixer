"""Settings, read once from the environment. The service refuses to start with a missing or weak secret."""
from __future__ import annotations

import base64
import os
from dataclasses import dataclass


class ConfigError(RuntimeError):
    pass


def _b64(name: str, size: int) -> bytes:
    raw = os.environ.get(name, "")
    try:
        data = base64.b64decode(raw, validate=True)
    except Exception as exc:  # noqa: BLE001
        raise ConfigError(f"{name} is not valid base64") from exc
    if len(data) != size:
        raise ConfigError(f"{name} must be {size} bytes (base64); make one with: python -m lkms.keygen")
    return data


@dataclass(frozen=True)
class Settings:
    database_url: str
    admin_key_hash: str      # hex SHA-256 of the admin key
    key_pepper: bytes        # 32 bytes
    signing_key: bytes       # 32 bytes, Ed25519 private key
    lease_hours: int
    trust_proxy: bool
    timestamp_skew_s: int = 300      # how far a request's clock may be from the server's
    max_bulk: int = 1000             # licences per admin request

    @staticmethod
    def from_env() -> "Settings":
        url = os.environ.get("LKMS_DATABASE_URL", "")
        if not url:
            raise ConfigError("LKMS_DATABASE_URL is not set")
        admin = os.environ.get("LKMS_ADMIN_KEY_HASH", "").strip().lower()
        if len(admin) != 64 or any(c not in "0123456789abcdef" for c in admin):
            raise ConfigError("LKMS_ADMIN_KEY_HASH must be the hex SHA-256 of the admin key")
        hours = int(os.environ.get("LKMS_LEASE_HOURS", "168"))
        if not 1 <= hours <= 24 * 90:
            raise ConfigError("LKMS_LEASE_HOURS must be between 1 and 2160")
        return Settings(
            database_url=url,
            admin_key_hash=admin,
            key_pepper=_b64("LKMS_KEY_PEPPER", 32),
            signing_key=_b64("LKMS_SIGNING_KEY", 32),
            lease_hours=hours,
            trust_proxy=os.environ.get("LKMS_TRUST_PROXY", "0") == "1",
        )
