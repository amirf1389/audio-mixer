'use strict';
// Audio Mixer license server: issues signed PC license keys on request. It runs on your own server, never inside the app or the installers.
//   LICENSE_PRIVATE_KEY_FILE=/secure/path/vendor-private.pem  (the vendor private key, from `node scripts/license.js init`)
//   LICENSE_ADMIN_TOKEN=<long random secret>                  (every issue request must send it as "Authorization: Bearer <token>")
//   node license-server/server.js                             listens on 127.0.0.1:8787 (set LICENSE_HOST / LICENSE_PORT to change)
//
// The private key is read from a file, not from the repository and not from an environment variable. It is never returned, logged or stored.
// Keys use the same format and signature as scripts/license.js, so every installed copy that trusts the vendor public key accepts them.
const http = require('node:http');
const fs = require('node:fs');
const crypto = require('node:crypto');
const { PLANS, signKey } = require('../license');

const HOST = process.env.LICENSE_HOST || '127.0.0.1';
const PORT = Number(process.env.LICENSE_PORT || 8787);
const MAX_BODY = 4096;

function loadPrivateKey() {
  const file = process.env.LICENSE_PRIVATE_KEY_FILE;
  if (!file) throw new Error('LICENSE_PRIVATE_KEY_FILE is not set');
  const pem = fs.readFileSync(file, 'utf8');
  return crypto.createPrivateKey(pem);
}

// Checks the token in constant time, so the response time does not reveal how much of it matched.
function authorized(req, token) {
  const header = String(req.headers.authorization || '');
  if (!token || !header.startsWith('Bearer ')) return false;
  const given = Buffer.from(header.slice(7));
  const want = Buffer.from(token);
  return given.length === want.length && crypto.timingSafeEqual(given, want);
}

// Validates an issue request and builds the license payload. Returns { error } or { payload }.
function buildPayload(body, now = Date.now()) {
  const plan = String(body.plan || 'pro');
  if (!PLANS[plan]) return { error: 'unknown plan' };
  const name = String(body.name || '').trim().slice(0, 80);
  if (!name) return { error: 'name is required' };
  const days = body.days === undefined ? 365 : Number(body.days);
  if (!Number.isInteger(days) || days < 0 || days > 3650) return { error: 'days must be 0 to 3650 (0 = no expiry)' };
  const seats = body.seats === undefined ? 1 : Number(body.seats);
  if (!Number.isInteger(seats) || seats < 1 || seats > 100) return { error: 'seats must be 1 to 100' };
  const machine = body.machine ? String(body.machine).trim().toUpperCase() : null;
  if (machine !== null && !/^[A-Z0-9-]{8,64}$/.test(machine)) return { error: 'machine code format is not valid' };
  const payload = {
    v: 1, id: crypto.randomBytes(6).toString('hex').toUpperCase(), plan, name,
    issued: now, expires: days ? now + days * 86400000 : null, seats,
  };
  if (machine) payload.mid = machine;
  return { payload };
}

function send(res, status, obj) {
  res.writeHead(status, { 'content-type': 'application/json', 'cache-control': 'no-store' });
  res.end(JSON.stringify(obj));
}

function createServer({ privateKey, token }) {
  return http.createServer((req, res) => {
    if (req.method === 'GET' && req.url === '/health') return send(res, 200, { ok: true });
    if (req.method !== 'POST' || req.url !== '/issue') return send(res, 404, { ok: false, error: 'not found' });
    if (!authorized(req, token)) return send(res, 401, { ok: false, error: 'unauthorized' });
    let size = 0; const chunks = [];
    req.on('data', c => { size += c.length; if (size > MAX_BODY) { req.destroy(); } else chunks.push(c); });
    req.on('end', () => {
      if (size > MAX_BODY) return send(res, 413, { ok: false, error: 'request too large' });
      let body;
      try { body = JSON.parse(Buffer.concat(chunks).toString('utf8') || '{}'); } catch (_) { return send(res, 400, { ok: false, error: 'body must be JSON' }); }
      const built = buildPayload(body);
      if (built.error) return send(res, 400, { ok: false, error: built.error });
      const key = signKey(built.payload, privateKey);
      console.log(`issued ${built.payload.id} plan=${built.payload.plan} seats=${built.payload.seats}`);   // id only: never the key
      send(res, 200, { ok: true, id: built.payload.id, plan: built.payload.plan, expires: built.payload.expires, key });
    });
  });
}

module.exports = { buildPayload, createServer, authorized };

if (require.main === module) {
  const token = process.env.LICENSE_ADMIN_TOKEN;
  if (!token || token.length < 32) {
    console.error('LICENSE_ADMIN_TOKEN must be set to at least 32 characters.');
    process.exit(1);
  }
  const privateKey = loadPrivateKey();
  createServer({ privateKey, token }).listen(PORT, HOST, () => console.log(`license server on http://${HOST}:${PORT}`));
}
