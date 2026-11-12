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

// ---- licence management: records, revocation, the admin API, the management page
const fsT = require('node:fs'), osT = require('node:os'), pathT = require('node:path');
const { createApp, rateLimiter } = require('./framework');
async function withServer(fn) {
  const dir = fsT.mkdtempSync(pathT.join(osT.tmpdir(), 'amfx1-lic-')), dataFile = pathT.join(dir, 'licenses.json');
  const srv = createServer({ privateKey: pair.privateKey, token: TOKEN, dataFile, quiet: true });
  await new Promise(r => srv.listen(0, '127.0.0.1', r));
  const base = 'http://127.0.0.1:' + srv.address().port;
  const call = async (method, p, body, tok = TOKEN) => { const r = await fetch(base + p, { method, headers: { ...(tok ? { authorization: 'Bearer ' + tok } : {}), 'content-type': 'application/json' }, body: body ? JSON.stringify(body) : undefined }); let j = null; const text = await r.text(); try { j = JSON.parse(text); } catch (_) { /* a page */ } return { status: r.status, headers: r.headers, json: j, text }; };
  try { await fn({ call, dataFile, base }); } finally { await new Promise(r => srv.close(r)); fsT.rmSync(dir, { recursive: true, force: true }); }
}

test('management: an issued key is on record with its customer, the key itself and the customer details are not readable in the file', async () => {
  await withServer(async ({ call, dataFile }) => {
    const r = await call('POST', '/admin/licenses', { plan: 'pro', name: 'Zebra Sound Ltd', email: 'ops@zebra.example', note: 'festival rig', days: 30, seats: 2 });
    assert.strictEqual(r.status, 200); assert.ok(r.json.key && r.json.id);
    const raw = fsT.readFileSync(dataFile, 'utf8');
    assert.ok(!raw.includes('Zebra') && !raw.includes('zebra.example') && !raw.includes('festival'), 'customer details must be encrypted at rest');
    assert.ok(!raw.includes(r.json.key) && !raw.includes(r.json.key.split('.')[2]), 'the key must not be stored');
    const l = (await call('GET', '/admin/licenses')).json;
    assert.strictEqual(l.licenses.length, 1);
    assert.deepStrictEqual([l.licenses[0].name, l.licenses[0].email, l.licenses[0].note, l.licenses[0].plan, l.licenses[0].seats, l.licenses[0].state], ['Zebra Sound Ltd', 'ops@zebra.example', 'festival rig', 'pro', 2, 'active']);
    assert.ok(!JSON.stringify(l).includes(r.json.key.split('.')[2]));
    assert.strictEqual((await call('GET', '/admin/licenses', null, 'y'.repeat(40))).status, 401);
    assert.strictEqual((await call('GET', '/admin/licenses', null, '')).status, 401);
  });
});

test('management: a revoked key cannot be activated and says so; restoring it works again; a seat can be freed by the admin', async () => {
  await withServer(async ({ call }) => {
    const k = (await call('POST', '/issue', { plan: 'pro', name: 'Ann', days: 30, seats: 2 })).json;
    const A = 'AAAA-1111-BBBB-2222', B = 'CCCC-3333-DDDD-4444';
    assert.strictEqual((await call('POST', '/activate', { key: k.key, machine: A }, '')).status, 200);
    let s = (await call('POST', '/status', { key: k.key, machine: A }, '')).json;
    assert.deepStrictEqual([s.ok, s.revoked, s.devices, s.limit, s.thisComputer, s.expired], [true, false, 1, 2, true, false]);
    assert.strictEqual((await call('POST', '/admin/licenses/' + k.id + '/revoke', { reason: 'refund' })).status, 200);
    const refused = await call('POST', '/activate', { key: k.key, machine: B }, '');
    assert.strictEqual(refused.status, 403); assert.match(refused.json.error, /revoked/);
    assert.strictEqual((await call('POST', '/status', { key: k.key, machine: A }, '')).json.revoked, true);
    let row = (await call('GET', '/admin/licenses')).json.licenses[0];
    assert.deepStrictEqual([row.state, row.reason, row.devices], ['revoked', 'refund', [A]]);
    assert.strictEqual((await call('POST', '/admin/licenses/' + k.id + '/restore', {})).status, 200);
    assert.strictEqual((await call('POST', '/activate', { key: k.key, machine: B }, '')).status, 200);
    assert.strictEqual((await call('DELETE', '/admin/licenses/' + k.id + '/devices/' + A)).status, 200);
    assert.strictEqual((await call('DELETE', '/admin/licenses/' + k.id + '/devices/' + A)).status, 404);
    row = (await call('GET', '/admin/licenses')).json.licenses[0]; assert.deepStrictEqual(row.devices, [B]);
    assert.strictEqual((await call('POST', '/admin/licenses/FFFFFFFFFFFF/revoke', {})).status, 404);
    assert.strictEqual((await call('POST', '/admin/licenses/..%2F..%2Fetc/revoke', {})).status, 404);
    assert.strictEqual((await call('POST', '/status', { key: 'not.a.key', machine: A }, '')).status, 400);
    const st = (await call('GET', '/admin/stats')).json; assert.deepStrictEqual([st.total, st.active, st.revoked, st.devices, st.plans.pro], [1, 1, 0, 1, 1]);
  });
});

