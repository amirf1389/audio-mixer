'use strict';
// Audio Mixer license server: issues signed PC license keys on request. It runs on your own server, never inside the app or the installers.
//   LICENSE_PRIVATE_KEY_FILE=/secure/path/vendor-private.pem  (the vendor private key, from `node scripts/license.js init`)
//   LICENSE_ADMIN_TOKEN=<long random secret>                  (every issue request must send it as "Authorization: Bearer <token>")
//   node license-server/server.js                             listens on 127.0.0.1:8787 (set LICENSE_HOST / LICENSE_PORT to change)
//   LICENSE_DATA_KEY_FILE=/secure/path/data.key               (optional: the secret the customer records are encrypted under; without it the admin token is used)
//   LICENSE_TRUST_PROXY=1                                     (behind nginx / Caddy on the same host: take the caller's address from X-Forwarded-For)
//
// LICENCE MANAGEMENT. Every key issued here is kept on record: its id, plan, dates, seats and the computers it runs on, and (encrypted with
// AES-256-GCM) the customer's name, e-mail and a note. The key itself is NOT kept: it is shown once, when it is issued. A key can be REVOKED:
// a revoked key cannot be activated on a computer any more and /status reports it. Customers use /portal/ (product key, key check, updates).
// The management page is at /admin/ of this server and
// needs the admin token; it is meant to be opened on the server itself or through an SSH tunnel, never published on the public website.
//   public (the app; the key is the proof)   POST /activate  POST /deactivate  POST /status   { key, machine }
//   admin (Authorization: Bearer <token>)    POST /issue | POST /admin/licenses        issue a key
//                                            GET  /admin/licenses                       every key on record, with its computers
//                                            POST /admin/licenses/:id/revoke | /restore { reason }
//                                            DELETE /admin/licenses/:id/devices/:machine   free one computer's seat
//                                            GET  /admin/stats   GET /admin/audit?limit=   GET /admin/audit/verify
//                                            GET|PUT /admin/website/state   POST /admin/website/publish   (the portal downloads)
//   public                                   GET /portal/downloads   GET /portal/download/:platform/:name   (LICENSE_RELEASES_DIR, default ../../releases)
//                                            GET|POST /admin/website/presets (staff)   GET /portal/presets (public: the published preset pack)
//
// The private key is read from a file, not from the repository and not from an environment variable. It is never returned, logged or stored.
// Keys use the same format and signature as scripts/license.js, so every installed copy that trusts the vendor public key accepts them.
const http = require('node:http');
const fs = require('node:fs');
const { createPortalAuth } = require('../portal-auth/auth');
const crypto = require('node:crypto');
const path = require('node:path');
const { PLANS, signKey, verifyKey, unb64u } = require('../license');
const { createApp, rateLimiter, bearerOk, HttpError, SECURITY_HEADERS } = require('./framework');

const HOST = process.env.LICENSE_HOST || '127.0.0.1';
const PORT = Number(process.env.LICENSE_PORT || 8787);
const MAX_BODY = 4096;
// One key runs on at most this many computers: keys are issued with at most this many seats, and activations past it are refused.
const MAX_DEVICES = 4;
// The device list of every key lives here, on the server's persistent volume (deploy/license/docker-compose.yml).
const DATA_FILE = process.env.LICENSE_DATA_FILE || '/data/licenses.json';
// The release files the customer portal offers for download (WEBSITE page): features.json, windows/*.exe|*.zip, android/*.apk.
const RELEASES_DIR = process.env.LICENSE_RELEASES_DIR || path.join(__dirname, '..', '..', 'releases');

