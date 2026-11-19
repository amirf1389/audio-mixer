"""The two command-line tools, run against the live service."""
from __future__ import annotations

import json
import socket
import sys
import threading
import time
from pathlib import Path

import pytest
import uvicorn

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import lkms_activate  # noqa: E402
import lkmsctl  # noqa: E402
from lkms import security  # noqa: E402


@pytest.fixture()
def live(env, monkeypatch):
    client, admin, settings = env
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0)); port = s.getsockname()[1]
    server = uvicorn.Server(uvicorn.Config(client.app, host="127.0.0.1", port=port, log_level="error", lifespan="off"))
    t = threading.Thread(target=server.run, daemon=True); t.start()
    while not server.started: time.sleep(0.02)
    monkeypatch.setenv("LKMS_URL", f"http://127.0.0.1:{port}"); monkeypatch.setenv("LKMS_ADMIN_KEY", admin["Authorization"][7:])
    yield f"http://127.0.0.1:{port}", settings
    server.should_exit = True; t.join(5)


def test_the_admin_tool_and_the_activation_tool_work_together(live, capsys, tmp_path, monkeypatch):
    url, settings = live
    run = lambda *a: (lkmsctl.main(list(a)), capsys.readouterr())[1]  # noqa: E731
    out = run("app-add", "Studio Desk").out
    app_id = out.split("app_id:")[1].split()[0]; secret = out.split("api_secret:")[1].split()[0]
    assert "Studio Desk" in run("apps").out
    keys = run("keys-add", "--app", app_id, "--count", "2", "--plan", "pro", "--devices", "1", "--expires", "2031-01-01", "--email", "ana@example.com").out.split()
    assert len(keys) == 2 and all(len(k) == 29 for k in keys)
    csvf = tmp_path / "k.csv"
    assert "1 keys written" in run("keys-add", "--app", app_id, "--site-seats", "50", "--csv", str(csvf)).out
    assert "volume" in csvf.read_text() and csvf.stat().st_mode & 0o077 == 0
    found = run("find", "ana@").out
    assert "2 of 2" in found and "pro" in found and "2031-01-01" in found and keys[0] not in found
    assert "site: 50 seats" in run("find", "--status", "active").out
    lic_id = run("find", keys[0]).out.splitlines()[1].split()[0]
    # the customer's computer
    cfg = tmp_path / "app.json"
    cfg.write_text(json.dumps({"base_url": url, "app_id": app_id, "app_secret": secret, "public_key": security.public_key_b64(settings.signing_key),
                               "cache_path": str(tmp_path / "lic.json"), "device_id": "this-computer-01"}))
    act = lambda *a: (lkms_activate.main(["--config", str(cfg), *a]), capsys.readouterr().out)  # noqa: E731
    assert act("status") == (1, "This computer is not activated.\n")
    code, text = act("activate", keys[0]); assert code == 0 and "Licensed." in text and "Plan: pro." in text and "7 more day" in text
    assert act("status")[0] == 0
    shown = run("show", lic_id).out
    assert "devices 1 / 1" in shown and "ana@example.com" in shown
    assert "Licence is now suspended." in run("suspend", keys[0], "--reason", "late").out
    assert act("status") == (1, "This licence is suspended.\n")
    assert "active" in run("reinstate", keys[0]).out and act("status")[0] == 0
    assert act("deactivate") == (0, "This computer's seat was handed back.\n")
    assert "1 device(s) removed" not in run("revoke", keys[0]).out and act("activate", keys[0]) == (1, "This licence was revoked.\n")
    assert "Licences 3 (active 2, suspended 0, expired 0, revoked 1)" in run("overview").out
    assert "license.revoke" in run("activity", "--limit", "5").out
    with pytest.raises(SystemExit) as e:
        lkmsctl.main(["show", "11111111-1111-1111-1111-111111111111"])
    assert "license not found" in str(e.value)
    monkeypatch.setenv("LKMS_ADMIN_KEY", "wrong")
    with pytest.raises(SystemExit) as e:
        lkmsctl.main(["apps"])
    assert "Refused (401)" in str(e.value)
