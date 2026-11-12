'use strict';
// End to end: the real engine (null audio device, test tone on strip 3), the real add-on, the real server.
// Needs AMFB_ENGINE (path of amfb-hybrid) and AMFB_ADDON (path of the built add-on).
const { test, before, after } = require('node:test');
const assert = require('node:assert');
const { spawn } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');
const http = require('http');

process.env.AMFB_SHM = '/amfb-test-' + process.pid;
const { start } = require('../node/server.js');
const pinFile = path.join(os.tmpdir(), 'amfb-pin-' + process.pid);
let engine, srv, base, clock = Date.now(), token;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const pin = () => fs.readFileSync(pinFile, 'utf8').trim();
async function call(p, { body, tok = token, headers = {}, method } = {}) {
  const r = await fetch(base + p, { method: method || (body ? 'POST' : 'GET'), headers: { ...(tok ? { Authorization: 'Bearer ' + tok } : {}), ...(body ? { 'Content-Type': 'application/json' } : {}), ...headers }, body: body ? JSON.stringify(body) : undefined });
  return { status: r.status, json: await r.json().catch(() => null), headers: r.headers };
}
const db = v => 20 * Math.log10(Math.max(v, 1e-9));

before(async () => {
  engine = spawn(process.env.AMFB_ENGINE, ['--backend', 'null', '--tone', '3'], { stdio: 'ignore', env: process.env });
  await sleep(700);
  srv = await start({ port: 0, pinFile, now: () => clock, log: () => {}, hosts: 'mixer.example' });
  base = 'http://127.0.0.1:' + srv.port;
});
after(async () => { engine.kill(); await srv.close(); });

