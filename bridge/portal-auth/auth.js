'use strict';
// Staff sign-in, JWT and the CRM for the vendor portals (licence server and OTA server). No packages: node:sqlite and node:crypto.
//   - staff: e-mail + password (scrypt, per-user salt); every enabled staff member may edit everything (the vendor's choice)
//   - JWT: HS256 only, 8 hours, issuer "amfx1-portal"; "alg" other than HS256 (and "none") is refused; a disabled account's tokens stop working
//   - CRM: customers and their notes. Name, e-mail, phone, company and notes are stored encrypted (AES-256-GCM); the e-mail is also kept as a
//     keyed hash so a customer can be found by e-mail without decrypting every row. Only status and timestamps are in the clear.
// Both servers point at the same database file (PORTAL_DB) and the same secret (PORTAL_JWT_SECRET), so one sign-in works on both.
const crypto = require('node:crypto');
const path = require('node:path');
const fs = require('node:fs');

const ISS = 'amfx1-portal', TTL_S = 8 * 3600, STATUSES = ['lead', 'trial', 'customer', 'reseller', 'lost'];
const b64u = b => Buffer.from(b).toString('base64url');

function openSqlite(file) {
  const prev = process.emitWarning; process.emitWarning = () => {};   // node:sqlite announces itself as experimental once: not an error of ours
  try { const { DatabaseSync } = require('node:sqlite'); return new DatabaseSync(file); } finally { process.emitWarning = prev; }
}

