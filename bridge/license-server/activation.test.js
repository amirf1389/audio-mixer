// Windows-style activation: product key / licence key / edition token -> machine-bound activation; seats; trial once per machine; revocation list.
// Every token the server signs is also checked by the native verifier (engine license_test reads the same formats).
'use strict';
const test = require('node:test'), assert = require('node:assert'), crypto = require('crypto'), fs = require('fs'), os = require('os'), path = require('path');
const S = require('./server'), L = require('../license');
const { privateKey, publicKey } = crypto.generateKeyPairSync('ec', { namedCurve: 'P-256' });
const jwk = (({ kty, crv, x, y }) => ({ kty, crv, x, y }))(publicKey.export({ format: 'jwk' }));
const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'amfx1-act-')), file = path.join(dir, 'reg.json');
const parse = t => JSON.parse(Buffer.from(t.split('.')[1].replace(/-/g, '+').replace(/_/g, '/'), 'base64').toString('utf8'));
const verifies = t => { const [, b, sg] = t.split('.'); return crypto.verify('sha256', Buffer.from(b), { key: publicKey, dsaEncoding: 'ieee-p1363' }, Buffer.from(sg.replace(/-/g, '+').replace(/_/g, '/'), 'base64')); };
const M1 = 'AAAA-BBBB-CCCC-DDDD-0001', M2 = 'AAAA-BBBB-CCCC-DDDD-0002', M3 = 'AAAA-BBBB-CCCC-DDDD-0003';

test('perpetual, annual and trial terms are set by the issuer', () => {
  assert.strictEqual(S.buildPayload({ plan: 'pro', name: 'P', term: 'perpetual' }).payload.expires, null);
  const a = S.buildPayload({ plan: 'pro', name: 'A', term: 'annual' }, 1000).payload; assert.strictEqual(a.expires, 1000 + 365 * 86400000); assert.strictEqual(a.term, 'annual');
  assert.ok(S.buildPayload({ plan: 'pro', name: 'X', term: 'forever' }).error);
});

test('a licence key activates up to its seats, then the next computer is refused; the same computer again is fine', () => {
  const p = S.buildPayload({ plan: 'pro', name: 'Two seats', seats: 2, term: 'perpetual' }).payload, key = L.signKey(p, privateKey);
  const a1 = S.activateOnline({ key, machine: M1, file, jwk, privateKey });
  assert.ok(a1.ok && verifies(a1.activation)); const t = parse(a1.activation);
  assert.strictEqual(t.kind, 'amfx1-activation'); assert.strictEqual(t.mid, M1); assert.strictEqual(t.lid, p.id); assert.strictEqual(t.term, 'perpetual'); assert.strictEqual(t.exp, 0);
  assert.ok(S.activateOnline({ key, machine: M2, file, jwk, privateKey }).ok);
  const a3 = S.activateOnline({ key, machine: M3, file, jwk, privateKey }); assert.strictEqual(a3.status, 409);
  assert.ok(S.activateOnline({ key, machine: M1, file, jwk, privateKey }).ok);   // re-activating a computer already on it uses no new seat
});

test('a revoked licence is refused online and listed, signed, in the revocation list', () => {
  const p = S.buildPayload({ plan: 'studio', name: 'Leaked', term: 'annual' }).payload, key = L.signKey(p, privateKey);
  assert.ok(S.activateOnline({ key, machine: M1, file, jwk, privateKey }).ok);
  S.setRevoked({ id: p.id, revoked: true, reason: 'leaked', file });
  const r = S.activateOnline({ key, machine: M2, file, jwk, privateKey }); assert.strictEqual(r.status, 403); assert.ok(r.revoked);
  const list = S.revocationList({ file, privateKey }); assert.ok(verifies(list)); const l = parse(list);
  assert.strictEqual(l.kind, 'amfx1-revocations'); assert.ok(l.lids.includes(p.id));
});

test('a trial is 30 days, once per computer; asking again keeps the first end date', () => {
  const t0 = 1762700000000, a = S.startTrial({ machine: M3, file, privateKey, now: t0 }); assert.ok(a.ok && verifies(a.activation));
  assert.strictEqual(parse(a.activation).exp, t0 + 30 * 86400000); assert.strictEqual(parse(a.activation).term, 'trial');
  const b = S.startTrial({ machine: M3, file, privateKey, now: t0 + 10 * 86400000 }); assert.strictEqual(parse(b.activation).exp, t0 + 30 * 86400000);
  assert.strictEqual(S.startTrial({ machine: M3, file, privateKey, now: t0 + 31 * 86400000 }).status, 403);
  assert.ok(!JSON.stringify(JSON.parse(fs.readFileSync(file, 'utf8'))._trials).includes(M3));   // only a hash of the machine code is kept
});

test('a key bound by the vendor to one computer cannot be activated on another; a bad machine code is refused', () => {
  const p = S.buildPayload({ plan: 'pro', name: 'Bound', machine: M1 }).payload, key = L.signKey(p, privateKey);
  assert.strictEqual(S.activateOnline({ key, machine: M2, file, jwk, privateKey }).status, 403);
  assert.strictEqual(S.activateOnline({ key, machine: 'x', file, jwk, privateKey }).status, 400);
});
