#!/usr/bin/env python3
"""lkms-activate: activate, check or release the licence of THIS computer from a terminal (for support and for setups).

It reads a small settings file your app installs (JSON):
    {"base_url": "https://licence.example.com", "app_id": "...", "app_secret": "...", "public_key": "...", "cache_path": "~/.myapp/licence.json"}
On a customer site with a site host, base_url is the host's address and the key is the word SITE.

    lkms-activate --config app-licence.json activate 7K2QD-0XWMA-9F3TZ-B6HNC-R41JE
    lkms-activate --config app-licence.json status
    lkms-activate --config app-licence.json deactivate
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "sdk"))
from lkms_client import LicenseClient  # noqa: E402

SAY = {"": "Licensed.", "not_activated": "This computer is not activated.", "offline_no_lease": "The licence server cannot be reached and this computer has no saved licence.",
       "lease_expired": "The saved licence has run out and the licence server cannot be reached.", "clock_moved_back": "The computer's clock was set back; correct it and try again.",
       "activation_limit_reached": "Every seat of this licence is in use. Deactivate another computer first.", "invalid_license": "That key is not valid for this app.",
       "license_revoked": "This licence was revoked.", "license_suspended": "This licence is suspended.", "license_expired": "This licence has expired.",
       "device_not_activated": "This computer is not activated for that licence.", "bad_server_signature": "The answer did not come from the real licence server. Nothing was changed.",
       "use_site_host": "This is a site licence: point the app at your site host and use the word SITE.", "app_not_authorized": "This app is not accepted by the licence server."}


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(prog="lkms-activate")
    p.add_argument("--config", required=True)
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("activate").add_argument("key"); sub.add_parser("status"); sub.add_parser("deactivate")
    a = p.parse_args(argv)
    cfg = json.loads(Path(a.config).read_text())
    c = LicenseClient(base_url=cfg["base_url"], app_id=cfg["app_id"], app_secret=cfg["app_secret"], public_key=cfg["public_key"], cache_path=cfg["cache_path"], device_id=cfg.get("device_id"))
    r = c.activate(a.key) if a.cmd == "activate" else c.check() if a.cmd == "status" else c.deactivate()
    if a.cmd == "deactivate" and r.ok:
        print("This computer's seat was handed back."); return 0
    print(SAY.get(r.reason, r.reason.replace("_", " ") + "."))
    if r.ok:
        left = (r.lease_expires_at - time.time()) / 86400 if r.lease_expires_at else 0
        print(f"Plan: {r.plan}.  " + ("Checked with the server just now." if r.source == "server" else "Running from the saved licence (server not reachable).") + f"  Works offline for {left:.0f} more day(s).")
    return 0 if r.ok else 1


if __name__ == "__main__":
    sys.exit(main())