test('management: every action is in the audit trail without keys or names, and a changed line is detected', async () => {
  await withServer(async ({ call, dataFile }) => {
    const k = (await call('POST', '/issue', { plan: 'pro', name: 'Secret Name', days: 1 })).json;
    await call('POST', '/admin/licenses/' + k.id + '/revoke', { reason: 'test' }); await call('GET', '/admin/licenses', null, 'z'.repeat(40));
    const a = (await call('GET', '/admin/audit?limit=10')).json.entries;
    assert.deepStrictEqual(a.map(e => e.action), ['auth.denied', 'license.revoke', 'license.issue']);
    const log = fsT.readFileSync(dataFile + '.audit.log', 'utf8'); assert.ok(!log.includes('Secret Name') && !log.includes(k.key.split('.')[2]) && !log.includes(TOKEN));
    assert.deepStrictEqual((await call('GET', '/admin/audit/verify')).json.chain, { ok: true, entries: 3 });
    fsT.writeFileSync(dataFile + '.audit.log', log.replace('license.revoke', 'license.restore'));
    const v = (await call('GET', '/admin/audit/verify')).json.chain; assert.strictEqual(v.ok, false); assert.strictEqual(v.brokenAt, 2);
  });
});

test('management: the page is served with a strict policy and nothing secret; wrong tokens are slowed down; unknown paths and methods are refused', async () => {
  await withServer(async ({ call }) => {
    const page = await call('GET', '/admin/', null, '');
    assert.strictEqual(page.status, 200); assert.match(page.headers.get('content-security-policy'), /default-src 'none'/); assert.strictEqual(page.headers.get('x-frame-options'), 'DENY');
    assert.ok(page.text.includes('Licence management') && !page.text.includes(TOKEN) && !/<script>[^<]/.test(page.text) && !/ on\w+="/.test(page.text), 'no inline script or handler');
    for (const f of ['/admin/app.js', '/admin/style.css']) assert.strictEqual((await call('GET', f, null, '')).status, 200);
    assert.strictEqual((await call('GET', '/admin/../server.js', null, '')).status, 404);
    assert.strictEqual((await call('GET', '/nothing')).status, 404); assert.strictEqual((await call('GET', '/issue')).status, 405);
    assert.strictEqual((await call('POST', '/issue', null)).status, 400);
    let last = 0; for (let i = 0; i < 12; i++) last = (await call('GET', '/admin/stats', null, 'w'.repeat(40))).status;
    assert.strictEqual(last, 429);
  });
});

test('framework: routes with parameters, the body limit, the limiter window', async () => {
  const app = createApp({ maxBody: 16 }); app.post('/a/:x', async c => c.json(200, { x: c.params.x, b: await c.body() }));
  const srv = require('node:http').createServer(app.handler); await new Promise(r => srv.listen(0, '127.0.0.1', r)); const base = 'http://127.0.0.1:' + srv.address().port;
  try {
    let r = await fetch(base + '/a/hello%20there', { method: 'POST', body: '{"k":1}' }); assert.deepStrictEqual(await r.json(), { x: 'hello there', b: { k: 1 } });
    r = await fetch(base + '/a/1', { method: 'POST', body: JSON.stringify({ long: 'x'.repeat(64) }) }); assert.strictEqual(r.status, 413);
    r = await fetch(base + '/a/1', { method: 'POST', body: '[1]' }); assert.strictEqual(r.status, 400);
    r = await fetch(base + '/a/%E0%A4%A', { method: 'POST', body: '{}' }); assert.strictEqual(r.status, 404);
  } finally { await new Promise(r => srv.close(r)); }
  let t = 0; const lim = rateLimiter({ now: () => t }); assert.ok(lim.allow('k', 2).ok && lim.allow('k', 2).ok); const no = lim.allow('k', 2); assert.ok(!no.ok && no.retryAfter === 60); t = 60000; assert.ok(lim.allow('k', 2).ok);
});

test('a trial key always ends within a year and is for one computer', () => {
  const y = 365 * 86400000;
  const a = buildPayload({ plan: 'pro', name: 'Trial user', days: 0, seats: 2, trial: true }, 1000);
  assert.ok(!a.error); assert.strictEqual(a.payload.trial, true); assert.strictEqual(a.payload.seats, 1); assert.strictEqual(a.payload.expires, 1000 + y);
  const b = buildPayload({ plan: 'pro', name: 'Trial user', days: 3650, trial: true }, 1000); assert.strictEqual(b.payload.expires, 1000 + y);
  const c = buildPayload({ plan: 'pro', name: 'Trial user', days: 30, trial: true }, 1000); assert.strictEqual(c.payload.expires, 1000 + 30 * 86400000);
  const d = buildPayload({ plan: 'pro', name: 'Paying user', days: 0 }, 1000); assert.strictEqual(d.payload.trial, undefined); assert.strictEqual(d.payload.expires, null);
});

test('a product key is made with the licence, redeems into a genuine key, and the register never holds it', async () => {
  const fsx = require('node:fs'), osx = require('node:os'), pathx = require('node:path');
  const dir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'lic-code-')), file = pathx.join(dir, 'licenses.json');
  const server = createServer({ privateKey: pair.privateKey, token: TOKEN, dataFile: file });
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${server.address().port}`, post = (path, body, auth) => fetch(base + path, { method: 'POST', body: JSON.stringify(body), headers: Object.assign({ 'content-type': 'application/json' }, auth ? { authorization: 'Bearer ' + TOKEN } : {}) });
  try {
    const made = await (await post('/issue', { plan: 'pro', name: 'Boxed customer', days: 365 }, true)).json();
    assert.match(made.productKey, /^[0-9A-HJKMNP-TV-Z]{5}(-[0-9A-HJKMNP-TV-Z]{5}){4}$/);
    assert.ok(!fsx.readFileSync(file, 'utf8').includes(made.productKey.replace(/-/g, '')) && !fsx.readFileSync(file, 'utf8').includes(made.productKey));
    const typed = made.productKey.toLowerCase().replace(/-/g, ' ');                      // typed in lower case with spaces: still the same key
    const got = await post('/redeem', { code: typed }); assert.strictEqual(got.status, 200);
    const j = await got.json(), v = verifyKey(j.key, { jwk });
    assert.strictEqual(v.ok, true); assert.strictEqual(j.id, made.id); assert.strictEqual(j.plan, 'pro');
    assert.strictEqual((await post('/redeem', { code: 'AAAAA-BBBBB-CCCCC-DDDDD-EEEEE' })).status, 404);
    assert.strictEqual((await post('/redeem', { code: 'TOO-SHORT' })).status, 400);
    await post(`/admin/licenses/${made.id}/revoke`, { reason: 'test' }, true);
    assert.strictEqual((await post('/redeem', { code: made.productKey })).status, 403);   // a revoked licence gives no key
  } finally { server.close(); fsx.rmSync(dir, { recursive: true, force: true }); }
});

test('the customer portal is served, reads the update list through the server, and holds nothing of the admin side', async () => {
  const http = require('node:http'), fsx = require('node:fs'), osx = require('node:os'), pathx = require('node:path');
  const manifest = { payload: JSON.stringify({ product: 'audio-mixer', version: '1.23.0.1', channel: 'stable', released: '2026-11-12', notes: ['one', 'two'], files: { 'win-x64-exe': { name: 'AmF-X1-setup.exe', url: 'https://example.org/AmF-X1-setup.exe', sha256: 'a'.repeat(64) } } }), signature: 's'.repeat(86) };
  const feed = http.createServer((q, r) => { r.setHeader('content-type', 'application/json'); r.end(JSON.stringify(manifest)); });
  await new Promise(r => feed.listen(0, '127.0.0.1', r));
  const dir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'lic-portal-'));
  const server = createServer({ privateKey: pair.privateKey, token: TOKEN, dataFile: pathx.join(dir, 'licenses.json'), updateUrl: `http://127.0.0.1:${feed.address().port}/update.json` });
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${server.address().port}`;
  try {
    const page = await fetch(base + '/portal/'); assert.strictEqual(page.status, 200);
    const html = await page.text(); assert.ok(html.includes('Activate with a product key') && !html.includes(TOKEN) && !/<script>[^<]/.test(html));   // no inline script, no secret
    for (const f of ['/portal/app.js', '/portal/portal.css', '/portal/base.css']) assert.strictEqual((await fetch(base + f)).status, 200);
    const js = await (await fetch(base + '/portal/app.js')).text(); assert.ok(!js.includes('/admin') && !js.includes('Bearer'));
    const u = await (await fetch(base + '/portal/updates')).json();
    assert.strictEqual(u.ok, true); assert.strictEqual(u.version, '1.23.0.1'); assert.strictEqual(u.signed, true); assert.strictEqual(u.files[0].platform, 'win-x64-exe'); assert.strictEqual(u.files[0].sha256.length, 64);
    const bad = createServer({ privateKey: pair.privateKey, token: TOKEN, dataFile: pathx.join(dir, 'l2.json'), updateUrl: 'http://example.org/update.json' });
    await new Promise(r => bad.listen(0, '127.0.0.1', r));
    const b = await (await fetch(`http://127.0.0.1:${bad.address().port}/portal/updates`)).json(); bad.close();
    assert.strictEqual(b.ok, false);                                                                   // a plain http list from elsewhere is not read
  } finally { server.close(); feed.close(); fsx.rmSync(dir, { recursive: true, force: true }); }
});