function loadPrivateKey() {
  const file = process.env.LICENSE_PRIVATE_KEY_FILE;
  if (!file) throw new Error('LICENSE_PRIVATE_KEY_FILE is not set');
  // the key file must be readable by this server only (0600 or 0400): a key others on the machine can read is refused, not just warned about
  if (process.platform !== 'win32' && process.env.LICENSE_ALLOW_OPEN_KEY_FILE !== '1') {
    const mode = fs.statSync(file).mode & 0o077;
    if (mode) throw new Error('LICENSE_PRIVATE_KEY_FILE can be read by other users: chmod 600 it (or 400)');
  }
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
  // TRIAL: a key marked as a trial always ends (one year unless fewer days are asked for) and is for one computer
  // TERM: perpetual (never ends), annual (one year), trial (30 days, one computer), limited (the days asked for)
  const term = body.term === undefined ? (body.trial === true ? 'trial' : days ? 'limited' : 'perpetual') : String(body.term);
  if (!['perpetual', 'annual', 'trial', 'limited'].includes(term)) return { error: 'term must be perpetual, annual, trial or limited' };
  payload.term = term; if (term === 'perpetual') payload.expires = null; if (term === 'annual') payload.expires = now + 365 * 86400000; if (term === 'trial') { payload.seats = 1; payload.expires = now + 30 * 86400000; }
  if (body.trial === true) { payload.trial = true; payload.seats = 1; const d = days && days <= 365 ? days : 365; payload.expires = now + d * 86400000; }
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
  if (!Array.isArray(rec.devices)) rec.devices = [];
  if (rec.revoked) return { ok: false, status: 403, error: 'this key was revoked', id, revoked: true };
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

// ---- the customer's own details (name, e-mail, note) are kept encrypted: AES-256-GCM under a key derived with scrypt from the data secret and
// a random salt stored in the file. The file alone, copied off the server, does not give them away.
const keyCache = new Map();
// ── ACTIVATION (Windows-style): a product key, licence key or edition token + this computer's machine code -> an activation token bound to
// that computer, signed with the vendor key. The seat limit and revocation are enforced HERE; the app then runs offline for the whole term.
const fingerprintHex = pub => crypto.createHash('sha256').update(pub.x + '.' + pub.y).digest('hex').slice(0, 16).toUpperCase();
function signToken(prefix, payload, privateKey) {
  const body = Buffer.from(JSON.stringify(payload)).toString('base64').replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
  const sig = crypto.sign('sha256', Buffer.from(body), { key: privateKey, dsaEncoding: 'ieee-p1363' }).toString('base64').replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
  return `${prefix}.${body}.${sig}`;
}
const termOf = p => p.term || (p.trial ? 'trial' : p.expires ? 'limited' : 'perpetual');
function activationFor({ lid, plan, term, expires, machine, privateKey, now }) {
  const pub = crypto.createPublicKey(privateKey).export({ format: 'jwk' });
  return signToken('AMXA1', { v: 1, kind: 'amfx1-activation', kid: fingerprintHex(pub), lid, mid: machine, plan, term, iat: now, exp: expires || 0 }, privateKey);
}
// one entry point for all three: { code } (product key), { key } (licence key) or { token } (shipped edition token), plus { machine }
function activateOnline({ code, key, token, machine, file = DATA_FILE, secret, jwk, privateKey, now = Date.now() }) {
  const m = normMachine(machine); if (!/^[A-Z0-9-]{8,64}$/.test(m)) return { ok: false, status: 400, error: 'machine code format is not valid' };
  let p;
  if (code) { const r = redeemProductKey({ code, file, secret, now }); if (!r.ok) return r; p = r.payload; }
  else if (token) {
    const v = require('../edition').verifyToken(String(token).trim(), { jwk, now }); if (!v.ok) return { ok: false, status: 400, error: v.message || v.reason };
    p = { id: v.payload.serial, plan: v.payload.plan, seats: 1, expires: v.payload.expires || null, term: v.payload.expires ? 'limited' : 'perpetual' };
  } else { const v = verifyKey(String(key || '').trim(), { machine: m, jwk, now }); if (!v.ok) return { ok: false, status: v.reason === 'wrong-machine' ? 403 : 400, error: v.message || v.reason }; p = v.payload; }
  if (p.mid && p.mid.replace(/-/g, '') !== m.replace(/-/g, '')) return { ok: false, status: 403, error: 'this licence is bound to a different computer' };
  const reg = loadRegistry(file), rec = reg[p.id] || { devices: [] }; if (!Array.isArray(rec.devices)) rec.devices = [];
  if (rec.revoked) return { ok: false, status: 403, error: 'this licence was revoked', revoked: true };
  if (p.expires && p.expires < now) return { ok: false, status: 403, error: 'this licence has ended' };
  const limit = Math.min(p.seats || 1, MAX_DEVICES);
  if (!rec.devices.includes(m)) { if (rec.devices.length >= limit) return { ok: false, status: 409, error: `this licence is already active on ${limit} computer${limit > 1 ? 's' : ''}: deactivate one first`, devices: rec.devices.length, limit };
    rec.devices.push(m); reg[p.id] = rec; saveRegistry(file, reg); }
  return { ok: true, status: 200, id: p.id, plan: p.plan, term: termOf(p), devices: rec.devices.length, limit,
    activation: activationFor({ lid: p.id, plan: p.plan, term: termOf(p), expires: p.expires, machine: m, privateKey, now }) };
}
// a 30-day trial of STUDIO, once per computer (asking again returns the same end date; the record is the machine code's hash only)
function startTrial({ machine, file = DATA_FILE, privateKey, now = Date.now(), days = 30, plan = 'studio' }) {
  const m = normMachine(machine); if (!/^[A-Z0-9-]{8,64}$/.test(m)) return { ok: false, status: 400, error: 'machine code format is not valid' };
  const reg = loadRegistry(file), t = reg._trials || {}, h = crypto.createHash('sha256').update('trial:' + m).digest('hex').slice(0, 32);
  if (!t[h]) { t[h] = now; reg._trials = t; saveRegistry(file, reg); }
  const ends = t[h] + days * 86400000; if (ends < now) return { ok: false, status: 403, error: 'the trial on this computer has ended' };
  const lid = 'TRIAL-' + h.slice(0, 12).toUpperCase();
  if ((reg[lid] || {}).revoked) return { ok: false, status: 403, error: 'this trial was revoked' };
  return { ok: true, status: 200, id: lid, plan, term: 'trial', expires: ends, activation: activationFor({ lid, plan, term: 'trial', expires: ends, machine: m, privateKey, now }) };
}
// the revocation list every app keeps: every revoked licence id, signed, newest first by its issue time
function revocationList({ file = DATA_FILE, privateKey, now = Date.now() }) {
  const reg = loadRegistry(file), lids = Object.keys(reg).filter(k => !k.startsWith('_') && reg[k] && reg[k].revoked).sort();
  const pub = crypto.createPublicKey(privateKey).export({ format: 'jwk' });
  return signToken('AMXR1', { v: 1, kind: 'amfx1-revocations', kid: fingerprintHex(pub), issued: now, lids }, privateKey);
}

function dataKey(reg, secret) {
  if (!reg._salt) reg._salt = crypto.randomBytes(16).toString('hex');
  const id = reg._salt + ':' + crypto.createHash('sha256').update(String(secret)).digest('hex');
  if (!keyCache.has(id)) { if (keyCache.size > 8) keyCache.clear(); keyCache.set(id, crypto.scryptSync(String(secret), Buffer.from(reg._salt, 'hex'), 32, { N: 1 << 15, r: 8, p: 1, maxmem: 64 * 1024 * 1024 })); }
  return keyCache.get(id);
}
function seal(obj, key) {
  const iv = crypto.randomBytes(12), c = crypto.createCipheriv('aes-256-gcm', key, iv);
  const ct = Buffer.concat([c.update(JSON.stringify(obj), 'utf8'), c.final()]);
  return [iv, c.getAuthTag(), ct].map(b => b.toString('base64url')).join('.');
}
function unseal(text, key) {
  try {
    const [iv, tag, ct] = String(text).split('.').map(t => Buffer.from(t, 'base64url'));
    const d = crypto.createDecipheriv('aes-256-gcm', key, iv); d.setAuthTag(tag);
    return JSON.parse(Buffer.concat([d.update(ct), d.final()]).toString('utf8'));
  } catch (_) { return null; }   // another secret, or a changed file: the details are unreadable, the record itself stays usable
}
const ids = reg => Object.keys(reg).filter(k => !k.startsWith('_'));
const clean = (v, n) => String(v === undefined || v === null ? '' : v).replace(/[\u0000-\u001f\u007f]/g, ' ').trim().slice(0, n);

// PRODUCT KEY: the short form of a licence, as boxed software has it: 25 characters in five groups (XXXXX-XXXXX-XXXXX-XXXXX-XXXXX, no
// I, L, O or U). It is made with the licence and shown once; the register keeps only its SHA-256. The console sends it to /redeem and
// gets the full signed key, which it then checks offline like any other. 125 bits of chance: it cannot be guessed.
const CODE_ALPHABET = '0123456789ABCDEFGHJKMNPQRSTVWXYZ';
function newProductKey() { const b = crypto.randomBytes(25); let o = ''; for (let i = 0; i < 25; i++) { o += CODE_ALPHABET[b[i] & 31]; if (i % 5 === 4 && i < 24) o += '-'; } return o; }
const normProductKey = v => String(v || '').toUpperCase().replace(/[^0-9A-Z]/g, '').replace(/O/g, '0').replace(/[IL]/g, '1').replace(/U/g, 'V');
const productKeyHash = v => crypto.createHash('sha256').update(normProductKey(v)).digest('hex');
// Finds the licence a product key belongs to and builds its payload again (the same id, plan, dates and seats), ready to be signed.
function redeemProductKey({ code, file = DATA_FILE, secret, now = Date.now() }) {
  const n = normProductKey(code); if (n.length !== 25) return { ok: false, status: 400, error: 'a product key has 25 characters' };
  const reg = loadRegistry(file), h = productKeyHash(n), id = ids(reg).find(k => reg[k].code && crypto.timingSafeEqual(Buffer.from(reg[k].code, 'hex'), Buffer.from(h, 'hex')));
  if (!id) return { ok: false, status: 404, error: 'this product key is not known' };
  const rec = reg[id], m = rec.meta || {};
  if (rec.revoked) return { ok: false, status: 403, error: 'this licence was revoked' };
  if (m.expires && m.expires < now) return { ok: false, status: 403, error: 'this licence has ended' };
  const d = rec.enc ? unseal(rec.enc, dataKey(reg, secret)) : null;
  const payload = { v: 1, id, plan: m.plan, name: (d && d.name) || 'Licensed user', issued: m.issued, expires: m.expires || null, seats: m.seats || 1 };
  if (m.trial) payload.trial = true; if (m.mid) payload.mid = m.mid; if (m.term) payload.term = m.term;
  rec.redeems = (rec.redeems || 0) + 1; rec.redeemedAt = now; saveRegistry(file, reg);   // how often and when last: the management page shows it
  return { ok: true, status: 200, id, payload };
}

// A NEW PRODUCT KEY for a licence on record: the old one stops working at once (lost, or passed on by mistake). Shown once.
function renewProductKey({ id, file = DATA_FILE }) {
  const reg = loadRegistry(file), rec = reg[id]; if (!rec || String(id).startsWith('_')) return { ok: false, status: 404, error: 'no such licence' };
  const productKey = newProductKey(); rec.code = productKeyHash(productKey); rec.redeems = 0; rec.redeemedAt = null; saveRegistry(file, reg);
  return { ok: true, status: 200, id, productKey };
}

// Puts a newly issued key on record. The key itself is not stored.
function recordIssue({ payload, email, note, file = DATA_FILE, secret }) {
  const reg = loadRegistry(file), key = dataKey(reg, secret), rec = reg[payload.id] || { devices: [] };
  rec.meta = { plan: payload.plan, issued: payload.issued, expires: payload.expires, seats: payload.seats, mid: payload.mid || null, term: payload.term || null };
  if (payload.trial) rec.meta.trial = true;
  const productKey = rec.code ? null : newProductKey(); if (productKey) rec.code = productKeyHash(productKey);
  rec.enc = seal({ name: payload.name, email: clean(email, 120), note: clean(note, 200) }, key);
  rec.revoked = null; reg[payload.id] = rec; saveRegistry(file, reg);
  return { productKey };
}
function listLicenses({ file = DATA_FILE, secret, now = Date.now() }) {
  const reg = loadRegistry(file), key = ids(reg).length ? dataKey(reg, secret) : null;
  return ids(reg).map(id => {
    const r = reg[id] || {}, m = r.meta || {}, c = r.enc ? unseal(r.enc, key) : null;
    const state = r.revoked ? 'revoked' : (m.expires && m.expires < now ? 'expired' : 'active');
    return { id, plan: m.plan || null, issued: m.issued || null, expires: m.expires || null, seats: m.seats || null, machine: m.mid || null, devices: Array.isArray(r.devices) ? r.devices : [], productKey: !!r.code, redeems: r.redeems || 0, redeemedAt: r.redeemedAt || null, trial: !!m.trial,
      name: c ? c.name : '', email: c ? c.email : '', note: c ? c.note : '', readable: !r.enc || !!c, onRecord: !!r.meta, state, revoked: r.revoked || null, reason: r.reason || '' };
  }).sort((a, b) => (b.issued || 0) - (a.issued || 0));
}
function setRevoked({ id, revoked, reason, file = DATA_FILE, now = Date.now() }) {
  const reg = loadRegistry(file), rec = reg[id];
  if (!/^[0-9A-F]{12}$/.test(String(id)) || !rec) return { ok: false, status: 404, error: 'no such key on record' };
  rec.revoked = revoked ? now : null; rec.reason = revoked ? clean(reason, 200) : ''; saveRegistry(file, reg);
  return { ok: true, status: 200, id, revoked: rec.revoked };
}
function removeDevice({ id, machine, file = DATA_FILE }) {
  const reg = loadRegistry(file), rec = reg[id], m = normMachine(machine);
  if (!rec || !Array.isArray(rec.devices) || !rec.devices.includes(m)) return { ok: false, status: 404, error: 'that computer is not on this key' };
  rec.devices = rec.devices.filter(x => x !== m); saveRegistry(file, reg);
  return { ok: true, status: 200, id, devices: rec.devices.length };
}
// What a key is worth right now: genuine, its plan and expiry, whether it was revoked, how many computers hold it. The app may ask at any time.
function keyStatus({ key, machine, file = DATA_FILE, jwk, now = Date.now(), list = false }) {
  const m = normMachine(machine);
  let issued = now; try { issued = JSON.parse(unb64u(String(key || '').trim().split('.')[1]).toString('utf8')).issued || now; } catch (_) { /* refused below */ }
  const v = verifyKey(String(key || '').trim(), { machine: m || undefined, jwk, now: issued });   // the signature; expiry and revocation are reported, not refused
  if (!v.ok) return { ok: false, status: 400, error: v.message || v.reason };
  const p = v.payload, rec = loadRegistry(file)[p.id] || {};
  return { ok: true, status: 200, id: p.id, plan: p.plan, expires: p.expires || null, expired: !!(p.expires && p.expires < now), revoked: !!rec.revoked,
    devices: Array.isArray(rec.devices) ? rec.devices.length : 0, limit: Math.min(p.seats || 1, MAX_DEVICES), thisComputer: !!(m && Array.isArray(rec.devices) && rec.devices.includes(m)),
    // MY ACCOUNT (/account/): whoever holds the signed key may see the machine codes on it, to free one with /deactivate
    ...(list ? { computers: Array.isArray(rec.devices) ? rec.devices.slice(0, MAX_DEVICES) : [] } : {}) };
}

// ---- the audit trail: who did what, when, from where. One JSON line per action; each line carries the hash of the one before, so a line that
// was removed or changed shows. No key, token, name or e-mail is ever written to it.
function auditLog(file) {
  const path_ = file + '.audit.log';
  const read = () => { try { return fs.readFileSync(path_, 'utf8').split('\n').filter(Boolean).map(l => JSON.parse(l)); } catch (_) { return []; } };
  const hashOf = (prev, e) => crypto.createHash('sha256').update(prev + JSON.stringify([e.t, e.action, e.id, e.ip, e.detail])).digest('hex');
  return {
    add(action, id, ip, detail = '') {
      const all = read(), prev = all.length ? all[all.length - 1].h : '', e = { t: Date.now(), action, id: id || '', ip: ip || '', detail: clean(detail, 200) };
      e.h = hashOf(prev, e); fs.mkdirSync(path.dirname(path_), { recursive: true }); fs.appendFileSync(path_, JSON.stringify(e) + '\n', { mode: 0o600 });
    },
    list(limit) { const all = read(); return all.slice(-Math.max(1, Math.min(500, Number(limit) || 100))).reverse(); },
    verify() { let prev = ''; const all = read(); for (let i = 0; i < all.length; i++) { if (hashOf(prev, all[i]) !== all[i].h) return { ok: false, entries: all.length, brokenAt: i + 1 }; prev = all[i].h; } return { ok: true, entries: all.length }; },
  };
}

// ── WEBSITE: the downloads the customer portal shows. The release folder is only ever listed; a request names a file by its listed name. ──
const RELEASE_DIRS = { windows: /\.(exe|zip)$/i, android: /\.apk$/i };
const SAFE_NAME = /^[A-Za-z0-9][A-Za-z0-9 ()._+-]{0,159}$/;
const shaCache = new Map();   // name -> { size, mtime, sha256 }: an APK is hashed once, not on every request
function scanReleases(dir) {
  let versions = [];
  try { const j = JSON.parse(fs.readFileSync(path.join(dir, 'features.json'), 'utf8')); versions = (Array.isArray(j.features) ? j.features : []).filter(f => f && /^\d+(\.\d+){1,3}$/.test(String(f.version)))
    .map(f => ({ version: String(f.version), date: clean(f.date, 24), title: clean(f.title, 200), items: (Array.isArray(f.items) ? f.items : []).slice(0, 60).map(i => clean(i, 400)) })); } catch (_) { /* no feed: no versions */ }
  const files = [];
  for (const [platform, re] of Object.entries(RELEASE_DIRS)) {
    const sub = path.join(dir, platform), sums = {};
    try { for (const line of fs.readFileSync(path.join(sub, 'SHA256SUMS.txt'), 'utf8').split(/\r?\n/)) { const m = /^([0-9a-f]{64})\s+\*?(.+)$/i.exec(line.trim()); if (m) sums[m[2]] = m[1].toLowerCase(); } } catch (_) { /* no list: hashed below */ }
    let names = []; try { names = fs.readdirSync(sub); } catch (_) { continue; }
    for (const name of names.sort()) {
      if (!re.test(name) || !SAFE_NAME.test(name)) continue;
      let st; try { st = fs.statSync(path.join(sub, name)); } catch (_) { continue; } if (!st.isFile()) continue;
      const key = platform + '/' + name, hit = shaCache.get(key);
      let sha256 = hit && hit.size === st.size && hit.mtime === st.mtimeMs ? hit.sha256 : null;
      if (!sha256 && platform === 'windows' && sums[name]) sha256 = sums[name];   // the published list; the APK list is older than the APKs, so those are hashed
      if (!sha256) { const h = crypto.createHash('sha256'); const fd = fs.openSync(path.join(sub, name), 'r'), buf = Buffer.alloc(1 << 20); try { let n; while ((n = fs.readSync(fd, buf, 0, buf.length, null)) > 0) h.update(buf.subarray(0, n)); } finally { fs.closeSync(fd); } sha256 = h.digest('hex'); }
      shaCache.set(key, { size: st.size, mtime: st.mtimeMs, sha256 });
      const v = /(\d+\.\d+\.\d+\.\d+)/.exec(name);
      files.push({ name, platform, size: st.size, sha256, version: v ? v[1] : null });
    }
  }
  return { versions, files };
}
function loadWebsite(file) { try { const j = JSON.parse(fs.readFileSync(file, 'utf8')); return j && typeof j === 'object' ? j : {}; } catch (_) { return {}; } }
// Checks a website change against the listing. Returns { error } or { value }.
function checkWebsite(b, rel) {
  const version = String(b.version || '');
  if (!rel.versions.some(v => v.version === version)) return { error: 'that version is not in features.json' };
  if (b.hidden !== undefined && !Array.isArray(b.hidden)) return { error: 'hidden must be a list of file names' };
  const hidden = [...new Set((b.hidden || []).map(String))];
  if (hidden.length > 200) return { error: 'too many file names' };
  for (const n of hidden) if (!rel.files.some(f => f.name === n)) return { error: 'unknown file: ' + clean(n, 80) };
  const headline = String(b.headline == null ? '' : b.headline), note = String(b.note == null ? '' : b.note);
  if (headline.length > 120) return { error: 'the headline can have at most 120 characters' };
  if (note.length > 600) return { error: 'the note can have at most 600 characters' };
  return { value: { version, hidden, headline: clean(headline, 120), note: note.replace(/[\u0000-\u0009\u000b-\u001f\u007f]/g, ' ').trim().slice(0, 600) } };
}
// A PRESET PACK's shape (scripts/make-presets.js): {version, mastering:[{name, params}], effects:[{kind, id, name, values}], tone:[{kind, name}]}. Returns an error text or null.
function checkPresetPack(p) {
  if (!p || typeof p !== 'object' || Array.isArray(p)) return 'the pack must be a JSON object (releases/presets/presets-pack.json)';
  if (typeof p.version !== 'string' || !/^[0-9A-Za-z.+-]{1,32}$/.test(p.version)) return 'version: a short text such as 2026.11.20';
  const txt = (v, n) => typeof v === 'string' && v.length > 0 && v.length <= n && !/[\u0000-\u001f\u007f<>]/.test(v);
  const flat = o => o && typeof o === 'object' && !Array.isArray(o) && Object.keys(o).length <= 64 && Object.entries(o).every(([k, v]) => txt(k, 32) && (typeof v === 'number' ? Number.isFinite(v) : typeof v === 'boolean'));
  for (const k of ['mastering', 'effects', 'tone']) if (!Array.isArray(p[k]) || !p[k].length || p[k].length > 5000) return k + ': a list of 1 to 5000 presets';
  for (const m of p.mastering) if (!m || !txt(m.name, 40) || !flat(m.params)) return 'mastering: each {name, params of numbers}';
  for (const e of p.effects) if (!e || !txt(e.kind, 24) || !txt(e.name, 60) || !Number.isInteger(e.id) || !flat(e.values)) return 'effects: each {kind, id, name, values of numbers}';
  for (const t of p.tone) if (!t || !txt(t.kind, 24) || !txt(t.name, 60)) return 'tone: each {kind, name}';
  return null;
}
// What the portal shows: the published choice, or with nothing published yet the newest version with every file.
function publicDownloads(site, rel) {
  const pub = site.published || { version: rel.versions[0] ? rel.versions[0].version : null, hidden: [], headline: '', note: '' };
  const ver = rel.versions.find(v => v.version === pub.version) || null;
  const files = ver ? rel.files.filter(f => f.version === ver.version && !pub.hidden.includes(f.name))
    .map(f => ({ name: f.name, platform: f.platform, size: f.size, sha256: f.sha256, path: '/portal/download/' + f.platform + '/' + encodeURIComponent(f.name) })) : [];
  return { version: ver ? ver.version : null, date: ver ? ver.date : '', title: ver ? ver.title : '', headline: pub.headline || '', note: pub.note || '', files, publishedAt: site.publishedAt || null };
}

function createServer({ privateKey, token, dataFile = DATA_FILE, dataSecret, portalDb = process.env.PORTAL_DB, portalSecret = process.env.PORTAL_JWT_SECRET, trustProxy = process.env.LICENSE_TRUST_PROXY === '1', quiet = false, releasesDir = RELEASES_DIR, updateUrl = process.env.LICENSE_UPDATE_URL || 'https://raw.githubusercontent.com/amirf1389/audio-mixer/main/releases/update.json' }) {
  const jwk = crypto.createPublicKey(privateKey).export({ format: 'jwk' });   // checks the keys this server issued, for the device lock
  const secret = dataSecret || token, audit = auditLog(dataFile), limiter = rateLimiter(), started = Date.now();
  const app = createApp({ maxBody: MAX_BODY, trustProxy });
  const limit = (what, n) => c => { const r = limiter.allow(what + ':' + c.ip, n); if (!r.ok) throw new HttpError(429, 'too many requests', { retryAfter: r.retryAfter }); };
  // staff sign-in (bridge/portal-auth): one database and one secret shared with the OTA server, so one sign-in opens both dashboards
  let staffDb = null;   // opened on first use, so a server that never signs anyone in never creates the file
  const staffAuth = () => staffDb || (staffDb = createPortalAuth({ dbFile: portalDb || path.join(path.dirname(path.resolve(dataFile)), 'portal.db'),
    secret: portalSecret || crypto.createHmac('sha256', String(secret)).update('amfx1-portal-jwt').digest('hex'),
    crmSecret: process.env.PORTAL_CRM_SECRET || crypto.createHmac('sha256', String(secret)).update('amfx1-portal-crm').digest('hex') }));   // the CRM key never leaves this server
  const admin = c => {
    if (bearerOk(c.req, token)) { c.state.staff = null; return undefined; }
    const st = /^Bearer [A-Za-z0-9_-]+\.[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+$/.test(String(c.req.headers.authorization || '')) ? staffAuth().bearer(c.req) : null; if (st) { c.state.staff = st; return undefined; }
    const bad = limiter.allow('bad:' + c.ip, 10);                         // ten wrong tokens a minute from one address, then it has to wait
    if (limiter.allow('badlog:' + c.ip, 5).ok) audit.add('auth.denied', '', c.ip, c.req.method + ' ' + c.path);
    if (!bad.ok) throw new HttpError(429, 'too many requests', { retryAfter: bad.retryAfter });
    return c.json(401, { ok: false, error: 'unauthorized' }, { 'www-authenticate': 'Bearer' });
  };
  const out = (c, r) => { const { status, ...rest } = r; return c.json(status, rest); };

  app.get('/health', c => c.json(200, { ok: true }));
  // the app calls these with its key: no admin token, the key is the proof
  app.post('/activate', limit('pub', 30), async c => { const b = await c.body(); const r = activateDevice({ key: b.key, machine: b.machine, file: dataFile, jwk }); if (r.ok) audit.add('device.activate', r.id, c.ip); return out(c, r); });
  app.post('/deactivate', limit('pub', 30), async c => { const b = await c.body(); const r = deactivateDevice({ key: b.key, machine: b.machine, file: dataFile, jwk }); if (r.ok) audit.add('device.deactivate', r.id, c.ip); return out(c, r); });
  app.post('/status', limit('pub', 30), async c => { const b = await c.body(); return out(c, keyStatus({ key: b.key, machine: b.machine, file: dataFile, jwk, list: b.list === true })); });

  const issue = async c => {
    const body = await c.body(), built = buildPayload(body);
    if (built.error) return c.json(400, { ok: false, error: built.error });
    const key = signKey(built.payload, privateKey);
    const rec = recordIssue({ payload: built.payload, email: body.email, note: body.note, file: dataFile, secret }) || {};
    audit.add('license.issue', built.payload.id, c.ip, `${built.payload.plan} seats=${built.payload.seats}`);
    if (!quiet) console.log(`issued ${built.payload.id} plan=${built.payload.plan} seats=${built.payload.seats}`);   // id only: never the key
    return c.json(200, { ok: true, id: built.payload.id, plan: built.payload.plan, expires: built.payload.expires, key, productKey: rec.productKey || null });
  };
  // the console trades a product key for the signed key (public, rate limited; the key it returns is checked offline by the console)
  // Windows-style activation: product key / licence key / edition token + machine code -> an activation for that computer (seats enforced here)
  app.post('/activate-online', limit('pub', 30), async c => { const b = await c.body(); const r = activateOnline({ code: b.code, key: b.key, token: b.token, machine: b.machine, file: dataFile, secret, jwk, privateKey });
    audit.add(r.ok ? 'device.activate-online' : 'device.activate-online.refused', r.id || '-', c.ip, r.ok ? `${r.plan} ${r.term} ${r.devices}/${r.limit}` : r.error); return r.ok ? c.json(200, r) : c.json(r.status, { ok: false, error: r.error }); });
  app.post('/trial', limit('pub', 10), async c => { const b = await c.body(); const r = startTrial({ machine: b.machine, file: dataFile, privateKey });
    audit.add(r.ok ? 'trial.start' : 'trial.refused', r.id || '-', c.ip, r.ok ? '' : r.error); return r.ok ? c.json(200, r) : c.json(r.status, { ok: false, error: r.error }); });
  app.get('/revocations', limit('pub', 60), c => c.json(200, { ok: true, list: revocationList({ file: dataFile, privateKey }) }));
  app.post('/redeem', limit('pub', 30), async c => { const b = await c.body(); const r = redeemProductKey({ code: b.code, file: dataFile, secret });
    if (!r.ok) { audit.add('license.redeem.refused', '-', c.ip, r.error); return c.json(r.status, { ok: false, error: r.error }); }
    audit.add('license.redeem', r.id, c.ip, r.payload.plan); return c.json(200, { ok: true, id: r.id, plan: r.payload.plan, key: signKey(r.payload, privateKey) }); });
  app.post('/issue', admin, limit('issue', 60), issue);
  // ── staff sign-in, staff accounts and the CRM (every enabled staff account may edit) ──
  const who = c => c.state.staff ? c.state.staff.email : 'admin-token';
  app.post('/admin/auth/login', limit('login', 10), async c => {
    const b = await c.body(), r = staffAuth().login(b.email, b.password);
    audit.add(r.ok ? 'staff.login' : 'staff.login.refused', r.ok ? r.staff.id : '-', c.ip);
    return r.ok ? c.json(200, { ok: true, token: r.token, expiresIn: r.expiresIn, staff: r.staff }) : c.json(r.status, { ok: false, error: r.error });
  });
  app.get('/admin/auth/me', admin, c => c.json(200, { ok: true, staff: c.state.staff }));
  app.post('/admin/auth/logout', admin, c => { if (c.state.staff) { staffAuth().endSessions(c.state.staff.id); audit.add('staff.logout', c.state.staff.id, c.ip); } return c.json(200, { ok: true }); });   // ends every session of the account, not just this tab
  app.post('/admin/auth/password', admin, limit('adm', 120), async c => {
    if (!c.state.staff) return c.json(400, { ok: false, error: 'sign in with a staff account to change its password' });
    const b = await c.body(); if (!staffAuth().checkPassword(c.state.staff.id, b.current)) { audit.add('staff.password.refused', c.state.staff.id, c.ip); return c.json(403, { ok: false, error: 'the current password is wrong' }); }
    const r = staffAuth().setPassword(c.state.staff.id, b.password); if (r.ok) audit.add('staff.password', c.state.staff.id, c.ip); return out(c, { status: r.ok ? 200 : r.status, ...r });
  });
  app.get('/admin/staff', admin, limit('adm', 120), c => c.json(200, { ok: true, staff: staffAuth().listStaff() }));
  app.post('/admin/staff', admin, limit('adm', 120), async c => {
    const r = staffAuth().addStaff(await c.body()); if (r.ok) audit.add('staff.add', r.id, c.ip, 'by ' + who(c));
    return out(c, { status: r.ok ? 200 : r.status, ...r });
  });
  for (const [verb, off] of [['disable', true], ['enable', false]]) app.post('/admin/staff/:id/' + verb, admin, limit('adm', 120), c => {
    if (off && c.state.staff && c.state.staff.id === c.params.id) return c.json(400, { ok: false, error: 'you cannot disable your own account' });
    const r = staffAuth().setStaffDisabled(c.params.id, off); if (r.ok) audit.add('staff.' + verb, c.params.id, c.ip, 'by ' + who(c)); return out(c, { status: r.ok ? 200 : r.status, ...r });
  });
  // ── SYSTEM page: health and configuration (never a secret's value), settings, sessions, export ──
  app.get('/admin/system/info', admin, limit('adm', 120), c => {
    const l = listLicenses({ file: dataFile, secret }), m = process.memoryUsage(), fsize = f => { try { return fs.statSync(f).size; } catch (_) { return 0; } };
    return c.json(200, { ok: true,
      server: { node: process.version, platform: process.platform + ' ' + process.arch, upSince: started, uptimeS: Math.round((Date.now() - started) / 1000), memoryMB: Math.round(m.rss / 1048576), pid: process.pid },
      data: { licences: l.length, active: l.filter(x => x.state === 'active').length, devices: l.reduce((n, x) => n + x.devices.length, 0), licenceFileBytes: fsize(dataFile), auditFileBytes: fsize(dataFile + '.audit.log'), auditChain: audit.verify(), db: staffAuth().dbInfo() },
      config: { trustProxy: !!trustProxy, sharedSignIn: !!(portalDb && portalSecret), crmKey: process.env.PORTAL_CRM_SECRET ? 'PORTAL_CRM_SECRET' : 'derived from the licence data key', dataKey: dataSecret ? 'separate key file' : 'derived from the admin token', updateFeed: updateUrl, maxDevices: MAX_DEVICES, plans: Object.keys(PLANS) },
      you: c.state.staff ? { kind: 'staff', ...c.state.staff } : { kind: 'admin token' } });
  });
  app.get('/admin/settings', admin, limit('adm', 120), c => c.json(200, { ok: true, settings: staffAuth().getSettings(), schema: staffAuth().settingsSchema() }));
  app.put('/admin/settings', admin, limit('settings', 60), async c => { const r = staffAuth().setSettings(await c.body(), who(c)); if (r.ok && r.changed.length) audit.add('settings.change', '', c.ip, r.changed.join(',') + ' by ' + who(c)); return out(c, { status: r.ok ? 200 : r.status, ...r }); });
  app.post('/admin/staff/:id/end-sessions', admin, limit('adm', 120), c => { const ok = staffAuth().endSessions(c.params.id); if (ok) audit.add('staff.end-sessions', c.params.id, c.ip, 'by ' + who(c)); return ok ? c.json(200, { ok: true }) : c.json(404, { ok: false, error: 'no such account' }); });
  const cell = v => { let t = String(v == null ? '' : v); if (/^[=+\-@\t\r]/.test(t)) t = "'" + t; return '"' + t.replace(/"/g, '""') + '"'; };   // a cell a spreadsheet would run as a formula is quoted as text
  app.get('/admin/crm/export.csv', admin, limit('export', 20), c => {   // a spreadsheet of the customers
    const rows = staffAuth().listCustomers().map(x => [x.name, x.company, x.email, x.phone, x.country, x.tags, x.status, x.followUp, new Date(x.created).toISOString(), new Date(x.updated).toISOString()].map(cell).join(','));
    audit.add('crm.export', '', c.ip, rows.length + ' rows by ' + who(c));
    return c.send(200, '\ufeff' + ['name,company,email,phone,country,tags,status,follow-up,created,updated', ...rows].join('\r\n') + '\r\n', { 'content-type': 'text/csv; charset=utf-8', 'content-disposition': 'attachment; filename="amfx1-customers.csv"' });
  });
  app.get('/admin/crm/stats', admin, limit('adm', 120), c => c.json(200, { ok: true, stats: staffAuth().stats(), statuses: staffAuth().STATUSES }));
  app.get('/admin/crm/customers', admin, limit('adm', 120), c => c.json(200, { ok: true, customers: staffAuth().listCustomers({ q: c.query.get('q') || '', status: c.query.get('status') || '' }) }));
  app.post('/admin/crm/customers', admin, limit('adm', 120), async c => { const r = staffAuth().saveCustomer(await c.body(), who(c)); if (r.ok) audit.add('crm.add', r.id, c.ip, 'by ' + who(c)); return out(c, { status: r.ok ? 200 : r.status, ...r }); });
  app.get('/admin/crm/customers/:id', admin, limit('adm', 120), c => { const cu = staffAuth().getCustomer(c.params.id); if (!cu) return c.json(404, { ok: false, error: 'no such customer' });
    const keys = cu.email ? listLicenses({ file: dataFile, secret }).filter(l => String(l.email || '').toLowerCase() === cu.email).map(l => ({ id: l.id, plan: l.plan, issued: l.issued, expires: l.expires, revoked: !!l.revoked, state: l.state, trial: l.trial, seats: l.seats, devices: l.devices.length })) : [];
    return c.json(200, { ok: true, customer: { ...cu, licences: keys } }); });
  app.put('/admin/crm/customers/:id', admin, limit('adm', 120), async c => { const r = staffAuth().saveCustomer(await c.body(), who(c), c.params.id); if (r.ok) audit.add('crm.edit', r.id, c.ip, 'by ' + who(c)); return out(c, { status: r.ok ? 200 : r.status, ...r }); });
  app.delete('/admin/crm/customers/:id', admin, limit('adm', 120), c => { const r = staffAuth().deleteCustomer(c.params.id); if (r.ok) audit.add('crm.delete', c.params.id, c.ip, 'by ' + who(c)); return out(c, { status: r.ok ? 200 : r.status, ...r }); });
  // ISSUE A KEY FROM A CUSTOMER: the customer's name and e-mail, the plan / days / seats from Settings unless the form changes them
  app.post('/admin/crm/customers/:id/licence', admin, limit('issue', 60), async c => {
    const cu = staffAuth().getCustomer(c.params.id); if (!cu) return c.json(404, { ok: false, error: 'no such customer' });
    if (!cu.email) return c.json(400, { ok: false, error: 'the customer needs an e-mail first: the key is listed under it' });
    const b = await c.body(), st = staffAuth().getSettings();
    const body = { name: cu.company ? cu.name + ' · ' + cu.company : cu.name, email: cu.email, note: 'issued from the CRM by ' + who(c),
      plan: b.plan || st.defaultPlan, days: b.days === undefined ? st.defaultDays : b.days, seats: b.seats === undefined ? st.defaultSeats : b.seats, ...(b.term ? { term: b.term } : {}), ...(b.trial === true ? { trial: true } : {}) };
    const built = buildPayload(body); if (built.error) return c.json(400, { ok: false, error: built.error });
    const key = signKey(built.payload, privateKey), rec = recordIssue({ payload: built.payload, email: body.email, note: body.note, file: dataFile, secret }) || {};
    audit.add('license.issue', built.payload.id, c.ip, `${built.payload.plan} seats=${built.payload.seats} crm=${cu.id} by ${who(c)}`);
    staffAuth().addNote(cu.id, `Licence ${built.payload.id} issued: ${built.payload.plan.toUpperCase()}, ${built.payload.seats} computer(s), ${built.payload.expires ? 'ends ' + new Date(built.payload.expires).toISOString().slice(0, 10) : 'no end'}.`, who(c));
    return c.json(200, { ok: true, id: built.payload.id, plan: built.payload.plan, expires: built.payload.expires, seats: built.payload.seats, key, productKey: rec.productKey || null });
  });
  app.get('/admin/crm/due', admin, limit('adm', 120), c => c.json(200, { ok: true, ...staffAuth().due(String(c.query.get('today') || '')) }));   // today: the browser's own day
  app.post('/admin/crm/customers/:id/notes', admin, limit('adm', 120), async c => { const r = staffAuth().addNote(c.params.id, (await c.body()).text, who(c)); if (r.ok) audit.add('crm.note', c.params.id, c.ip, 'by ' + who(c)); return out(c, { status: r.ok ? 200 : r.status, ...r }); });

  app.post('/admin/licenses', admin, limit('adm', 120), issue);
  app.get('/admin/licenses', admin, limit('adm', 120), c => c.json(200, { ok: true, maxDevices: MAX_DEVICES, plans: Object.keys(PLANS), licenses: listLicenses({ file: dataFile, secret }) }));
  app.post('/admin/licenses/:id/revoke', admin, limit('adm', 120), async c => { const b = await c.body(); const r = setRevoked({ id: c.params.id, revoked: true, reason: b.reason, file: dataFile }); if (r.ok) audit.add('license.revoke', r.id, c.ip, b.reason); return out(c, r); });
  app.post('/admin/licenses/:id/restore', admin, limit('adm', 120), async c => { const r = setRevoked({ id: c.params.id, revoked: false, file: dataFile }); if (r.ok) audit.add('license.restore', r.id, c.ip); return out(c, r); });
  app.delete('/admin/licenses/:id/devices/:machine', admin, limit('adm', 120), c => { const r = removeDevice({ id: c.params.id, machine: c.params.machine, file: dataFile }); if (r.ok) audit.add('device.remove', r.id, c.ip); return out(c, r); });
  app.get('/admin/stats', admin, limit('adm', 120), c => {
    const l = listLicenses({ file: dataFile, secret }), by = s => l.filter(x => x.state === s).length, plans = {};
    for (const x of l) if (x.plan) plans[x.plan] = (plans[x.plan] || 0) + 1;
    return c.json(200, { ok: true, total: l.length, active: by('active'), expired: by('expired'), revoked: by('revoked'), devices: l.reduce((n, x) => n + x.devices.length, 0), plans,
      expiringSoon: l.filter(x => x.state === 'active' && x.expires && x.expires - Date.now() < 30 * 86400000).length, upSince: started, node: process.version });
  });
  app.get('/admin/licenses/ending.csv', admin, limit('export', 20), c => {   // ENDING SOON as a spreadsheet: active keys that end within the warning days, the nearest first
    const d = Number(c.query.get('days')), days = Number.isInteger(d) && d >= 1 && d <= 3650 ? d : staffAuth().getSettings().expiringWarnDays, now = Date.now();
    const soon = listLicenses({ file: dataFile, secret }).filter(l => l.state === 'active' && l.expires && l.expires - now < days * 86400000).sort((a, b) => a.expires - b.expires);
    const rows = soon.map(l => [l.id, l.name, l.email, String(l.plan || '').toUpperCase(), l.trial ? 'trial' : '', new Date(l.expires).toISOString().slice(0, 10), Math.max(0, Math.ceil((l.expires - now) / 86400000)), l.devices.length + '/' + (l.seats || 1)].map(cell).join(','));
    audit.add('license.export-ending', '', c.ip, rows.length + ' rows, ' + days + ' days by ' + who(c));
    return c.send(200, '\ufeff' + ['id,customer,email,plan,trial,ends,days left,computers', ...rows].join('\r\n') + '\r\n', { 'content-type': 'text/csv; charset=utf-8', 'content-disposition': 'attachment; filename="amfx1-ending-soon.csv"', 'cache-control': 'no-store' });
  });
  // BACKUP of the staff / CRM database (admin token or a signed-in staff account; audited). The CRM inside stays encrypted: its key is not in the file.
  app.get('/admin/system/backup', admin, limit('backup', 6), c => {
    const tmp = path.join(require('node:os').tmpdir(), 'amfx1-portal-backup-' + crypto.randomBytes(8).toString('hex') + '.db');
    try { const r = staffAuth().backup(tmp, who(c)); const data = fs.readFileSync(tmp); audit.add('system.backup', '', c.ip, data.length + ' bytes by ' + who(c));
      return c.send(200, data, { 'content-type': 'application/vnd.sqlite3', 'content-disposition': `attachment; filename="amfx1-portal-${new Date(r.at).toISOString().slice(0, 10)}.db"`, 'cache-control': 'no-store' });
    } finally { try { fs.unlinkSync(tmp); } catch (_) { /* already gone */ } }
  });
  app.get('/admin/audit', admin, limit('adm', 120), c => c.json(200, { ok: true, entries: audit.list(c.query.get('limit')) }));
  app.get('/admin/audit/verify', admin, limit('adm', 120), c => c.json(200, { ok: true, chain: audit.verify() }));
  // THE CUSTOMER PORTAL (/portal/): activate a product key, check a key, see the newest release. Public: it uses only /redeem, /status and
  // the route below, which reads the published update list (LICENSE_UPDATE_URL, https) for the page and keeps it for five minutes.
  const upd = { at: 0, body: null };
  // what the vendor wants every customer to see (SYSTEM → Settings): public by design, nothing else from the settings
  app.get('/portal/notice', limit('pub', 60), c => { const st = staffAuth().getSettings(); return c.json(200, { ok: true, orgName: st.orgName, supportEmail: st.supportEmail, notice: st.portalNotice, maintenance: !!st.maintenance }); });
  app.get('/portal/updates', limit('pub', 60), async c => {
    if (upd.body && Date.now() - upd.at < 300000) return c.json(200, upd.body);
    let body;
    try {
      const u = new URL(updateUrl);
      if (!(u.protocol === 'https:' || (u.protocol === 'http:' && (u.hostname === '127.0.0.1' || u.hostname === 'localhost')))) throw new Error('the update list must be an https address');
      const r = await fetch(u, { signal: AbortSignal.timeout(6000) }); if (!r.ok) throw new Error('the update list answered ' + r.status);
      const j = JSON.parse((await r.text()).slice(0, 262144)), p = typeof j.payload === 'string' ? JSON.parse(j.payload) : j, files = [];
      for (const [platform, f] of Object.entries(p.files || {})) if (f && typeof f === 'object') files.push({ platform: clean(platform, 40), name: clean(f.name || String(f.url || '').split('/').pop(), 120), url: clean(f.url, 400), sha256: clean(f.sha256, 64), size: Number.isFinite(Number(f.size)) ? Number(f.size) : null });
      body = { ok: true, version: clean(p.version, 32), channel: clean(p.channel, 24), released: clean(p.released, 24), notes: (Array.isArray(p.notes) ? p.notes : []).slice(0, 20).map(n => clean(n, 300)), files,
        features: (Array.isArray(p.features) ? p.features : []).slice(0, 8).map(f => ({ version: clean(f && f.version, 32), date: clean(f && f.date, 24), title: clean(f && f.title, 120), items: (Array.isArray(f && f.items) ? f.items : []).slice(0, 60).map(i => clean(i, 400)) })), signed: typeof j.signature === 'string' && j.signature.length > 40 };
    } catch (e) { body = { ok: false, error: 'The update list could not be read: ' + clean(e.message, 120) }; }
    upd.at = Date.now(); upd.body = body; return c.json(200, body);
  });
  // PORTAL MANAGEMENT (admin): what the customer portal is doing, and the two things an operator does to it
  app.get('/admin/portal', admin, limit('adm', 120), c => {
    const all = listLicenses({ file: dataFile, secret }), since = Date.now() - 86400000, log = audit.list(500);
    const when = e => Number(e.t) || 0, what = e => e.action || '';
    return c.json(200, { ok: true, path: '/portal/', updateUrl, update: upd.body, updateReadAt: upd.at || null,
      productKeys: { made: all.filter(l => l.productKey).length, redeemed: all.filter(l => l.redeems > 0).length, neverUsed: all.filter(l => l.productKey && !l.redeems).length },
      last24h: { redeemed: log.filter(e => what(e) === 'license.redeem' && when(e) >= since).length, refused: log.filter(e => what(e) === 'license.redeem.refused' && when(e) >= since).length } });
  });
  app.post('/admin/portal/refresh', admin, limit('refresh', 30), async c => { upd.at = 0; upd.body = null; audit.add('portal.refresh', '', c.ip); return c.json(200, { ok: true }); });
  // WEBSITE page (staff): which release the portal offers, which files, a headline and a note. Saved as a draft; PUBLISH makes it public.
  const siteFile = dataFile + '.website.json';
  const saveSite = s => { const tmp = siteFile + '.tmp'; fs.mkdirSync(path.dirname(siteFile), { recursive: true }); fs.writeFileSync(tmp, JSON.stringify(s, null, 1) + '\n', { mode: 0o600 }); fs.renameSync(tmp, siteFile); };
  app.get('/admin/website/state', admin, limit('adm', 120), c => { const rel = scanReleases(releasesDir), site = loadWebsite(siteFile);
    return c.json(200, { ok: true, releasesDir: path.resolve(releasesDir), versions: rel.versions.map(v => ({ version: v.version, date: v.date, title: v.title })), files: rel.files,
      draft: site.draft || site.published || { version: rel.versions[0] ? rel.versions[0].version : '', hidden: [], headline: '', note: '' }, published: site.published || null, publishedAt: site.publishedAt || null, publishedBy: site.publishedBy || null,
      updatedAt: site.updatedAt || null, updatedBy: site.updatedBy || null, live: publicDownloads(site, rel) }); });
  app.put('/admin/website/state', admin, limit('adm', 120), async c => { const r = checkWebsite(await c.body(), scanReleases(releasesDir)); if (r.error) return c.json(400, { ok: false, error: r.error });
    const site = loadWebsite(siteFile); site.draft = r.value; site.updatedAt = Date.now(); site.updatedBy = who(c); saveSite(site);
    audit.add('website.save', r.value.version, c.ip, 'hidden=' + r.value.hidden.length + ' by ' + who(c)); return c.json(200, { ok: true, draft: site.draft }); });
  app.post('/admin/website/publish', admin, limit('adm', 120), c => { const rel = scanReleases(releasesDir), site = loadWebsite(siteFile);
    if (!site.draft) return c.json(400, { ok: false, error: 'save the changes first' });
    const r = checkWebsite(site.draft, rel); if (r.error) return c.json(400, { ok: false, error: r.error });   // the folder may have changed since the save
    site.published = r.value; site.publishedAt = Date.now(); site.publishedBy = who(c); saveSite(site);
    audit.add('website.publish', r.value.version, c.ip, 'hidden=' + r.value.hidden.length + ' by ' + who(c)); return c.json(200, { ok: true, live: publicDownloads(site, rel) }); });
  // PRESETS (staff, on the WEBSITE page): publish a PRESET PACK (scripts/make-presets.js writes one from the console's code). GET /portal/presets
  // gives it out as published, byte for byte. It is NOT trusted for that: an app takes a downloaded pack only when its sha256 is in an update
  // manifest the owner signed (node scripts/make-update.js --prepare puts it in the pending manifest; the owner's signature makes it count).
  const presetFile = dataFile + '.presets.json', builtInPack = path.join(releasesDir, 'presets', 'presets-pack.json');
  const manifestPresets = (name, signed) => { try { let j = JSON.parse(fs.readFileSync(path.join(releasesDir, name), 'utf8')); if (signed) j = typeof j.payload === 'string' ? JSON.parse(j.payload) : null;
    return j && j.presets && /^[0-9a-f]{64}$/.test(String(j.presets.sha256)) ? { version: String(j.presets.version), sha256: j.presets.sha256 } : null; } catch (_) { return null; } };
  const signedPresets = () => ({ signed: manifestPresets('update.json', true), pending: manifestPresets('update.pending.json', false) });   // what the signed and the pending manifest say
  const packInfo = (text, extra = {}) => { const p = JSON.parse(text); return { version: p.version, sha256: crypto.createHash('sha256').update(text).digest('hex'), size: Buffer.byteLength(text),
    mastering: p.mastering.length, effects: p.effects.length, tone: p.tone.length, ...extra }; };
  const readText = f => { try { return fs.readFileSync(f, 'utf8'); } catch (_) { return null; } };
  app.get('/admin/website/presets', admin, limit('adm', 120), c => { const pub = readText(presetFile), built = readText(builtInPack); let meta = {}; try { meta = JSON.parse(readText(presetFile + '.meta') || '{}'); } catch (_) { /* none */ }
    const m = signedPresets(), live = pub ? packInfo(pub, meta) : built ? packInfo(built, { builtIn: true }) : null;
    return c.json(200, { ok: true, live, builtIn: built ? packInfo(built) : null, manifest: m, trusted: !!(live && m.signed && m.signed.sha256 === live.sha256) }); });
  app.post('/admin/website/presets', admin, limit('adm', 30), async c => { const b = await c.body(4 * 1024 * 1024);
    let pack = b.fromBuiltIn === true ? (() => { try { return JSON.parse(readText(builtInPack)); } catch (_) { return null; } })() : b.pack;
    const err = checkPresetPack(pack); if (err) return c.json(400, { ok: false, error: err });
    pack = { version: pack.version, generated: String(pack.generated || '').slice(0, 32), mastering: pack.mastering, effects: pack.effects, tone: pack.tone };
    const text = JSON.stringify(pack, null, 1) + '\n', tmp = presetFile + '.tmp';   // the same text scripts/make-presets.js writes, so the same pack has the same sha256
    fs.mkdirSync(path.dirname(presetFile), { recursive: true }); fs.writeFileSync(tmp, text, { mode: 0o600 }); fs.renameSync(tmp, presetFile);
    fs.writeFileSync(presetFile + '.meta', JSON.stringify({ publishedAt: Date.now(), publishedBy: who(c) }), { mode: 0o600 });
    const info = packInfo(text); audit.add('website.presets.publish', pack.version, c.ip, 'sha256=' + info.sha256 + ' by ' + who(c));
    const m = signedPresets(); return c.json(200, { ok: true, live: info, trusted: !!(m.signed && m.signed.sha256 === info.sha256) }); });
  app.get('/portal/presets', limit('pub', 60), c => { const text = readText(presetFile) || readText(builtInPack);
    if (!text) return c.json(404, { ok: false, error: 'no preset pack is published' });
    return c.send(200, text, { 'content-type': 'application/json', 'cache-control': 'public, max-age=300', 'x-preset-sha256': crypto.createHash('sha256').update(text).digest('hex') }); });
  // PUBLIC: the published release and its visible files; a file is given out only when it is on that list
  app.get('/portal/downloads', limit('pub', 60), c => c.json(200, { ok: true, ...publicDownloads(loadWebsite(siteFile), scanReleases(releasesDir)) }));
  app.get('/portal/download/:platform/:name', limit('pub', 30), c => {
    const f = publicDownloads(loadWebsite(siteFile), scanReleases(releasesDir)).files.find(x => x.platform === c.params.platform && x.name === c.params.name);
    if (!f) return c.json(404, { ok: false, error: 'not found' });
    const file = path.join(path.resolve(releasesDir), f.platform, f.name);
    c.res.writeHead(200, { ...SECURITY_HEADERS, 'cache-control': 'public, max-age=300', 'content-type': 'application/octet-stream', 'content-length': String(f.size), 'content-disposition': 'attachment; filename="' + f.name.replace(/"/g, '') + '"' });
    if (c.req.method === 'HEAD') { c.res.end(); return true; }
    fs.createReadStream(file).on('error', () => c.res.destroy()).pipe(c.res); return true;
  });
  app.post('/admin/licenses/:id/product-key', admin, limit('issue', 60), async c => { const r = renewProductKey({ id: c.params.id, file: dataFile });
    if (!r.ok) return c.json(r.status, { ok: false, error: r.error }); audit.add('license.productkey.new', r.id, c.ip); return c.json(200, { ok: true, id: r.id, productKey: r.productKey }); });
  const portal = path.join(__dirname, 'portal');
  app.get('/portal', c => c.send(301, '', { location: '/portal/' }));
  app.static({ '/portal/': [path.join(portal, 'index.html'), 'text/html; charset=utf-8'], '/portal/app.js': [path.join(portal, 'app.js'), 'text/javascript; charset=utf-8'], '/portal/portal.css': [path.join(portal, 'portal.css'), 'text/css; charset=utf-8'], '/portal/base.css': [path.join(__dirname, 'dashboard', 'style.css'), 'text/css; charset=utf-8'] });
  // MY ACCOUNT: the customer self-service page (management/customer): a key's plan, end date and computers, free a computer, a product
  // key traded for the licence key, the downloads. Public files; everything it shows comes from /status, /deactivate, /redeem and /portal/*
  const account = path.join(__dirname, '..', '..', 'management', 'customer');
  app.get('/account', c => c.send(301, '', { location: '/account/' }));
  let accountPage = null;   // one inline file (Tailwind Play CDN + its script): the CSP allows that script by its hash only
  app.get('/account/', c => { if (!accountPage) accountPage = require('../../management/inline-page').inlinePage(path.join(account, 'index.html'));
    return c.send(200, accountPage.body, { 'content-type': 'text/html; charset=utf-8', 'content-security-policy': accountPage.csp }); });
  // the management page: three fixed files; it asks for the token itself, so the files carry nothing secret
  const dash = path.join(__dirname, 'dashboard');
  // the staff pages load pinned CDN files (Tailwind Play CDN, Bootstrap, Font Awesome) checked by their SRI hash; each page gets a CSP naming exactly those hosts
  const CDN_CSP = {
    login: "default-src 'none'; script-src 'self' https://cdn.tailwindcss.com/3.4.16; style-src 'self' 'unsafe-inline' https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.5.2/; font-src https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.5.2/; connect-src 'self'; img-src 'self' data:; base-uri 'none'; form-action 'none'; frame-ancestors 'none'",
    crm: "default-src 'none'; script-src 'self' https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/; style-src 'self' https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/ https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.5.2/; font-src https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.5.2/; connect-src 'self'; img-src 'self' data:; base-uri 'none'; form-action 'none'; frame-ancestors 'none'",
  };
  const page = (file, csp) => c => { let data; try { data = fs.readFileSync(path.join(dash, file)); } catch (_) { throw new HttpError(404, 'not found'); } return c.send(200, data, { 'content-type': 'text/html; charset=utf-8', 'content-security-policy': csp }); };
  app.get('/admin/login', page('login.html', CDN_CSP.login));
  app.get('/admin/crm', page('crm.html', CDN_CSP.crm));
  app.get('/admin/system', page('system.html', CDN_CSP.crm));
  app.get('/admin/website', page('website.html', CDN_CSP.crm));
  app.static({ '/admin/login.js': [path.join(dash, 'login.js'), 'text/javascript; charset=utf-8'], '/admin/crm.js': [path.join(dash, 'crm.js'), 'text/javascript; charset=utf-8'], '/admin/system.js': [path.join(dash, 'system.js'), 'text/javascript; charset=utf-8'], '/admin/website.js': [path.join(dash, 'website.js'), 'text/javascript; charset=utf-8'] });
  app.get('/admin', c => c.send(301, '', { location: '/admin/' }));
  app.static({ '/admin/': [path.join(dash, 'index.html'), 'text/html; charset=utf-8'], '/admin/app.js': [path.join(dash, 'app.js'), 'text/javascript; charset=utf-8'], '/admin/style.css': [path.join(dash, 'style.css'), 'text/css; charset=utf-8'] });
  return http.createServer(app.handler);
}

module.exports = { loadPrivateKey, activateOnline, startTrial, revocationList, signToken, renewProductKey, newProductKey, normProductKey, redeemProductKey, buildPayload, createServer, authorized, activateDevice, deactivateDevice, keyStatus, listLicenses, setRevoked, removeDevice, recordIssue, MAX_DEVICES };

if (require.main === module) {
  const token = process.env.LICENSE_ADMIN_TOKEN;
  if (!token || token.length < 32) {
    console.error('LICENSE_ADMIN_TOKEN must be set to at least 32 characters.');
    process.exit(1);
  }
  let dataSecret; if (process.env.LICENSE_DATA_KEY_FILE) { dataSecret = fs.readFileSync(process.env.LICENSE_DATA_KEY_FILE, 'utf8').trim(); if (dataSecret.length < 32) { console.error('LICENSE_DATA_KEY_FILE must hold at least 32 characters.'); process.exit(1); } }
  const privateKey = loadPrivateKey();
  // the apps trust bridge/license-public.json only: a different key here issues keys no installed app accepts (fine for a test server only)
  const shipped = require('../license-public.json'), mine = crypto.createPublicKey(privateKey).export({ format: 'jwk' });
  if (shipped.x !== mine.x || shipped.y !== mine.y) console.warn('WARNING: this private key is not the one whose public key ships in the apps (bridge/license-public.json): keys issued here will not activate installed apps.');
  createServer({ privateKey, token, dataSecret }).listen(PORT, HOST, () => console.log(`license server on http://${HOST}:${PORT}  (management page: /admin/)`));
}
