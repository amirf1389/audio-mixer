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
const path = require('node:path');
const { PLANS, signKey, verifyKey, unb64u } = require('../license');

const HOST = process.env.LICENSE_HOST || '127.0.0.1';
const PORT = Number(process.env.LICENSE_PORT || 8787);
const MAX_BODY = 4096;
// One key runs on at most this many computers: keys are issued with at most this many seats, and activations past it are refused.
const MAX_DEVICES = 4;
// The device list of every key lives here, on the server's persistent volume (deploy/license/docker-compose.yml).
const DATA_FILE = process.env.LICENSE_DATA_FILE || '/data/licenses.json';

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
  if (!Number.isInteger(seats) || seats < 1 || seats > MAX_DEVICES) return { error: `seats must be 1 to ${MAX_DEVICES}` };
  const machine = body.machine ? String(body.machine).trim().toUpperCase() : null;
  if (machine !== null && !/^[A-Z0-9-]{8,64}$/.test(machine)) return { error: 'machine code format is not valid' };
  const payload = {
    v: 1, id: crypto.randomBytes(6).toString('hex').toUpperCase(), plan, name,
    issued: now, expires: days ? now + days * 86400000 : null, seats,
  };
  if (machine) payload.mid = machine;
  return { payload };
}

// The device registry: { [key id]: { devices: [machine code, ...] } }. Only this process writes it, one request at a time (the file is replaced whole).
function loadRegistry(file) {
  try { const j = JSON.parse(fs.readFileSync(file, 'utf8')); return j && typeof j === 'object' ? j : {}; } catch (_) { return {}; }
}
function saveRegistry(file, reg) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  const tmp = file + '.tmp';
  fs.writeFileSync(tmp, JSON.stringify(reg) + '\n', { mode: 0o600 });
  fs.renameSync(tmp, file);
}
const normMachine = m => String(m || '').trim().toUpperCase();

// Registers a computer on a key. The key must be genuine (signed by the vendor) and not expired; a computer already on the list is fine again.
function activateDevice({ key, machine, file = DATA_FILE, jwk, now = Date.now() }) {
  const m = normMachine(machine);
  if (!/^[A-Z0-9-]{8,64}$/.test(m)) return { ok: false, status: 400, error: 'machine code format is not valid' };
  const v = verifyKey(String(key || '').trim(), { machine: m, jwk, now });
  if (!v.ok) return { ok: false, status: v.reason === 'wrong-machine' ? 403 : 400, error: v.message || v.reason };
  const id = v.payload.id, limit = Math.min(v.payload.seats || 1, MAX_DEVICES);
  const reg = loadRegistry(file), rec = reg[id] || { devices: [] };
  if (!rec.devices.includes(m)) {
    if (rec.devices.length >= limit) return { ok: false, status: 409, error: `this key is already used on ${limit} computers: deactivate one first`, devices: rec.devices.length, limit };
    rec.devices.push(m); reg[id] = rec; saveRegistry(file, reg);
  }
  return { ok: true, status: 200, id, plan: v.payload.plan, devices: rec.devices.length, limit };
}

// Frees the computer's seat. An expired key still frees its seat, so the list never gets stuck.
function deactivateDevice({ key, machine, file = DATA_FILE, jwk }) {
  const m = normMachine(machine);
  // checked as of the day it was issued: the signature is verified, the expiry is not
  let issued = Date.now();
  try { issued = JSON.parse(unb64u(String(key || '').trim().split('.')[1]).toString('utf8')).issued || issued; } catch (_) { /* verifyKey below refuses it */ }
  const v = verifyKey(String(key || '').trim(), { machine: m, jwk, now: issued });
  if (!v.ok) return { ok: false, status: v.reason === 'wrong-machine' ? 403 : 400, error: v.message || v.reason };
  const id = v.payload.id, reg = loadRegistry(file), rec = reg[id];
  if (rec && rec.devices.includes(m)) { rec.devices = rec.devices.filter(x => x !== m); reg[id] = rec; saveRegistry(file, reg); }
  return { ok: true, status: 200, id, devices: rec ? rec.devices.length : 0, limit: Math.min(v.payload.seats || 1, MAX_DEVICES) };
}

function send(res, status, obj) {
  res.writeHead(status, { 'content-type': 'application/json', 'cache-control': 'no-store' });
  res.end(JSON.stringify(obj));
}

function createServer({ privateKey, token, dataFile = DATA_FILE }) {
  const jwk = crypto.createPublicKey(privateKey).export({ format: 'jwk' });   // checks the keys this server issued, for the device lock
  return http.createServer((req, res) => {
    if (req.method === 'GET' && req.url === '/health') return send(res, 200, { ok: true });
    if (req.method === 'POST' && (req.url === '/activate' || req.url === '/deactivate')) {   // the app calls these with its key: no admin token, the key is the proof
      let size = 0; const chunks = [];
      req.on('data', c => { size += c.length; if (size > MAX_BODY) req.destroy(); else chunks.push(c); });
      return req.on('end', () => {
        if (size > MAX_BODY) return send(res, 413, { ok: false, error: 'request too large' });
        let body; try { body = JSON.parse(Buffer.concat(chunks).toString('utf8') || '{}'); } catch (_) { return send(res, 400, { ok: false, error: 'body must be JSON' }); }
        const fn = req.url === '/activate' ? activateDevice : deactivateDevice;
        const r = fn({ key: body.key, machine: body.machine, file: dataFile, jwk });
        const { status, ...out } = r;
        return send(res, status, out);
      });
    }
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

module.exports = { buildPayload, createServer, authorized, activateDevice, deactivateDevice, MAX_DEVICES };

if (require.main === module) {
  const token = process.env.LICENSE_ADMIN_TOKEN;
  if (!token || token.length < 32) {
    console.error('LICENSE_ADMIN_TOKEN must be set to at least 32 characters.');
    process.exit(1);
  }
  const privateKey = loadPrivateKey();
  createServer({ privateKey, token }).listen(PORT, HOST, () => console.log(`license server on http://${HOST}:${PORT}`));
}
