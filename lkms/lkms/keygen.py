"""Makes the service's secrets on YOUR machine:  python -m lkms.keygen

Run it once, put the lines into .env on the server, and keep the admin key in a password manager.
Nothing is written to disk and nothing is sent anywhere.
"""
from __future__ import annotations

import base64
import secrets

from . import security


def main() -> None:
    admin = security.new_secret()
    signing = secrets.token_bytes(32)
    print("# --- for the server's .env ---")
    print("LKMS_ADMIN_KEY_HASH=" + security.sha256_hex(admin))
    print("LKMS_KEY_PEPPER=" + base64.b64encode(secrets.token_bytes(32)).decode())
    print("LKMS_SIGNING_KEY=" + base64.b64encode(signing).decode())
    print()
    print("# --- keep in your password manager; it is not stored on the server ---")
    print("ADMIN KEY: " + admin)
    print()
    print("# --- build into your apps (public, safe to ship) ---")
    print("LEASE PUBLIC KEY: " + security.public_key_b64(signing))


if __name__ == "__main__":
    main()
