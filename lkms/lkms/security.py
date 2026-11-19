"""Keys, hashes and signed leases. Nothing here touches the database."""
from __future__ import annotations

import base64
import hashlib
import hmac
import json
import secrets
import time

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey

# Crockford base32 without I, L, O, U: no letters that are read as digits or as each other.
_ALPHABET = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"
KEY_GROUPS, GROUP_LEN = 5, 5          # 25 symbols x 5 bits = 125 bits of randomness


def new_license_key() -> str:
    """A fresh licence key such as 7K2QD-0XWMA-9F3TZ-B6HNC-R41JE, from the system's secure random source."""
    symbols = [secrets.choice(_ALPHABET) for _ in range(KEY_GROUPS * GROUP_LEN)]
    return "-".join("".join(symbols[i:i + GROUP_LEN]) for i in range(0, len(symbols), GROUP_LEN))


def normalize_key(key: str) -> str | None:
    """What the customer typed, tidied (case, spaces, look-alike letters). None when it cannot be a key."""
    s = key.strip().upper().replace(" ", "").replace("-", "")
    s = s.replace("O", "0").replace("I", "1").replace("L", "1")
    if len(s) != KEY_GROUPS * GROUP_LEN or any(c not in _ALPHABET for c in s):
        return None
    return "-".join(s[i:i + GROUP_LEN] for i in range(0, len(s), GROUP_LEN))


def key_hash(pepper: bytes, normalized_key: str) -> str:
    """How a licence key is stored and looked up. Without the pepper a database copy cannot be searched for a key."""
    return hmac.new(pepper, normalized_key.encode(), hashlib.sha256).hexdigest()


def new_secret() -> str:
    """An app secret or admin key: 256 random bits. Random secrets need no slow password hash; SHA-256 is enough."""
    return secrets.token_urlsafe(32)


def sha256_hex(value: str) -> str:
    return hashlib.sha256(value.encode()).hexdigest()


def same(a: str, b: str) -> bool:
    """Comparison that takes the same time whether or not the values match."""
    return hmac.compare_digest(a.encode(), b.encode())


def _b64u(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode()


def _unb64u(text: str) -> bytes:
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


def public_key_b64(signing_key: bytes) -> str:
    from cryptography.hazmat.primitives import serialization
    pub = Ed25519PrivateKey.from_private_bytes(signing_key).public_key()
    return base64.b64encode(pub.public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)).decode()


def sign_lease(signing_key: bytes, payload: dict) -> str:
    """A lease the app can check by itself: <payload>.<Ed25519 signature>, both base64url."""
    body = _b64u(json.dumps(payload, separators=(",", ":"), sort_keys=True).encode())
    sig = Ed25519PrivateKey.from_private_bytes(signing_key).sign(body.encode())
    return body + "." + _b64u(sig)


def verify_lease(public_key_b64_text: str, lease: str, now: float | None = None) -> dict | None:
    """The lease's payload when the signature is the server's and it has not run out; otherwise None."""
    try:
        body, sig = lease.split(".")
        Ed25519PublicKey.from_public_bytes(base64.b64decode(public_key_b64_text)).verify(_unb64u(sig), body.encode())
        payload = json.loads(_unb64u(body))
    except (ValueError, InvalidSignature, TypeError):
        return None
    if not isinstance(payload, dict) or float(payload.get("exp", 0)) < (time.time() if now is None else now):
        return None
    return payload