function createPortalAuth({ dbFile, secret, crmSecret = '' }) {
  if (!secret || String(secret).length < 32) throw new Error('PORTAL_JWT_SECRET must be at least 32 characters');
  if (dbFile !== ':memory:') fs.mkdirSync(path.dirname(dbFile), { recursive: true, mode: 0o700 });
  const db = openSqlite(dbFile);
  if (dbFile !== ':memory:') try { fs.chmodSync(dbFile, 0o600); } catch (_) { /* not every file system has modes */ }
  db.exec(`PRAGMA journal_mode = WAL; PRAGMA foreign_keys = ON;
    CREATE TABLE IF NOT EXISTS staff (id TEXT PRIMARY KEY, email TEXT UNIQUE NOT NULL, name TEXT NOT NULL, salt TEXT NOT NULL, hash TEXT NOT NULL,
      created INTEGER NOT NULL, disabled INTEGER NOT NULL DEFAULT 0, last_login INTEGER, token_gen INTEGER NOT NULL DEFAULT 0);
    CREATE TABLE IF NOT EXISTS customers (id TEXT PRIMARY KEY, email_hash TEXT, data TEXT NOT NULL, status TEXT NOT NULL, created INTEGER NOT NULL, updated INTEGER NOT NULL, updated_by TEXT);
    CREATE INDEX IF NOT EXISTS customers_email ON customers (email_hash);
    CREATE TABLE IF NOT EXISTS customer_notes (id TEXT PRIMARY KEY, customer_id TEXT NOT NULL REFERENCES customers(id) ON DELETE CASCADE, data TEXT NOT NULL, at INTEGER NOT NULL, by TEXT NOT NULL);`);
  const keyOf = label => crypto.createHmac('sha256', String(secret)).update(label).digest();
  const jwtKey = keyOf('jwt-hs256');
  // the CRM has its own secret, held only by the licence server: the OTA server shares the sign-in but can never read customers
  const crmKeyOf = label => { if (!crmSecret || String(crmSecret).length < 32) throw Object.assign(new Error('the CRM is not available on this server'), { status: 404 }); return crypto.createHmac('sha256', String(crmSecret)).update(label).digest(); };
  let encKeyC = null, idxKeyC = null; const encKeyF = () => encKeyC || (encKeyC = crmKeyOf('crm-aes-256-gcm')), idxKeyF = () => idxKeyC || (idxKeyC = crmKeyOf('crm-email-index'));
  const enc = obj => { const iv = crypto.randomBytes(12), c = crypto.createCipheriv('aes-256-gcm', encKeyF(), iv); const ct = Buffer.concat([c.update(JSON.stringify(obj), 'utf8'), c.final()]); return 'v1.' + b64u(iv) + '.' + b64u(c.getAuthTag()) + '.' + b64u(ct); };
  const dec = s => { const [v, iv, tag, ct] = String(s).split('.'); if (v !== 'v1') throw new Error('unknown record format'); const d = crypto.createDecipheriv('aes-256-gcm', encKeyF(), Buffer.from(iv, 'base64url')); d.setAuthTag(Buffer.from(tag, 'base64url')); return JSON.parse(Buffer.concat([d.update(Buffer.from(ct, 'base64url')), d.final()]).toString('utf8')); };
  const emailHash = e => e ? crypto.createHmac('sha256', idxKeyF()).update(String(e).trim().toLowerCase()).digest('hex') : null;
  const clean = (v, n) => String(v == null ? '' : v).replace(/[\u0000-\u001f\u007f]/g, ' ').trim().slice(0, n);
  const okEmail = e => /^[^\s@]{1,64}@[^\s@]{1,190}\.[^\s@]{2,24}$/.test(e);
  const newId = p => p + '_' + crypto.randomBytes(9).toString('base64url');

  // ── passwords ──
  const hashPw = (pw, salt) => crypto.scryptSync(String(pw), Buffer.from(salt, 'base64url'), 32, { N: 16384, r: 8, p: 1 }).toString('base64url');
  const dummy = { salt: b64u(crypto.randomBytes(16)) }; dummy.hash = hashPw('x', dummy.salt);

  // guesses spread over many addresses: 10 wrong passwords for one e-mail in 15 minutes lock that e-mail for 15 minutes (even with the right password)
  const fails = new Map(), LOCK_N = 10, LOCK_MS = 15 * 60 * 1000;
  const failsOf = (e, now) => (fails.get(e) || []).filter(t => now - t < LOCK_MS);
  // ── staff ──
  const staffCount = () => db.prepare('SELECT COUNT(*) AS n FROM staff').get().n;
  function addStaff({ email, name, password }) {
    email = clean(email, 254).toLowerCase(); name = clean(name, 80);
    if (!okEmail(email)) return { ok: false, status: 400, error: 'a valid e-mail is needed' };
    if (!name) return { ok: false, status: 400, error: 'a name is needed' };
    if (String(password || '').length < 12) return { ok: false, status: 400, error: 'the password needs at least 12 characters' };
    if (db.prepare('SELECT 1 FROM staff WHERE email = ?').get(email)) return { ok: false, status: 409, error: 'that e-mail already has an account' };
    const salt = b64u(crypto.randomBytes(16)), id = newId('stf');
    db.prepare('INSERT INTO staff (id, email, name, salt, hash, created) VALUES (?, ?, ?, ?, ?, ?)').run(id, email, name, salt, hashPw(password, salt), Date.now());
    return { ok: true, id, email, name };
  }
  const listStaff = () => db.prepare('SELECT id, email, name, created, disabled, last_login FROM staff ORDER BY created').all().map(r => ({ ...r, disabled: !!r.disabled }));
  function setStaffDisabled(id, disabled) {
    const r = db.prepare('UPDATE staff SET disabled = ?, token_gen = token_gen + 1 WHERE id = ?').run(disabled ? 1 : 0, String(id));   // old tokens die either way
    return r.changes ? { ok: true } : { ok: false, status: 404, error: 'no such account' };
  }
  function checkPassword(id, password, now = Date.now()) {   // the current password, for changes made from a signed-in session: under the same per-account lock as sign-in
    const s = db.prepare('SELECT email, salt, hash FROM staff WHERE id = ?').get(String(id)); if (!s) return false;
    const recent = failsOf(s.email, now); if (recent.length >= LOCK_N) return false;
    const ok = crypto.timingSafeEqual(Buffer.from(hashPw(String(password || ''), s.salt)), Buffer.from(s.hash));
    if (!ok) { recent.push(now); fails.set(s.email, recent); }
    return ok;
  }
  const endSessions = id => db.prepare('UPDATE staff SET token_gen = token_gen + 1 WHERE id = ?').run(String(id)).changes > 0;   // every token of that account stops
  function setPassword(id, password) {
    if (String(password || '').length < 12) return { ok: false, status: 400, error: 'the password needs at least 12 characters' };
    const salt = b64u(crypto.randomBytes(16));
    const r = db.prepare('UPDATE staff SET salt = ?, hash = ?, token_gen = token_gen + 1 WHERE id = ?').run(salt, hashPw(password, salt), String(id));
    return r.changes ? { ok: true } : { ok: false, status: 404, error: 'no such account' };
  }

  // ── JWT (HS256) ──
  function sign(claims, now = Date.now()) {
    const iat = Math.floor(now / 1000), head = b64u(JSON.stringify({ alg: 'HS256', typ: 'JWT' }));
    const body = b64u(JSON.stringify({ ...claims, iss: ISS, iat, exp: iat + TTL_S, jti: crypto.randomBytes(9).toString('base64url') }));
    return head + '.' + body + '.' + crypto.createHmac('sha256', jwtKey).update(head + '.' + body).digest('base64url');
  }
  function verify(token, now = Date.now()) {
    const parts = String(token || '').split('.'); if (parts.length !== 3 || parts.some(p => !/^[A-Za-z0-9_-]+$/.test(p))) return null;
    let head, body; try { head = JSON.parse(Buffer.from(parts[0], 'base64url')); body = JSON.parse(Buffer.from(parts[1], 'base64url')); } catch (_) { return null; }
    if (!head || head.alg !== 'HS256' || !body || body.iss !== ISS) return null;
    const want = crypto.createHmac('sha256', jwtKey).update(parts[0] + '.' + parts[1]).digest(), got = Buffer.from(parts[2], 'base64url');
    if (got.length !== want.length || !crypto.timingSafeEqual(got, want)) return null;
    const t = Math.floor(now / 1000); if (typeof body.exp !== 'number' || body.exp <= t || typeof body.iat !== 'number' || body.iat > t + 60) return null;
    const s = db.prepare('SELECT id, email, name, disabled, token_gen FROM staff WHERE id = ?').get(String(body.sub));
    if (!s || s.disabled || s.token_gen !== body.gen) return null;   // a disabled account, or a password changed since: the token is dead
    return { id: s.id, email: s.email, name: s.name };
  }
  function login(email, password, now = Date.now()) {
    const e = clean(email, 254).toLowerCase(), recent = failsOf(e, now);
    if (recent.length >= LOCK_N) { hashPw(String(password || ''), dummy.salt); return { ok: false, status: 429, error: 'too many wrong passwords for this account: try again in 15 minutes' }; }
    const s = db.prepare('SELECT * FROM staff WHERE email = ?').get(e);
    const h = hashPw(String(password || ''), s ? s.salt : dummy.salt), want = s ? s.hash : dummy.hash;   // the same work whether or not the e-mail exists
    const same = crypto.timingSafeEqual(Buffer.from(h), Buffer.from(want));
    if (!s || !same || s.disabled) { recent.push(now); fails.set(e, recent); if (fails.size > 10000) for (const k of fails.keys()) { if (!failsOf(k, now).length) fails.delete(k); } return { ok: false, status: 401, error: 'wrong e-mail or password' }; }
    fails.delete(e);
    db.prepare('UPDATE staff SET last_login = ? WHERE id = ?').run(now, s.id);
    return { ok: true, token: sign({ sub: s.id, gen: s.token_gen, name: s.name, email: s.email }, now), expiresIn: TTL_S, staff: { id: s.id, email: s.email, name: s.name } };
  }
  const bearer = req => { const m = /^Bearer ([A-Za-z0-9_.-]{20,4096})$/.exec(String(req.headers.authorization || '')); return m ? verify(m[1]) : null; };

  // ── CRM ──
  const fields = b => ({ name: clean(b.name, 120), email: clean(b.email, 254).toLowerCase(), phone: clean(b.phone, 40), company: clean(b.company, 120), country: clean(b.country, 60), tags: clean(b.tags, 200) });
  const rowOut = r => { let d; try { d = dec(r.data); } catch (_) { d = { name: '(unreadable: encrypted with another key)', email: '', phone: '', company: '', country: '', tags: '' }; } return { id: r.id, ...d, status: r.status, created: r.created, updated: r.updated, updatedBy: r.updated_by || '' }; };
  function saveCustomer(b, by, id) {
    const f = fields(b || {}); const status = STATUSES.includes(b && b.status) ? b.status : 'lead';
    if (!f.name) return { ok: false, status: 400, error: 'a name is needed' };
    if (f.email && !okEmail(f.email)) return { ok: false, status: 400, error: 'that e-mail does not look right' };
    const now = Date.now();
    if (id) {
      const r = db.prepare('UPDATE customers SET email_hash = ?, data = ?, status = ?, updated = ?, updated_by = ? WHERE id = ?').run(emailHash(f.email), enc(f), status, now, String(by || ''), String(id));
      return r.changes ? { ok: true, id: String(id) } : { ok: false, status: 404, error: 'no such customer' };
    }
    const nid = newId('cus');
    db.prepare('INSERT INTO customers (id, email_hash, data, status, created, updated, updated_by) VALUES (?, ?, ?, ?, ?, ?, ?)').run(nid, emailHash(f.email), enc(f), status, now, now, String(by || ''));
    return { ok: true, id: nid };
  }
  function listCustomers({ q = '', status = '' } = {}) {
    const rows = (STATUSES.includes(status) ? db.prepare('SELECT * FROM customers WHERE status = ? ORDER BY updated DESC').all(status) : db.prepare('SELECT * FROM customers ORDER BY updated DESC').all()).map(rowOut);
    const n = clean(q, 80).toLowerCase(); if (!n) return rows;
    return rows.filter(c => [c.name, c.email, c.company, c.phone, c.tags].some(v => String(v).toLowerCase().includes(n)));
  }
  function getCustomer(id) {
    const r = db.prepare('SELECT * FROM customers WHERE id = ?').get(String(id)); if (!r) return null;
    const notes = db.prepare('SELECT * FROM customer_notes WHERE customer_id = ? ORDER BY at DESC').all(r.id).map(n => { let t; try { t = dec(n.data).text; } catch (_) { t = '(unreadable)'; } return { id: n.id, text: t, at: n.at, by: n.by }; });
    return { ...rowOut(r), notes };
  }
  const findByEmail = e => db.prepare('SELECT * FROM customers WHERE email_hash = ?').all(emailHash(e)).map(rowOut);
  function deleteCustomer(id) { const r = db.prepare('DELETE FROM customers WHERE id = ?').run(String(id)); return r.changes ? { ok: true } : { ok: false, status: 404, error: 'no such customer' }; }
  function addNote(id, text, by) {
    const t = String(text || '').replace(/[\u0000-\u0008\u000b-\u001f\u007f]/g, ' ').trim().slice(0, 4000); if (!t) return { ok: false, status: 400, error: 'the note is empty' };
    if (!db.prepare('SELECT 1 FROM customers WHERE id = ?').get(String(id))) return { ok: false, status: 404, error: 'no such customer' };
    const nid = newId('note'); db.prepare('INSERT INTO customer_notes (id, customer_id, data, at, by) VALUES (?, ?, ?, ?, ?)').run(nid, String(id), enc({ text: t }), Date.now(), String(by || ''));
    db.prepare('UPDATE customers SET updated = ?, updated_by = ? WHERE id = ?').run(Date.now(), String(by || ''), String(id));
    return { ok: true, id: nid };
  }
  const stats = () => { const o = { total: 0 }; for (const s of STATUSES) o[s] = 0; for (const r of db.prepare('SELECT status, COUNT(*) AS n FROM customers GROUP BY status').all()) { o[r.status] = r.n; o.total += r.n; } return o; };

  return { staffCount, addStaff, listStaff, setStaffDisabled, setPassword, checkPassword, endSessions, login, sign, verify, bearer, saveCustomer, listCustomers, getCustomer, findByEmail, deleteCustomer, addNote, stats, STATUSES, close: () => db.close() };
}

module.exports = { createPortalAuth, STATUSES };
