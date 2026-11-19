"""Starts a throw-away PostgreSQL for the tests (its own data folder and port), loads the schema, and makes test secrets."""
from __future__ import annotations

import base64
import os
import secrets
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "sdk"))

PG_BIN = next((p for p in (Path("/usr/lib/postgresql/16/bin"), Path("/usr/lib/postgresql/15/bin"), Path("/usr/lib/postgresql/14/bin")) if p.exists()), None)
PORT = 54329


def _run(cmd: list[str], as_postgres: bool) -> None:
    if as_postgres:
        cmd = ["runuser", "-u", "postgres", "--"] + cmd       # PostgreSQL refuses to run as root
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)


@pytest.fixture(scope="session")
def database_url():
    if os.environ.get("LKMS_TEST_DATABASE_URL"):
        yield os.environ["LKMS_TEST_DATABASE_URL"]
        return
    if not PG_BIN:
        pytest.skip("no PostgreSQL server binaries found; set LKMS_TEST_DATABASE_URL")
    root = os.geteuid() == 0
    base = Path(tempfile.mkdtemp(prefix="lkms-pg-"))
    data = base / "data"
    if root:
        shutil.chown(base, "postgres", "postgres")
        os.chmod(base, 0o755)
    _run([str(PG_BIN / "initdb"), "-D", str(data), "-A", "trust", "-U", "lkms", "-E", "UTF8"], root)
    _run([str(PG_BIN / "pg_ctl"), "-D", str(data), "-w", "-o", f"-p {PORT} -c listen_addresses=127.0.0.1 -c unix_socket_directories={base}", "-l", str(base / "log"), "start"], root)
    url = f"postgresql://lkms@127.0.0.1:{PORT}/postgres"
    try:
        subprocess.run(["psql", url, "-v", "ON_ERROR_STOP=1", "-q", "-f", str(ROOT / "schema.sql")], check=True, stdout=subprocess.DEVNULL)
        yield url
    finally:
        _run([str(PG_BIN / "pg_ctl"), "-D", str(data), "-m", "immediate", "stop"], root)
        shutil.rmtree(base, ignore_errors=True)


@pytest.fixture()
def env(database_url):
    """A clean database and fresh secrets for each test. Returns (TestClient, admin headers, settings)."""
    import psycopg
    from fastapi.testclient import TestClient

    from lkms import security
    from lkms.config import Settings
    from lkms.main import create_app

    with psycopg.connect(database_url, autocommit=True) as c:
        c.execute("TRUNCATE audit_events, request_nonces, activations, licenses, apps CASCADE")
    admin_key = security.new_secret()
    settings = Settings(database_url=database_url, admin_key_hash=security.sha256_hex(admin_key), key_pepper=secrets.token_bytes(32),
                        signing_key=secrets.token_bytes(32), lease_hours=168, trust_proxy=False)
    with TestClient(create_app(settings)) as client:
        yield client, {"Authorization": "Bearer " + admin_key}, settings
