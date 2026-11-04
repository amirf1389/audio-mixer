'use strict';
// The app's side of the 4-computer device lock: asks the vendor's license server (bridge/license-server) whether this computer may use the key.
// LICENSE_SERVER_URL is the server's address (https). Without it nothing is checked online and the app keeps its offline key check.
const TIMEOUT_MS = 8000;
const base = () => String(process.env.LICENSE_SERVER_URL || '').replace(/\/+$/, '');

async function call(route, body, fetchImpl = fetch) {
  const r = await fetchImpl(base() + route, { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify(body), signal: AbortSignal.timeout(TIMEOUT_MS) });
  const j = await r.json().catch(() => ({}));
  return { status: r.status, ...j };
}

// { ok: true, devices, limit } | { ok: false, status, message } (refused) | { offline: true } (server not reached)
async function activate(key, machine, fetchImpl) {
  if (!base()) return { ok: true, skipped: true };
  try {
    const r = await call('/activate', { key, machine }, fetchImpl);
    return r.ok ? { ok: true, devices: r.devices, limit: r.limit } : { ok: false, status: r.status, message: r.error || 'the license server refused this key' };
  } catch (e) { return { offline: true, message: String(e && e.message || e) }; }
}

// Frees this computer's seat. Best effort: a failure never blocks deactivating on this computer.
async function deactivate(key, machine, fetchImpl) {
  if (!base()) return { ok: true, skipped: true };
  try { await call('/deactivate', { key, machine }, fetchImpl); return { ok: true }; } catch (e) { return { offline: true }; }
}

module.exports = { activate, deactivate, base };
