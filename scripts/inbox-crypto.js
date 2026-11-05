'use strict';
// Encrypted key-request inbox. The internet-facing listener must be able to STORE a customer's request (name, e-mail, machine ID) without
// the vendor's PIN, but nobody who copies the vendor folder should be able to READ it. So each record is sealed to the vendor's PUBLIC key
// (ECIES on P-256: a fresh ephemeral key per record, ECDH -> HKDF-SHA256 -> AES-256-GCM) and can only be opened with the vendor's PRIVATE key,
// which lives in the PIN vault (scripts/license-vault.js). The same key pair that signs licenses; nothing new to back up.
// In the clear per record: the time and a salted hash used to merge duplicate requests from the same machine and plan (no personal data).
const crypto = require('node:crypto');

const INFO = Buffer.from('amfx1-inbox-v1');
const b64 = b => Buffer.from(b).toString('base64');
const unb64 = s => Buffer.from(String(s || ''), 'base64');

// salted so the stored value is not the raw machine ID; the salt is per vendor key (its x coordinate), so different vendors' hashes differ
function dedupeHash(pubJwk, machineId, plan) {
  return crypto.createHash('sha256').update('amfx1-dedupe|' + String(pubJwk && pubJwk.x) + '|' + String(machineId || '') + '|' + String(plan || '')).digest('hex').slice(0, 32);
}

function deriveKey(shared, epkRaw) { return Buffer.from(crypto.hkdfSync('sha256', shared, epkRaw, INFO, 32)); }

function seal(rec, pubJwk) {
  const pub = crypto.createPublicKey({ key: { kty: 'EC', crv: 'P-256', x: pubJwk.x, y: pubJwk.y }, format: 'jwk' });
  const eph = crypto.generateKeyPairSync('ec', { namedCurve: 'P-256' });
  const epk = eph.publicKey.export({ format: 'jwk' }), epkRaw = eph.publicKey.export({ type: 'spki', format: 'der' });
  const key = deriveKey(crypto.diffieHellman({ privateKey: eph.privateKey, publicKey: pub }), epkRaw);
  const iv = crypto.randomBytes(12), head = { v: 1, sealed: true, at: rec.at, plan: rec.plan, mh: rec.machineId ? dedupeHash(pubJwk, rec.machineId, rec.plan) : null };
  const c = crypto.createCipheriv('aes-256-gcm', key, iv); c.setAAD(Buffer.from(JSON.stringify(head)));
  const ct = Buffer.concat([c.update(JSON.stringify(rec), 'utf8'), c.final()]);
  return { ...head, epk: { x: epk.x, y: epk.y }, iv: b64(iv), tag: b64(c.getAuthTag()), ct: b64(ct) };
}

function open(box, privateKey) {
  if (!box || box.v !== 1 || !box.sealed) throw new Error('not a sealed inbox record');
  const head = { v: 1, sealed: true, at: box.at, plan: box.plan, mh: box.mh };
  const epkKey = crypto.createPublicKey({ key: { kty: 'EC', crv: 'P-256', x: box.epk.x, y: box.epk.y }, format: 'jwk' });
  const key = deriveKey(crypto.diffieHellman({ privateKey, publicKey: epkKey }), epkKey.export({ type: 'spki', format: 'der' }));
  const d = crypto.createDecipheriv('aes-256-gcm', key, unb64(box.iv)); d.setAAD(Buffer.from(JSON.stringify(head))); d.setAuthTag(unb64(box.tag));
  return JSON.parse(Buffer.concat([d.update(unb64(box.ct)), d.final()]).toString('utf8'));   // throws on a wrong key or any changed byte
}

module.exports = { seal, open, dedupeHash };
