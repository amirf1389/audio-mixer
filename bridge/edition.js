'use strict';
// PAID EDITIONS (AmF X1 Pro / AmF X1 Studio Master) with automatic activation: no key to type, no request to send.
//   The vendor makes an EDITION TOKEN in the Key Generator (BUILD EDITION TOKEN) for each copy sold:
//     "AMFXE1." + base64url(payload JSON) + "." + base64url(ECDSA P-256 / SHA-256 signature, IEEE-P1363), signed with the SAME vendor key as license keys
//     payload = { v: 1, kind: 'amfx1-edition', edition: 'pro' | 'studio-master', plan, serial: 'AMFX-PRO-XXXX-XXXX-XXXX', issued, order?, expires? }
//   scripts/build-edition.js refuses to build without a token that verifies with bridge/license-public.json, then puts it in the installer as
//   bridge/edition-token.json (never committed: .gitignore). On first start the local server verifies it here (offline) and records the activation
//   in ~/.audio-mixer/edition.json bound to this computer's machine ID, so the edition survives a Basic OTA update that replaces the installed files.
// Honest limits: the token is signed (it cannot be forged or changed, a different edition or a longer expiry breaks the signature), and an
// activation file copied to another computer does not work there (machine ID). The installer itself can still be copied: the serial in every
// token is random and unique per sale, so a leaked installer can be traced to its order. The private key never leaves the vendor's computer.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');

const PREFIX = 'AMFXE1', KIND = 'amfx1-edition';
const EDITIONS = {
  pro: { id: 'pro', plan: 'pro', name: 'AmF X1 Pro', short: 'PRO', serial: 'AMFX-PRO' },
  'studio-master': { id: 'studio-master', plan: 'master', name: 'AmF X1 Studio Master', short: 'STUDIO MASTER', serial: 'AMFX-SM' },
};
const ALPHABET = '23456789ABCDEFGHJKLMNPQRSTUVWXYZ';   // 32 symbols, no 0/O/1/I: read over the phone without mistakes
const SERIAL_RE = /^AMFX-(PRO|SM)-[2-9A-HJ-NP-Z]{4}-[2-9A-HJ-NP-Z]{4}-[2-9A-HJ-NP-Z]{4}$/;

const lic = () => require('./license');
const b64u = buf => Buffer.from(buf).toString('base64').replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
const unb64u = s => Buffer.from(String(s).replace(/-/g, '+').replace(/_/g, '/'), 'base64');

// a random per-copy serial: AMFX-PRO-XXXX-XXXX-XXXX (Pro) / AMFX-SM-XXXX-XXXX-XXXX (Studio Master); 60 random bits from the OS CSPRNG
function newSerial(edition, rnd = crypto.randomBytes) {
  const e = EDITIONS[edition]; if (!e) throw new Error('edition must be one of: ' + Object.keys(EDITIONS).join(', '));
  const b = rnd(12), s = Array.from(b, x => ALPHABET[x & 31]).join('');
  return `${e.serial}-${s.slice(0, 4)}-${s.slice(4, 8)}-${s.slice(8, 12)}`;
}