test('listens on this machine only and sends the hardening headers', async () => {
  assert.strictEqual(srv.server.address().address, '127.0.0.1');
  const r = await fetch(base + '/');
  assert.strictEqual(r.status, 200);
  assert.match(r.headers.get('content-security-policy'), /default-src 'none'/);
  assert.strictEqual(r.headers.get('x-frame-options'), 'DENY');
  assert.strictEqual((await fetch(base + '/app.js')).status, 200);
  assert.strictEqual((await fetch(base + '/..%2fserver.js')).status, 404);
});
test('nothing works without pairing', async () => {
  for (const p of ['/api/state', '/api/meters']) assert.strictEqual((await call(p, { tok: '' })).status, 401);
  assert.strictEqual((await call('/api/strip/1', { tok: '', body: { mute: true } })).status, 401);
  assert.strictEqual((await call('/api/state', { tok: 'x'.repeat(43) })).status, 401);
});
test('an unknown Host and a cross-site post are refused', async () => {
  const withHost = h => new Promise(r => http.get({ host: '127.0.0.1', port: srv.port, path: '/api/state', headers: { Host: h } }, x => { x.resume(); r(x.statusCode); }));
  assert.strictEqual(await withHost('evil.example'), 421);
  assert.strictEqual(await withHost('mixer.example'), 401);
  assert.strictEqual((await call('/api/pair', { body: { pin: pin() }, headers: { Origin: 'https://evil.example' } })).status, 403);
  assert.strictEqual((await call('/api/pair', { method: 'PUT' })).status, 405);
});
test('the PIN is six digits, private, single use and pairs', async () => {
  assert.match(pin(), /^\d{6}$/);
  if (process.platform !== 'win32') assert.strictEqual(fs.statSync(pinFile).mode & 0o077, 0);
  const first = pin();
  assert.strictEqual((await call('/api/pair', { body: { pin: first === '000000' ? '000001' : '000000' } })).status, 401);
  const r = await call('/api/pair', { body: { pin: first } });
  assert.strictEqual(r.status, 200); assert.match(r.json.token, /^[A-Za-z0-9_-]{43}$/);
  token = r.json.token;
  assert.notStrictEqual(pin(), first);
  assert.strictEqual((await call('/api/pair', { body: { pin: first } })).status, 401);
});
test('the PIN changes every minute', async () => {
  const old = pin(); clock += 61_000;
  assert.strictEqual((await call('/api/pair', { body: { pin: old } })).status, 401);
  assert.notStrictEqual(pin(), old);
});
test('state and meters come from the running engine', async () => {
  const s = await call('/api/state');
  assert.strictEqual(s.status, 200);
  assert.strictEqual(s.json.strips.length, 16); assert.strictEqual(s.json.strips[2].source, 'TONE 440'); assert.strictEqual(s.json.strips[0].eq.length, 7);
  await sleep(600);
  const m = (await call('/api/meters')).json.meters;
  assert.strictEqual(m.length, 17);
  assert.ok(Math.abs(db(m[2][0]) + 12) < 0.3, 'strip 3 peak ' + db(m[2][0]));
  assert.ok(Math.abs(db(m[2][4]) + 12) < 0.3, 'strip 3 true peak ' + db(m[2][4]));
  assert.ok(m[2][6] > 0.99 && m[2][7] === 1 && m[0][7] === 0);
  assert.ok(Math.abs(db(m[16][0]) + 12) < 0.3, 'master ' + db(m[16][0]));
});
test('controls reach the audio', async () => {
  assert.strictEqual((await call('/api/strip/3', { body: { gainDb: -6, pan: -1 } })).status, 200);
  await sleep(400);
  let m = (await call('/api/meters')).json.meters;
  assert.ok(Math.abs(db(m[16][0]) + 12 + 6 - 3.01) < 0.3, 'left ' + db(m[16][0])); assert.ok(m[16][1] < 1e-4);
  await call('/api/strip/3', { body: { gainDb: 0, pan: 0, mute: true } }); await sleep(400);
  m = (await call('/api/meters')).json.meters; assert.ok(m[16][0] < 1e-4, 'mute');
  await call('/api/strip/3', { body: { mute: false } }); await call('/api/master', { body: { gainDb: -20 } }); await sleep(400);
  m = (await call('/api/meters')).json.meters; assert.ok(Math.abs(db(m[16][0]) + 32) < 0.3, 'master gain ' + db(m[16][0]));
  await call('/api/master', { body: { gainDb: 0 } });
  assert.strictEqual((await call('/api/strip/3', { body: { compLevel: 8 } })).status, 200); await sleep(500);
  m = (await call('/api/meters')).json.meters; assert.ok(m[2].length === 9 && m[2][8] < -1, 'gain reduction ' + m[2][8]);
  assert.strictEqual((await call('/api/state')).json.strips[2].compLevel, 8);
  await call('/api/strip/3', { body: { compLevel: 0 } }); await sleep(400);
  m = (await call('/api/meters')).json.meters; assert.ok(m[2][8] === 0, 'no reduction when off');
  assert.strictEqual((await call('/api/eq/3/2', { body: { freq: 440, gainDb: 6, q: 1 } })).status, 200); await sleep(500);
  m = (await call('/api/meters')).json.meters; assert.ok(Math.abs(db(m[2][0]) + 6) < 0.3, 'eq ' + db(m[2][0]));
  assert.strictEqual((await call('/api/state')).json.strips[2].eq[1].gainDb, 6);
});
test('bad input is refused or clamped, never passed on', async () => {
  assert.strictEqual((await call('/api/strip/17', { body: { mute: true } })).status, 400);
  assert.strictEqual((await call('/api/strip/0', { body: { mute: true } })).status, 400);
  assert.strictEqual((await call('/api/strip/1', { body: { gainDb: 'loud' } })).status, 400);
  assert.strictEqual((await call('/api/strip/1', { body: { mute: 1 } })).status, 400);
  assert.strictEqual((await call('/api/eq/1/8', { body: { gainDb: 1 } })).status, 400);
  assert.strictEqual((await call('/api/strip/1', { body: { gainDb: 999, pan: -7 } })).status, 200);
  const s = (await call('/api/state')).json.strips[0]; assert.strictEqual(s.gainDb, 12); assert.strictEqual(s.pan, -1);
  await call('/api/strip/1', { body: { gainDb: 0, pan: 0 } });
  const big = await fetch(base + '/api/strip/1', { method: 'POST', headers: { Authorization: 'Bearer ' + token, 'Content-Type': 'application/json' }, body: '{"x":"' + 'a'.repeat(9000) + '"}' }).then(r => r.status, () => 413);
  assert.strictEqual(big, 413);
  assert.strictEqual((await fetch(base + '/api/strip/1', { method: 'POST', headers: { Authorization: 'Bearer ' + token, 'Content-Type': 'text/plain' }, body: '{}' })).status, 415);
});
test('a session is bound to its address and ends after ten idle minutes', async () => {
  assert.strictEqual((await call('/api/state', { headers: { 'X-Forwarded-For': '203.0.113.9' } })).status, 401);
  await sleep(60); clock += 9 * 60_000; assert.strictEqual((await call('/api/state')).status, 200);
  clock += 11 * 60_000; assert.strictEqual((await call('/api/state')).status, 401);
});
test('wrong PINs are stopped per address, and for everyone when many addresses try', async () => {
  const wrongPin = () => (pin() === '111111' ? '222222' : '111111');
  for (let i = 0; i < 5; i++) assert.strictEqual((await call('/api/pair', { tok: '', body: { pin: wrongPin() }, headers: { 'X-Forwarded-For': '198.51.100.1' } })).status, 401);
  assert.strictEqual((await call('/api/pair', { tok: '', body: { pin: pin() }, headers: { 'X-Forwarded-For': '198.51.100.1' } })).status, 429);
  assert.strictEqual((await call('/api/pair', { tok: '', body: { pin: wrongPin() }, headers: { 'X-Forwarded-For': '198.51.100.2' } })).status, 401);
  for (let i = 0; i < 24; i++) await call('/api/pair', { tok: '', body: { pin: wrongPin() }, headers: { 'X-Forwarded-For': '198.51.100.' + (10 + i) } });
  assert.strictEqual((await call('/api/pair', { tok: '', body: { pin: pin() }, headers: { 'X-Forwarded-For': '192.0.2.77' } })).status, 429);
  clock += 61_000;
  const ok = await call('/api/pair', { tok: '', body: { pin: srv.currentPin() } });
  assert.strictEqual(ok.status, 200); token = ok.json.token;
});
test('a stopped engine is reported, not hidden', async () => {
  engine.kill(); await sleep(300); clock += 2000; await call('/api/state'); clock += 2000;
  assert.strictEqual((await call('/api/state')).status, 503);
});
