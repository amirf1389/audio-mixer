'use strict';
// Tests for the license server. Uses a throwaway key pair, never the vendor key.
//   node --test license-server/test.js
const test = require('node:test');
const assert = require('node:assert');
const crypto = require('node:crypto');
const { buildPayload, createServer, authorized } = require('./server');
const { verifyKey, signKey } = require('../license');

const pair = crypto.generateKeyPairSync('ec', { namedCurve: 'P-256' });
const jwk = pair.publicKey.export({ format: 'jwk' });
const TOKEN = 'x'.repeat(40);

test('a payload is built with the same fields the app expects', () => {
  const r = buildPayload({ plan: 'pro', name: 'Jane Doe', days: 30, seats: 2, machine: 'ABCD-1234-EFGH-5678' }, 1000);
  assert.ok(!r.error);
  assert.strictEqual(r.payload.v, 1);
  assert.strictEqual(r.payload.plan, 'pro');
  assert.strictEqual(r.payload.seats, 2);
  assert.strictEqual(r.payload.issued, 1000);
  assert.strictEqual(r.payload.expires, 1000 + 30 * 86400000);
  assert.strictEqual(r.payload.mid, 'ABCD-1234-EFGH-5678');
});

test('a key the server signs verifies with the matching public key, and tampering fails', () => {
  const { payload } = buildPayload({ plan: 'pro', name: 'Jane', days: 0 });
  const key = signKey(payload, pair.privateKey);
  assert.strictEqual(verifyKey(key, { jwk }).ok, true);
  const parts = key.split('.');
  const forged = [parts[0], Buffer.from(JSON.stringify({ ...payload, plan: 'studio' })).toString('base64url'), parts[2]].join('.');
  assert.strictEqual(verifyKey(forged, { jwk }).ok, false);
});

test('bad requests are refused with a reason', () => {
  assert.match(buildPayload({ plan: 'gold', name: 'x' }).error, /plan/);
  assert.match(buildPayload({ plan: 'pro', name: '' }).error, /name/);
  assert.match(buildPayload({ plan: 'pro', name: 'x', days: -1 }).error, /days/);
  assert.match(buildPayload({ plan: 'pro', name: 'x', seats: 0 }).error, /seats/);
  assert.match(buildPayload({ plan: 'pro', name: 'x', machine: 'no' }).error, /machine/);
});

test('the admin token is checked in constant time and must match exactly', () => {
  const req = (h) => ({ headers: { authorization: h } });
  assert.strictEqual(authorized(req('Bearer ' + TOKEN), TOKEN), true);
  assert.strictEqual(authorized(req('Bearer ' + 'y'.repeat(40)), TOKEN), false);
  assert.strictEqual(authorized(req('Bearer short'), TOKEN), false);
  assert.strictEqual(authorized(req(undefined), TOKEN), false);
  assert.strictEqual(authorized(req('Bearer ' + TOKEN), ''), false);
});

test('the HTTP endpoint issues a key only with the token, and never echoes the private key', async () => {
  const server = createServer({ privateKey: pair.privateKey, token: TOKEN });
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${server.address().port}`;
  try {
    const body = JSON.stringify({ plan: 'pro', name: 'Jane', days: 7 });
    const denied = await fetch(base + '/issue', { method: 'POST', body, headers: { 'content-type': 'application/json' } });
    assert.strictEqual(denied.status, 401);

    const ok = await fetch(base + '/issue', { method: 'POST', body, headers: { 'content-type': 'application/json', authorization: 'Bearer ' + TOKEN } });
    assert.strictEqual(ok.status, 200);
    const j = await ok.json();
    assert.strictEqual(verifyKey(j.key, { jwk }).ok, true);
    assert.ok(!JSON.stringify(j).includes('PRIVATE'));
  } finally {
    server.close();
  }
});