test('portal management: counts product keys and their use, makes a new product key that replaces the old one, admin only', async () => {
  const fsx = require('node:fs'), osx = require('node:os'), pathx = require('node:path');
  const dir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'lic-manage-'));
  const server = createServer({ privateKey: pair.privateKey, token: TOKEN, dataFile: pathx.join(dir, 'licenses.json'), updateUrl: 'http://127.0.0.1:9/none' });
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${server.address().port}`, H = { 'content-type': 'application/json', authorization: 'Bearer ' + TOKEN };
  const post = (p, b, auth = true) => fetch(base + p, { method: 'POST', body: JSON.stringify(b), headers: auth ? H : { 'content-type': 'application/json' } });
  try {
    const a = await (await post('/issue', { plan: 'pro', name: 'First', days: 30 })).json(), b = await (await post('/issue', { plan: 'studio', name: 'Second', days: 0 })).json();
    assert.strictEqual((await fetch(base + '/admin/portal')).status === 200, false);                       // not without the token
    assert.strictEqual((await post('/redeem', { code: a.productKey }, false)).status, 200);
    assert.strictEqual((await post('/redeem', { code: a.productKey }, false)).status, 200);
    assert.strictEqual((await post('/redeem', { code: 'ZZZZZ-ZZZZZ-ZZZZZ-ZZZZZ-ZZZZZ' }, false)).status, 404);
    const p = await (await fetch(base + '/admin/portal', { headers: H })).json();
    assert.deepStrictEqual(p.productKeys, { made: 2, redeemed: 1, neverUsed: 1 }); assert.strictEqual(p.last24h.redeemed, 2); assert.strictEqual(p.last24h.refused, 1); assert.strictEqual(p.path, '/portal/');
    const list = (await (await fetch(base + '/admin/licenses', { headers: H })).json()).licenses, la = list.find(l => l.id === a.id);
    assert.strictEqual(la.redeems, 2); assert.ok(la.redeemedAt > 0); assert.strictEqual(la.productKey, true);
    assert.strictEqual((await post(`/admin/licenses/${a.id}/product-key`, {}, false)).status === 200, false);  // admin only
    const n = await (await post(`/admin/licenses/${a.id}/product-key`, {})).json();
    assert.match(n.productKey, /^[0-9A-Z]{5}(-[0-9A-Z]{5}){4}$/); assert.notStrictEqual(n.productKey, a.productKey);
    assert.strictEqual((await post('/redeem', { code: a.productKey }, false)).status, 404);                // the old one is dead
    assert.strictEqual((await post('/redeem', { code: n.productKey }, false)).status, 200);                // the new one works
    assert.strictEqual((await post('/redeem', { code: b.productKey }, false)).status, 200);                // the other licence is untouched
    assert.strictEqual((await post('/admin/licenses/NOPE/product-key', {})).status, 404);
  } finally { server.close(); fsx.rmSync(dir, { recursive: true, force: true }); }
});
