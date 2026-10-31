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

test('device lock: a key runs on at most 4 computers; a 5th is refused until one is deactivated; re-activating a listed computer is fine', () => {
  const fsx = require('node:fs'), osx = require('node:os'), pathx = require('node:path');
  const { activateDevice, deactivateDevice, MAX_DEVICES } = require('./server');
  assert.strictEqual(MAX_DEVICES, 4);
  const file = pathx.join(fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'lic-')), 'licenses.json');
  const key = signKey({ v: 1, id: 'DEV1', plan: 'pro', name: 'Lab', issued: 1000, expires: null, seats: 4 }, pair.privateKey);
  const machines = ['AAAA-0001', 'AAAA-0002', 'AAAA-0003', 'AAAA-0004', 'AAAA-0005'];
  try {
    for (const m of machines.slice(0, 4)) assert.strictEqual(activateDevice({ key, machine: m, file, jwk }).ok, true, m);
    const fifth = activateDevice({ key, machine: machines[4], file, jwk });
    assert.strictEqual(fifth.ok, false); assert.strictEqual(fifth.status, 409); assert.match(fifth.error, /4 computers/);
    assert.strictEqual(activateDevice({ key, machine: machines[0], file, jwk }).devices, 4, 'a listed computer is not counted twice');
    assert.strictEqual(deactivateDevice({ key, machine: machines[1], file, jwk }).devices, 3);
    assert.strictEqual(activateDevice({ key, machine: machines[4], file, jwk }).ok, true, 'the freed seat is used');
    // a key made by someone else (not signed with the vendor key) is refused
    const forged = signKey({ v: 1, id: 'DEV2', plan: 'studio', name: 'Pirate', issued: 1000, expires: null, seats: 100 }, crypto.generateKeyPairSync('ec', { namedCurve: 'P-256' }).privateKey);
    const bad = activateDevice({ key: forged, machine: 'BBBB-0001', file, jwk });
    assert.strictEqual(bad.ok, false); assert.strictEqual(bad.status, 400);
  } finally { fsx.rmSync(pathx.dirname(file), { recursive: true, force: true }); }
});

test('device lock over HTTP: /activate and /deactivate answer with the seat count, never the key', async () => {
  const fsx = require('node:fs'), osx = require('node:os'), pathx = require('node:path');
  const dir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'lic-http-'));
  const server = createServer({ privateKey: pair.privateKey, token: TOKEN, dataFile: pathx.join(dir, 'licenses.json') });
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${server.address().port}`, post = (path, body) => fetch(base + path, { method: 'POST', body: JSON.stringify(body), headers: { 'content-type': 'application/json' } });
  try {
    const issued = await (await fetch(base + '/issue', { method: 'POST', body: JSON.stringify({ plan: 'pro', name: 'Jane', days: 7, seats: 9 }), headers: { 'content-type': 'application/json', authorization: 'Bearer ' + TOKEN } })).json();
    assert.strictEqual(issued.ok, false, 'more than 4 seats is refused when issuing');
    const ok = await (await fetch(base + '/issue', { method: 'POST', body: JSON.stringify({ plan: 'pro', name: 'Jane', days: 7, seats: 4 }), headers: { 'content-type': 'application/json', authorization: 'Bearer ' + TOKEN } })).json();
    const a = await post('/activate', { key: ok.key, machine: 'CCCC-0001' });
    const aj = await a.json(); assert.strictEqual(a.status, 200); assert.strictEqual(aj.devices, 1); assert.strictEqual(aj.limit, 4);
    assert.ok(!JSON.stringify(aj).includes(ok.key.split('.')[1]), 'the key is not echoed');
    const d = await post('/deactivate', { key: ok.key, machine: 'CCCC-0001' });
    assert.strictEqual((await d.json()).devices, 0);
    assert.strictEqual((await post('/activate', { key: 'nope', machine: 'CCCC-0001' })).status, 400);
  } finally { server.closeAllConnections(); server.close(); fsx.rmSync(dir, { recursive: true, force: true }); }
});

test('the app asks the license server before activating (device lock); without a server address nothing is checked online', async () => {
  const client = require('../license-client');
  const saved = process.env.LICENSE_SERVER_URL;
  try {
    delete process.env.LICENSE_SERVER_URL;
    assert.deepStrictEqual(await client.activate('k', 'M'), { ok: true, skipped: true });
    process.env.LICENSE_SERVER_URL = 'https://license.example.com/';
    assert.strictEqual(client.base(), 'https://license.example.com');
    const seen = [];
    const refused = await client.activate('k', 'M', async (url, opts) => { seen.push(url, JSON.parse(opts.body)); return new Response(JSON.stringify({ ok: false, error: 'this key is already used on 4 computers: deactivate one first' }), { status: 409 }); });
    assert.strictEqual(refused.ok, false); assert.strictEqual(refused.status, 409); assert.match(refused.message, /4 computers/);
    assert.deepStrictEqual(seen, ['https://license.example.com/activate', { key: 'k', machine: 'M' }]);
    const accepted = await client.activate('k', 'M', async () => new Response(JSON.stringify({ ok: true, devices: 2, limit: 4 }), { status: 200 }));
    assert.deepStrictEqual(accepted, { ok: true, devices: 2, limit: 4 });
    const offline = await client.activate('k', 'M', async () => { throw new Error('network down'); });
    assert.strictEqual(offline.offline, true);
  } finally { if (saved === undefined) delete process.env.LICENSE_SERVER_URL; else process.env.LICENSE_SERVER_URL = saved; }
});
