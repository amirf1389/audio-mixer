'use strict';
// A second factor for the vendor's Key Generator: a TOTP (RFC 6238, HMAC-SHA1, 30 s, 6 digits) from an authenticator app, on top of the PIN.
//   setup(dir, pin)         -> { secret (base32), uri (otpauth://), file }: the secret is stored in <dir>/totp.json ENCRYPTED WITH THE PIN (the same
//                             AES-256-GCM / scrypt box as the private key), so the file alone is useless, and the PIN alone is not enough to unlock.
//   confirm(dir, pin, code) -> activates the pending secret once the authenticator shows a right code (so a mistyped secret cannot lock you out).
//   verify(dir, pin, code)  -> true / throws 401 'wrong code'. Accepts the step before and after (clock drift); a code is accepted once (no replay).
//   isSet(dir)              -> a confirmed second factor exists. remove(dir, pin) deletes it (needs the PIN).
// The native console (engine/cpp/app/license/crypto.cpp) implements the same TOTP and is checked against the same RFC vectors.
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const vault = require('./license-vault');

const FILE = 'totp.json', STEP = 30, DIGITS = 6, WINDOW = 1;
const B32 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ234567';
function base32(buf) { let bits = 0, acc = 0, out = ''; for (const b of buf) { acc = (acc << 8) | b; bits += 8; while (bits >= 5) { bits -= 5; out += B32[(acc >> bits) & 31]; } } if (bits) out += B32[(acc << (5 - bits)) & 31]; return out; }
function unbase32(s) { let bits = 0, acc = 0; const out = []; for (const ch of String(s).toUpperCase().replace(/[^A-Z2-7]/g, '')) { acc = (acc << 5) | B32.indexOf(ch); bits += 5; if (bits >= 8) { bits -= 8; out.push((acc >> bits) & 255); } } return Buffer.from(out); }

function hotp(secret, counter, digits = DIGITS) {
  const c = Buffer.alloc(8); c.writeBigUInt64BE(BigInt(counter));
  const h = crypto.createHmac('sha1', secret).update(c).digest(); const o = h[19] & 15;
  const bin = ((h[o] & 0x7f) << 24) | (h[o + 1] << 16) | (h[o + 2] << 8) | h[o + 3];
  return bin % 10 ** digits;
}
const totp = (secret, unixSeconds, digits = DIGITS, step = STEP) => hotp(secret, Math.floor(unixSeconds / step), digits);
const pad = (n, d = DIGITS) => String(n).padStart(d, '0');

const file = dir => path.join(dir, FILE);
const read = dir => { try { return JSON.parse(fs.readFileSync(file(dir), 'utf8')); } catch (_) { return null; } };
const write = (dir, rec) => { fs.mkdirSync(dir, { recursive: true, mode: 0o700 }); fs.writeFileSync(file(dir), JSON.stringify(rec) + '\n', { mode: 0o600 }); };
const isSet = dir => { const r = read(dir); return !!(r && r.v === 1 && r.confirmed && r.box); };

function setup(dir, pin, { issuer = 'AmF X1 Key Generator', account = 'vendor', rnd = crypto.randomBytes } = {}) {
  vault.checkPin(pin);
  if (isSet(dir)) { const e = new Error('a second factor is already set up: remove it first (needs the PIN)'); e.status = 409; throw e; }
  const secret = base32(rnd(20));
  write(dir, { v: 1, confirmed: false, box: vault.encrypt(secret, pin), last: 0, at: Date.now() });
  return { secret, uri: `otpauth://totp/${encodeURIComponent(issuer)}:${encodeURIComponent(account)}?secret=${secret}&issuer=${encodeURIComponent(issuer)}&algorithm=SHA1&digits=${DIGITS}&period=${STEP}`, file: file(dir) };
}
function secretOf(dir, pin) { const r = read(dir); if (!r || r.v !== 1 || !r.box) { const e = new Error('no second factor is set up'); e.status = 404; throw e; } return { rec: r, secret: unbase32(vault.decrypt(r.box, pin)) }; }
// the code is right for now-1, now or now+1 steps and newer than the last accepted one
function check(secret, rec, code, now) {
  const want = String(code || '').replace(/\s+/g, ''); if (!/^\d{6}$/.test(want)) { const e = new Error('the authenticator code is 6 digits'); e.status = 400; throw e; }
  const t = Math.floor(now / 1000 / STEP);
  for (let w = -WINDOW; w <= WINDOW; ++w) { const c = t + w; if (c > (rec.last || 0) && pad(hotp(secret, c)) === want) return c; }
  const e = new Error('wrong authenticator code'); e.status = 401; e.code = 'WRONG_CODE'; throw e;
}
function confirm(dir, pin, code, now = Date.now()) {
  const { rec, secret } = secretOf(dir, pin); const c = check(secret, rec, code, now);
  write(dir, { ...rec, confirmed: true, last: c }); return true;
}
function verify(dir, pin, code, now = Date.now()) {
  if (!isSet(dir)) return true;   // nothing set up: the PIN alone decides
  const { rec, secret } = secretOf(dir, pin); const c = check(secret, rec, code, now);
  write(dir, { ...rec, last: c }); return true;
}
function remove(dir, pin) { secretOf(dir, pin); fs.unlinkSync(file(dir)); return true; }

module.exports = { FILE, STEP, DIGITS, base32, unbase32, hotp, totp, pad, setup, confirm, verify, remove, isSet, file };