// vendor side (Key Generator, tests): sign a token with a private KeyObject. order = the shop's order number (optional), days = 0: no expiry
function signToken({ edition, privateKey, order = '', days = 0, now = Date.now(), serial } = {}) {
  const e = EDITIONS[edition]; if (!e) { const err = new Error('edition must be one of: ' + Object.keys(EDITIONS).join(', ')); err.status = 400; throw err; }
  const payload = { v: 1, kind: KIND, edition: e.id, plan: e.plan, serial: serial || newSerial(edition), issued: now };
  const o = String(order || '').replace(/[^\w.#/-]/g, '').slice(0, 40); if (o) payload.order = o;
  const d = Math.floor(Number(days) || 0); if (d > 0) payload.expires = now + d * 86400000;
  const body = b64u(Buffer.from(JSON.stringify(payload)));
  const sig = crypto.sign('sha256', Buffer.from(body), { key: privateKey, dsaEncoding: 'ieee-p1363' });
  return { token: `${PREFIX}.${body}.${b64u(sig)}`, payload };
}

// checks a token offline: shape, vendor signature, kind, edition <-> plan pairing, serial format, dates. jwk: trust only this key (tests, build check)
function verifyToken(token, { jwk = null, now = Date.now() } = {}) {
  const parts = String(token || '').trim().replace(/\s+/g, '').split('.');
  if (parts.length !== 3 || parts[0] !== PREFIX) return { ok: false, reason: 'malformed', message: 'This is not an AmF X1 edition token.' };
  const L = lic(), keys = jwk ? [jwk] : [L.PUBLIC_JWK, L.sandboxJwk()].filter(Boolean);   // vendor key only (the admin key cannot make editions); the sandbox key in sandbox mode
  let payload;
  try {
    if (!keys.some(k => crypto.verify('sha256', Buffer.from(parts[1]), { key: crypto.createPublicKey({ key: { kty: 'EC', crv: 'P-256', x: k.x, y: k.y }, format: 'jwk' }), dsaEncoding: 'ieee-p1363' }, unb64u(parts[2])))) throw new Error('bad');
    payload = JSON.parse(unb64u(parts[1]).toString('utf8'));
  } catch (_) { return { ok: false, reason: 'bad-signature', message: 'The edition token was not signed by the AmF X1 vendor key (changed or forged).' }; }
  const e = payload && EDITIONS[payload.edition];
  if (!payload || payload.v !== 1 || payload.kind !== KIND || !e || payload.plan !== e.plan) return { ok: false, reason: 'wrong-edition', message: 'The edition token names an unknown edition or the wrong plan for it.' };
  if (!SERIAL_RE.test(String(payload.serial || '')) || !String(payload.serial).startsWith(e.serial + '-')) return { ok: false, reason: 'bad-serial', message: 'The edition token has no valid serial number.' };
  if (payload.issued && payload.issued > now + 86400000) return { ok: false, reason: 'not-yet-valid', message: 'The edition token is not valid yet (check the date of this computer).' };
  if (payload.expires && payload.expires < now) return { ok: false, reason: 'expired', message: `The edition expired on ${new Date(payload.expires).toISOString().slice(0, 10)}.`, payload };
  return { ok: true, payload, edition: e, plan: payload.plan };
}

const shippedFile = () => process.env.AMFX_EDITION_TOKEN_FILE || path.join(__dirname, 'edition-token.json');
const activationFile = () => process.env.BRIDGE_EDITION_FILE || path.join(os.homedir(), '.audio-mixer', 'edition.json');
const readJson = f => { try { return JSON.parse(fs.readFileSync(f, 'utf8')); } catch (_) { return null; } };
const sameMachine = (a, b) => !!a && !!b && String(a).replace(/-/g, '') === String(b).replace(/-/g, '');

let logged = '';
function note(level, msg, data) {   // each distinct outcome once per process, not on every /api/license poll
  const k = level + msg; if (logged === k) return; logged = k;
  try { require('./diagnostics')[level]('license', msg, data); } catch (_) { /* diagnostics are optional here */ }
}

// The edition this computer runs, or null for Basic. Activates on first start: the shipped token is verified and recorded, bound to the machine.
//   { id, name, short, plan, serial, order, issued, expires, activatedAt, source: 'activated' | 'installer', activatedNow }
//   or { error: { reason, message } } when an edition build carries a token that does not verify (the app then runs Basic and says why)
function current({ machine, now = Date.now(), jwk = null, shipped = shippedFile(), file = activationFile(), write = true } = {}) {
  const view = (v, rec, source, activatedNow) => ({ id: v.edition.id, name: v.edition.name, short: v.edition.short, plan: v.plan, serial: v.payload.serial, order: v.payload.order || null,
    issued: v.payload.issued || null, expires: v.payload.expires || null, activatedAt: rec.activatedAt, source, activatedNow: !!activatedNow, boundToThisPc: true });
  const rec = readJson(file);
  if (rec && typeof rec.token === 'string') {
    if (!sameMachine(rec.machineId, machine)) note('warn', 'Edition activation file belongs to another computer: ignored', { serial: rec.serial || null });
    else { const v = verifyToken(rec.token, { jwk, now }); if (v.ok) return view(v, rec, 'activated'); note('warn', 'Edition activation no longer valid: ' + v.message, { reason: v.reason }); }
  }
  const s = readJson(shipped), token = s && typeof s.token === 'string' ? s.token : null;
  if (!token) return null;
  const v = verifyToken(token, { jwk, now });
  if (!v.ok) { note('error', 'Edition token in this installer did not verify: ' + v.message, { reason: v.reason }); return { error: { reason: v.reason, message: v.message } }; }
  const next = { token, machineId: machine, serial: v.payload.serial, edition: v.edition.id, activatedAt: now };
  if (write) {
    try { fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, JSON.stringify(next) + '\n', { mode: 0o600 }); }
    catch (e) { note('warn', 'Edition activated for this session only (could not save the activation: ' + e.message + ')'); return view(v, next, 'installer', true); }
  }
  note('info', `${v.edition.name} activated on this computer (serial ${v.payload.serial})`, { edition: v.edition.id, plan: v.plan });
  return view(v, next, 'installer', true);
}

// WhatsApp upgrade link (the app's UPGRADE TO PRO / STUDIO MASTER): the vendor's number and a prefilled message. Nothing is sent by the app itself:
// WhatsApp opens with the text, the customer presses send. Every field is cleaned and the whole text percent-encoded.
const VENDOR_WHATSAPP = '601120871185';
function upgradeText({ edition = 'pro', machineId = '', version = '', platform = '' } = {}) {
  const e = EDITIONS[edition] || EDITIONS.pro, c = (v, n) => String(v || '').replace(/[\u0000-\u001f<>]/g, ' ').trim().slice(0, n);
  return `Hello AMFB Audio, I would like to buy ${e.name}.\nEdition: ${e.short}\nMachine ID: ${c(machineId, 40) || '(not shown)'}\nVersion: ${c(version, 40)}\nPlatform: ${c(platform, 40)}`;
}
function upgradeUrl(opts = {}, number = VENDOR_WHATSAPP) {
  const n = String(number || '').replace(/\D/g, '');
  if (!/^\d{8,15}$/.test(n)) throw new Error('a WhatsApp number has 8 to 15 digits, with the country code');
  return `https://wa.me/${n}?text=${encodeURIComponent(upgradeText(opts))}`;
}

module.exports = { PREFIX, KIND, EDITIONS, SERIAL_RE, VENDOR_WHATSAPP, newSerial, signToken, verifyToken, current, shippedFile, activationFile, upgradeText, upgradeUrl };
