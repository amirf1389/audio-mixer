#!/usr/bin/env python3
"""lkmsctl: manage the licence server from a terminal.

    export LKMS_URL=https://licence.example.com        # your server
    export LKMS_ADMIN_KEY=...                           # or leave it out and type it when asked (it is not echoed)

    lkmsctl overview
    lkmsctl apps
    lkmsctl app-add "Studio Desk"
    lkmsctl app-disable APP_ID | app-enable APP_ID | app-new-secret APP_ID
    lkmsctl keys-add --app APP_ID [--count 10] [--plan pro] [--devices 2] [--expires 2027-12-31] [--email a@b.c] [--csv keys.csv]
    lkmsctl keys-add --app APP_ID --site-seats 50       # a volume licence for a customer's site host
    lkmsctl find TEXT [--status active] [--app APP_ID]  # a whole key, the start of a key, an e-mail or a plan
    lkmsctl show LICENCE_ID
    lkmsctl suspend KEY | reinstate KEY | revoke KEY [--reason "refund"]
    lkmsctl remove-device ACTIVATION_ID
    lkmsctl activity [--limit 50]
"""
from __future__ import annotations

import argparse
import csv
import getpass
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request


def call(method: str, path: str, body: dict | None = None, **query) -> dict:
    url = os.environ.get("LKMS_URL", "").rstrip("/")
    if not url:
        sys.exit("Set LKMS_URL to your licence server, for example https://licence.example.com")
    key = os.environ.get("LKMS_ADMIN_KEY") or getpass.getpass("Admin key: ")
    os.environ["LKMS_ADMIN_KEY"] = key
    q = {k: v for k, v in query.items() if v not in (None, "")}
    req = urllib.request.Request(url + path + ("?" + urllib.parse.urlencode(q) if q else ""), method=method,
                                 data=json.dumps(body).encode() if body is not None else None,
                                 headers={"Authorization": "Bearer " + key, "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            return json.loads(r.read())
    except urllib.error.HTTPError as e:
        try:
            detail = json.loads(e.read())
        except Exception:  # noqa: BLE001
            detail = {}
        sys.exit(f"Refused ({e.code}): " + str(detail.get("error") or detail.get("detail") or "no reason given").replace("_", " "))
    except urllib.error.URLError as e:
        sys.exit(f"Cannot reach {url}: {e.reason}")


def table(rows: list[dict], cols: list[tuple[str, str]]) -> None:
    if not rows:
        print("(nothing)"); return
    cells = [[str(r.get(k) if r.get(k) is not None else "-") for k, _ in cols] for r in rows]
    widths = [max(len(t), *(len(c[i]) for c in cells)) for i, (_, t) in enumerate(cols)]
    print("  ".join(t.ljust(w) for (_, t), w in zip(cols, widths)))
    for c in cells:
        print("  ".join(v.ljust(w) for v, w in zip(c, widths)))


def main(argv: list[str] | None = None) -> None:
    p = argparse.ArgumentParser(prog="lkmsctl", description="Manage the licence server.")
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("overview"); sub.add_parser("apps")
    sub.add_parser("app-add").add_argument("name")
    for c in ("app-disable", "app-enable", "app-new-secret"):
        sub.add_parser(c).add_argument("app_id")
    k = sub.add_parser("keys-add")
    k.add_argument("--app", required=True); k.add_argument("--count", type=int, default=1); k.add_argument("--plan", default="standard")
    k.add_argument("--devices", type=int, default=1); k.add_argument("--expires"); k.add_argument("--email"); k.add_argument("--site-seats", type=int); k.add_argument("--csv")
    f = sub.add_parser("find"); f.add_argument("text", nargs="?", default=""); f.add_argument("--status"); f.add_argument("--app"); f.add_argument("--limit", type=int, default=50)
    sub.add_parser("show").add_argument("license_id")
    for c in ("suspend", "reinstate", "revoke"):
        s = sub.add_parser(c); s.add_argument("key"); s.add_argument("--reason")
    sub.add_parser("remove-device").add_argument("activation_id")
    sub.add_parser("activity").add_argument("--limit", type=int, default=50)
    a = p.parse_args(argv)

    if a.cmd == "overview":
        o = call("GET", "/admin/overview"); s = o["licenses_by_status"]
        print(f"Apps {o['apps']}   Licences {o['licenses']} (active {s['active']}, suspended {s['suspended']}, expired {s['expired']}, revoked {s['revoked']})")
        print(f"Activated devices {o['activations']}   seen in 24 hours {o['devices_seen_24h']}   licences expiring within 30 days {o['expiring_30_days']}")
    elif a.cmd == "apps":
        table(call("GET", "/admin/apps")["apps"], [("name", "NAME"), ("app_id", "APP ID"), ("status", "STATUS"), ("licenses", "LICENCES"), ("activations", "DEVICES")])
    elif a.cmd in ("app-add", "app-new-secret"):
        r = call("POST", "/admin/apps", {"name": a.name}) if a.cmd == "app-add" else call("POST", f"/admin/apps/{a.app_id}/rotate-secret")
        print(f"App: {r['name']}\napp_id:     {r['app_id']}\napi_secret: {r['api_secret']}\nSave the secret now: it is not shown again.")
    elif a.cmd in ("app-disable", "app-enable"):
        r = call("PATCH", f"/admin/apps/{a.app_id}", {"status": "disabled" if a.cmd == "app-disable" else "active"}); print(f"{r['name']}: {r['status']}")
    elif a.cmd == "keys-add":
        body = {"app_id": a.app, "count": a.count, "plan": a.plan, "max_activations": a.devices}
        if a.expires: body["expires_at"] = a.expires + "T23:59:59Z"
        if a.email: body["customer_email"] = a.email
        if a.site_seats: body["volume_seats"] = a.site_seats
        made = call("POST", "/admin/licenses", body)["licenses"]
        if a.csv:
            with open(a.csv, "w", newline="") as fh:
                w = csv.writer(fh); w.writerow(["license_key", "kind", "plan", "devices", "site_seats", "expires_at", "customer_email"])
                w.writerows([m["license_key"], m["kind"], m["plan"], m["max_activations"], m["volume_seats"] or "", m["expires_at"] or "", m["customer_email"] or ""] for m in made)
            os.chmod(a.csv, 0o600); print(f"{len(made)} keys written to {a.csv}. They are not shown again.")
        else:
            for m in made: print(m["license_key"])
            print(f"{len(made)} key(s) created. Save them now: they are not shown again.", file=sys.stderr)
    elif a.cmd == "find":
        r = call("GET", "/admin/search/licenses", q=a.text, status=a.status, app_id=a.app, limit=a.limit)
        for x in r["licenses"]:
            x["seats"] = f"{x['activations']} / {x['max_activations']}" + (f" (site: {x['volume_seats']} seats)" if x["kind"] == "volume" else "")
            x["expires"] = (x["expires_at"] or "never")[:10]
        table(r["licenses"], [("license_id", "LICENCE ID"), ("key_hint", "KEY"), ("app_name", "APP"), ("plan", "PLAN"), ("status", "STATUS"), ("seats", "DEVICES"), ("expires", "EXPIRES"), ("customer_email", "CUSTOMER")])
        print(f"{len(r['licenses'])} of {r['total']}")
    elif a.cmd == "show":
        d = call("GET", f"/admin/license/{a.license_id}")
        print(f"{d['key_hint']}  {d['app_name']}  plan {d['plan']}  {d['status']}" + (f"  reason: {d['status_reason']}" if d["status_reason"] else ""))
        print(f"kind {d['kind']}" + (f" ({d['volume_seats']} site seats)" if d["kind"] == "volume" else "") + f"   devices {len(d['activations'])} / {d['max_activations']}   expires {(d['expires_at'] or 'never')[:10]}   customer {d['customer_email'] or '-'}")
        table(d["activations"], [("activation_id", "ACTIVATION ID"), ("device_name", "DEVICE"), ("ip_address", "ADDRESS"), ("last_seen_at", "LAST SEEN")])
    elif a.cmd in ("suspend", "reinstate", "revoke"):
        r = call("PATCH", f"/admin/licenses/{urllib.parse.quote(a.key)}/revoke", {"action": a.cmd, "reason": a.reason}); print(f"Licence is now {r['status']}." + (f" {r['seats_freed']} device(s) removed." if r["seats_freed"] else ""))
    elif a.cmd == "remove-device":
        call("DELETE", f"/admin/activations/{a.activation_id}"); print("Device removed; its seat is free.")
    elif a.cmd == "activity":
        ev = call("GET", "/admin/audit", limit=a.limit)["events"]
        for e in ev: e["when"] = e["at"][:19].replace("T", " "); e["who"] = "app" if e["actor"].startswith("app:") else e["actor"]
        table(ev, [("when", "WHEN (UTC)"), ("action", "WHAT"), ("who", "WHO"), ("key_hint", "KEY"), ("app_name", "APP"), ("ip_address", "ADDRESS")])


if __name__ == "__main__":
    main()
