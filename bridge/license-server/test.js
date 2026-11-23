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
    assert.strictEqual(u.ok, true); assert.strictEqual(u.version, '1.23.0.1'); assert.strictEqual(u.files[0].size, null); assert.deepStrictEqual(u.features, []); assert.strictEqual(u.signed, true); assert.strictEqual(u.files[0].platform, 'win-x64-exe'); assert.strictEqual(u.files[0].sha256.length, 64);
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

test('staff sign-in, JWT, staff accounts and the CRM (encrypted at rest)', async () => {
  const os = require('node:os'), fsx = require('node:fs'), pathx = require('node:path');
  const dir = fsx.mkdtempSync(pathx.join(os.tmpdir(), 'portal-')), portalDb = pathx.join(dir, 'portal.db');
  const srv = createServer({ privateKey: pair.privateKey, token: TOKEN, dataFile: pathx.join(dir, 'licenses.json'), portalDb, portalSecret: 'p'.repeat(48), quiet: true });
  await new Promise(r => srv.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${srv.address().port}`;
  const call = async (p, { method, body, tok } = {}) => { const r = await fetch(base + p, { method: method || (body ? 'POST' : 'GET'), headers: { ...(tok ? { authorization: 'Bearer ' + tok } : {}), ...(body ? { 'content-type': 'application/json' } : {}) }, body: body ? JSON.stringify(body) : undefined }); return { status: r.status, json: await r.json().catch(() => null), headers: r.headers }; };
  try {
    // the pages: each with its own CSP naming only the CDN hosts it uses, files pinned by SRI
    const login = await fetch(base + '/admin/login'); const lh = await login.text();
    assert.strictEqual(login.status, 200); assert.match(login.headers.get('content-security-policy'), /script-src 'self';/);
    assert.match(lh, /<script src="tailwind\.js"><\/script>/); assert.doesNotMatch(lh, /cdn\.tailwindcss\.com/);
    const tw = await fetch(base + '/admin/tailwind.js'); assert.strictEqual(tw.status, 200); assert.ok((await tw.text()).length > 100000, 'the local Tailwind copy is served'); assert.match(lh, /font-awesome\/6\.5\.2\/css\/all\.min\.css" integrity="sha384-/);
    const crm = await fetch(base + '/admin/crm'); const ch = await crm.text();
    assert.match(crm.headers.get('content-security-policy'), /script-src 'self' https:\/\/cdn\.jsdelivr\.net\/npm\/bootstrap@5\.3\.3\/dist\/;/); assert.ok(!/unsafe-inline/.test(crm.headers.get('content-security-policy')));
    assert.match(ch, /bootstrap@5\.3\.3\/dist\/css\/bootstrap\.min\.css" integrity="sha384-/); assert.ok(!/style="/.test(ch), 'no inline style attributes under a CSP without unsafe-inline');
    for (const f of ['/admin/login.js', '/admin/crm.js']) assert.strictEqual((await fetch(base + f)).status, 200);
    // nothing without a token; the first staff account is made with the admin token
    assert.strictEqual((await call('/admin/crm/customers')).status, 401);
    assert.strictEqual((await call('/admin/staff', { body: { email: 'a@b.co', name: 'A', password: 'long enough pass' } })).status, 401);
    assert.strictEqual((await call('/admin/staff', { body: { email: 'a@b.co', name: 'A', password: 'short' }, tok: TOKEN })).status, 400);
    const made = await call('/admin/staff', { body: { email: 'Ana@Studio.my', name: 'Ana', password: 'correct horse battery' }, tok: TOKEN });
    assert.strictEqual(made.status, 200);
    assert.strictEqual((await call('/admin/staff', { body: { email: 'ana@studio.my', name: 'Ana 2', password: 'correct horse battery' }, tok: TOKEN })).status, 409);
    // sign-in
    assert.strictEqual((await call('/admin/auth/login', { body: { email: 'ana@studio.my', password: 'wrong password!!' } })).status, 401);
    assert.strictEqual((await call('/admin/auth/login', { body: { email: 'nobody@studio.my', password: 'correct horse battery' } })).status, 401);
    const li = await call('/admin/auth/login', { body: { email: 'ANA@studio.my', password: 'correct horse battery' } });
    assert.strictEqual(li.status, 200); const jwt = li.json.token; assert.strictEqual(jwt.split('.').length, 3);
    assert.strictEqual((await call('/admin/auth/me', { tok: jwt })).json.staff.email, 'ana@studio.my');
    // the JWT opens the existing admin routes too (the licence dashboard) — and a forged or "none" token does not
    assert.strictEqual((await call('/admin/licenses', { tok: jwt })).status, 200);
    const [h, b] = jwt.split('.');
    const none = Buffer.from(JSON.stringify({ alg: 'none', typ: 'JWT' })).toString('base64url') + '.' + b + '.' + 'x'.repeat(43);
    assert.strictEqual((await call('/admin/licenses', { tok: none })).status, 401);
    const evil = h + '.' + Buffer.from(JSON.stringify({ ...JSON.parse(Buffer.from(b, 'base64url')), sub: 'stf_other' })).toString('base64url') + '.' + jwt.split('.')[2];
    assert.strictEqual((await call('/admin/licenses', { tok: evil })).status, 401);
    // CRM: every staff member edits; stored encrypted
    const add = await call('/admin/crm/customers', { body: { name: 'Studio Rimba', email: 'Boss@Rimba.my', phone: '+60 12 345', company: 'Rimba Sdn Bhd', status: 'trial' }, tok: jwt });
    assert.strictEqual(add.status, 200); const cid = add.json.id;
    assert.strictEqual((await call('/admin/crm/customers', { body: { name: '' }, tok: jwt })).status, 400);
    assert.strictEqual((await call('/admin/crm/customers', { body: { name: 'X', email: 'not-an-email' }, tok: jwt })).status, 400);
    assert.strictEqual((await call('/admin/crm/customers/' + cid + '/notes', { body: { text: 'Called, wants 3 seats' }, tok: jwt })).status, 200);
    const one = await call('/admin/crm/customers/' + cid, { tok: jwt });
    assert.strictEqual(one.json.customer.email, 'boss@rimba.my'); assert.strictEqual(one.json.customer.notes[0].text, 'Called, wants 3 seats'); assert.strictEqual(one.json.customer.notes[0].by, 'ana@studio.my');
    assert.strictEqual((await call('/admin/crm/customers/' + cid, { method: 'PUT', body: { name: 'Studio Rimba', email: 'boss@rimba.my', status: 'customer' }, tok: jwt })).status, 200);
    assert.strictEqual((await call('/admin/crm/customers?q=rimba', { tok: jwt })).json.customers.length, 1);
    assert.strictEqual((await call('/admin/crm/customers?status=lead', { tok: jwt })).json.customers.length, 0);
    assert.strictEqual((await call('/admin/crm/stats', { tok: jwt })).json.stats.customer, 1);
    srv.close(); await new Promise(r => setTimeout(r, 50));
    const raw = fsx.readFileSync(portalDb).toString('latin1') + (fsx.existsSync(portalDb + '-wal') ? fsx.readFileSync(portalDb + '-wal').toString('latin1') : '');
    for (const plain of ['Rimba', 'boss@rimba', '+60 12 345', 'wants 3 seats', 'correct horse']) assert.ok(!raw.includes(plain), 'stored in the clear: ' + plain);
    if (process.platform !== 'win32') assert.strictEqual(fsx.statSync(portalDb).mode & 0o077, 0);
    await new Promise(r => srv.listen(0, '127.0.0.1', r));
    const base2 = `http://127.0.0.1:${srv.address().port}`;
    // a disabled account's token stops at once; you cannot disable yourself
    const self = await fetch(base2 + '/admin/staff/' + made.json.id + '/disable', { method: 'POST', headers: { authorization: 'Bearer ' + jwt, 'content-type': 'application/json' }, body: '{}' });
    assert.strictEqual(self.status, 400);
    const dis = await fetch(base2 + '/admin/staff/' + made.json.id + '/disable', { method: 'POST', headers: { authorization: 'Bearer ' + TOKEN, 'content-type': 'application/json' }, body: '{}' });
    assert.strictEqual(dis.status, 200);
    assert.strictEqual((await fetch(base2 + '/admin/licenses', { headers: { authorization: 'Bearer ' + jwt } })).status, 401);
    // the password changes only with the current one; sign-out ends the token on the server
    await fetch(base2 + '/admin/staff/' + made.json.id + '/enable', { method: 'POST', headers: { authorization: 'Bearer ' + TOKEN, 'content-type': 'application/json' }, body: '{}' });
    const j2 = (await (await fetch(base2 + '/admin/auth/login', { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ email: 'ana@studio.my', password: 'correct horse battery' }) })).json()).token;
    const pw = (body) => fetch(base2 + '/admin/auth/password', { method: 'POST', headers: { authorization: 'Bearer ' + j2, 'content-type': 'application/json' }, body: JSON.stringify(body) });
    assert.strictEqual((await pw({ current: 'not it at all', password: 'a brand new password' })).status, 403);
    assert.strictEqual((await pw({ current: 'correct horse battery', password: 'short' })).status, 400);
    const out2 = await fetch(base2 + '/admin/auth/logout', { method: 'POST', headers: { authorization: 'Bearer ' + j2 } }); assert.strictEqual(out2.status, 200);
    assert.strictEqual((await fetch(base2 + '/admin/licenses', { headers: { authorization: 'Bearer ' + j2 } })).status, 401);
    // sign-in is rate limited
    let limited = 0; for (let i = 0; i < 14; i++) { const r = await fetch(base2 + '/admin/auth/login', { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ email: 'x@y.zz', password: 'nopenopenope' }) }); if (r.status === 429) limited++; }
    assert.ok(limited >= 2, 'sign-in is limited (' + limited + ')');
  } finally { srv.close(); fsx.rmSync(dir, { recursive: true, force: true }); }
});

test('SYSTEM page: health without secrets, validated settings, session ending, safe CSV export, public notice', async () => {
  const os = require('node:os'), fsx = require('node:fs'), pathx = require('node:path');
  const dir = fsx.mkdtempSync(pathx.join(os.tmpdir(), 'system-'));
  const JWT_SECRET = 'j'.repeat(48);
  const srv = createServer({ privateKey: pair.privateKey, token: TOKEN, dataFile: pathx.join(dir, 'licenses.json'), portalDb: pathx.join(dir, 'portal.db'), portalSecret: JWT_SECRET, quiet: true });
  await new Promise(r => srv.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${srv.address().port}`;
  const call = async (p, { method, body, tok } = {}) => { const r = await fetch(base + p, { method: method || (body ? 'POST' : 'GET'), headers: { ...(tok ? { authorization: 'Bearer ' + tok } : {}), ...(body ? { 'content-type': 'application/json' } : {}) }, body: body ? JSON.stringify(body) : undefined }); const t = await r.text(); let j = null; try { j = JSON.parse(t); } catch (_) {} return { status: r.status, json: j, text: t, headers: r.headers }; };
  try {
    const page = await fetch(base + '/admin/system'); assert.strictEqual(page.status, 200); assert.match(page.headers.get('content-security-policy'), /bootstrap@5\.3\.3\/dist\//);
    assert.ok(!/style="/.test(await page.text()));
    for (const p of ['/admin/system/info', '/admin/settings']) assert.strictEqual((await call(p)).status, 401);
    await call('/admin/staff', { body: { email: 'sam@amfb.my', name: 'Sam', password: 'sam long password' }, tok: TOKEN });
    const jwt = (await call('/admin/auth/login', { body: { email: 'sam@amfb.my', password: 'sam long password' } })).json.token;
    const info = await call('/admin/system/info', { tok: jwt });
    assert.strictEqual(info.status, 200); assert.strictEqual(info.json.you.email, 'sam@amfb.my'); assert.strictEqual(info.json.data.db.integrity, 'ok'); assert.ok(info.json.data.auditChain.ok);
    for (const secret of [TOKEN, JWT_SECRET, 'sam long password']) assert.ok(!info.text.includes(secret), 'a secret in /admin/system/info');
    // settings: defaults, validation of every kind, unknown keys refused, saved and audited
    const s0 = (await call('/admin/settings', { tok: jwt })).json; assert.strictEqual(s0.settings.defaultPlan, 'pro'); assert.ok(s0.schema.defaultDays);
    for (const bad of [{ defaultDays: -1 }, { defaultDays: 2.5 }, { defaultSeats: 9 }, { defaultPlan: 'gold' }, { supportEmail: 'nope' }, { maintenance: 'yes' }, { adminToken: 'x' }])
      assert.strictEqual((await call('/admin/settings', { method: 'PUT', body: bad, tok: jwt })).status, 400, JSON.stringify(bad));
    const ok = await call('/admin/settings', { method: 'PUT', body: { defaultPlan: 'studio', defaultDays: 90, supportEmail: 'Help@AMFB.my', portalNotice: '<b>Hi</b> all', maintenance: true }, tok: jwt });
    assert.strictEqual(ok.status, 200); assert.deepStrictEqual(ok.json.changed.sort(), ['defaultDays', 'defaultPlan', 'maintenance', 'portalNotice', 'supportEmail']);
    assert.strictEqual((await call('/admin/settings', { tok: TOKEN })).json.settings.supportEmail, 'help@amfb.my');
    assert.ok((await call('/admin/audit?limit=20', { tok: jwt })).json.entries.some(e => e.action === 'settings.change'));
    // the public notice carries those four fields and nothing else
    const n = (await call('/portal/notice')).json; assert.deepStrictEqual(Object.keys(n).sort(), ['maintenance', 'notice', 'ok', 'orgName', 'supportEmail']); assert.strictEqual(n.notice, '<b>Hi</b> all'); assert.strictEqual(n.maintenance, true);
    // CSV: a cell a spreadsheet would run as a formula is quoted as text
    await call('/admin/crm/customers', { body: { name: '=HYPERLINK("http://evil","x")', company: '+cmd', email: 'a@b.co', tags: '@sum' }, tok: jwt });
    const csv = await call('/admin/crm/export.csv', { tok: jwt });
    assert.strictEqual(csv.status, 200); assert.match(csv.headers.get('content-type'), /text\/csv/); assert.match(csv.headers.get('content-disposition'), /attachment/);
    assert.ok(csv.text.includes('"\'=HYPERLINK(""http://evil"",""x"")"') && csv.text.includes('"\'+cmd"') && csv.text.includes('"\'@sum"'));
    assert.strictEqual((await call('/admin/crm/export.csv')).status, 401);
    // ending a staff member's sessions stops their token
    const sam = (await call('/admin/staff', { tok: TOKEN })).json.staff.find(x => x.email === 'sam@amfb.my');
    assert.strictEqual((await call('/admin/staff/' + sam.id + '/end-sessions', { method: 'POST', body: {}, tok: TOKEN })).status, 200);
    assert.strictEqual((await call('/admin/system/info', { tok: jwt })).status, 401);
    assert.strictEqual((await call('/admin/staff/stf_none/end-sessions', { method: 'POST', body: {}, tok: TOKEN })).status, 404);
  } finally { srv.close(); fsx.rmSync(dir, { recursive: true, force: true }); }
});

test('CRM and portal extras: a key from a customer record, follow-ups, ending-soon CSV, database backup, sign-in page', async () => {
  const os = require('node:os'), fsx = require('node:fs'), pathx = require('node:path');
  const dir = fsx.mkdtempSync(pathx.join(os.tmpdir(), 'extras-'));
  const srv = createServer({ privateKey: pair.privateKey, token: TOKEN, dataFile: pathx.join(dir, 'licenses.json'), portalDb: pathx.join(dir, 'portal.db'), portalSecret: 'k'.repeat(48), quiet: true });
  await new Promise(r => srv.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${srv.address().port}`;
  const call = async (p, { method, body, tok } = {}) => { const r = await fetch(base + p, { method: method || (body ? 'POST' : 'GET'), headers: { ...(tok ? { authorization: 'Bearer ' + tok } : {}), ...(body ? { 'content-type': 'application/json' } : {}) }, body: body ? JSON.stringify(body) : undefined }); const buf = Buffer.from(await r.arrayBuffer()), t = buf.toString('utf8'); let j = null; try { j = JSON.parse(t); } catch (_) {} return { status: r.status, json: j, text: t, buf, headers: r.headers }; };
  try {
    await call('/admin/staff', { body: { email: 'kim@amfb.my', name: 'Kim', password: 'kim long password' }, tok: TOKEN });
    const jwt = (await call('/admin/auth/login', { body: { email: 'kim@amfb.my', password: 'kim long password' } })).json.token;
    await call('/admin/settings', { method: 'PUT', body: { defaultPlan: 'studio', defaultDays: 20, defaultSeats: 2, orgName: 'AMFB Audio Sdn Bhd' }, tok: TOKEN });
    // P1: a key straight from the record, with the Settings defaults, listed under the customer with its state
    const id = (await call('/admin/crm/customers', { body: { name: 'Studio Nord', company: 'Nord AB', email: 'Desk@Nord.example', status: 'customer' }, tok: jwt })).json.id;
    const noMail = (await call('/admin/crm/customers', { body: { name: 'No Mail' }, tok: jwt })).json.id;
    assert.strictEqual((await call('/admin/crm/customers/' + noMail + '/licence', { body: {}, tok: jwt })).status, 400);
    assert.strictEqual((await call('/admin/crm/customers/cus_none/licence', { body: {}, tok: jwt })).status, 404);
    assert.strictEqual((await call('/admin/crm/customers/' + id + '/licence', { body: {} })).status, 401);
    const k1 = await call('/admin/crm/customers/' + id + '/licence', { body: {}, tok: jwt });
    assert.strictEqual(k1.status, 200); assert.strictEqual(k1.json.plan, 'studio'); assert.strictEqual(k1.json.seats, 2); assert.ok(k1.json.expires > Date.now() + 19 * 86400000 && k1.json.expires < Date.now() + 21 * 86400000);
    assert.strictEqual(verifyKey(k1.json.key, { jwk }).ok, true);
    const k2 = await call('/admin/crm/customers/' + id + '/licence', { body: { plan: 'basic', days: 0, seats: 1 }, tok: jwt }); assert.strictEqual(k2.json.expires, null);
    assert.strictEqual((await call('/admin/crm/customers/' + id + '/licence', { body: { plan: 'gold' }, tok: jwt })).status, 400);
    await call('/admin/licenses/' + k2.json.id + '/revoke', { body: { reason: 'test' }, tok: TOKEN });
    const cu = (await call('/admin/crm/customers/' + id, { tok: jwt })).json.customer;
    assert.strictEqual(cu.licences.length, 2); const st = Object.fromEntries(cu.licences.map(l => [l.id, l.state])); assert.strictEqual(st[k1.json.id], 'active'); assert.strictEqual(st[k2.json.id], 'revoked');
    assert.ok(cu.notes.some(n => n.text.includes(k1.json.id)), 'the issue is noted on the customer');
    assert.ok((await call('/admin/audit?limit=50', { tok: TOKEN })).json.entries.some(e => e.action === 'license.issue' && String(e.detail || e.note || JSON.stringify(e)).includes('crm=')));
    // P2: follow-up days, validated; due today and overdue by the browser's day; lost customers left out
    for (const bad of ['tomorrow', '2026-02-30', '2026-1-5']) assert.strictEqual((await call('/admin/crm/customers/' + id, { method: 'PUT', body: { name: 'Studio Nord', email: 'desk@nord.example', status: 'customer', followUp: bad }, tok: jwt })).status, 400, bad);
    await call('/admin/crm/customers/' + id, { method: 'PUT', body: { name: 'Studio Nord', company: 'Nord AB', email: 'desk@nord.example', status: 'customer', followUp: '2026-11-18' }, tok: jwt });
    await call('/admin/crm/customers', { body: { name: 'Late One', followUp: '2026-11-01' }, tok: jwt });
    await call('/admin/crm/customers', { body: { name: 'Gone', followUp: '2026-10-01', status: 'lost' }, tok: jwt });
    await call('/admin/crm/customers', { body: { name: 'Later', followUp: '2026-12-24' }, tok: jwt });
    const due = (await call('/admin/crm/due?today=2026-11-18', { tok: jwt })).json;
    assert.deepStrictEqual(due.dueToday.map(c => c.name), ['Studio Nord']); assert.deepStrictEqual(due.overdue.map(c => c.name), ['Late One']);
    assert.strictEqual((await call('/admin/crm/due?today=nonsense', { tok: jwt })).status, 200);
    assert.strictEqual((await call('/admin/crm/due')).status, 401);
    assert.ok((await call('/admin/crm/export.csv', { tok: jwt })).text.includes('follow-up'));
    // P3: the ending-soon list as CSV: only active keys inside the window, formula cells quoted
    const evil = (await call('/admin/licenses', { body: { name: '=cmd|calc', email: 'x@y.co', plan: 'pro', days: 5 }, tok: TOKEN })).json.id;
    await call('/admin/licenses', { body: { name: 'Far Away', plan: 'pro', days: 300 }, tok: TOKEN });
    const csv = await call('/admin/licenses/ending.csv?days=30', { tok: jwt });
    assert.strictEqual(csv.status, 200); assert.match(csv.headers.get('content-type'), /text\/csv/); assert.match(csv.headers.get('content-disposition'), /ending-soon/);
    const lines = csv.text.trim().split('\r\n'); assert.strictEqual(lines[0].replace('﻿', ''), 'id,customer,email,plan,trial,ends,days left,computers');
    assert.ok(lines.some(l => l.includes(evil) && l.includes('"\'=cmd|calc"')), 'formula quoted'); assert.ok(lines.some(l => l.includes(k1.json.id)));
    assert.ok(!csv.text.includes('Far Away') && !csv.text.includes(k2.json.id), 'only active keys inside the window');
    assert.strictEqual((await call('/admin/licenses/ending.csv')).status, 401);
    // P4: a database backup: admin or staff, audited, a real SQLite file, the last backup time shown
    assert.strictEqual((await call('/admin/system/backup')).status, 401);
    assert.strictEqual((await call('/admin/system/info', { tok: jwt })).json.data.db.lastBackup, null);
    const bk = await call('/admin/system/backup', { tok: jwt });
    assert.strictEqual(bk.status, 200); assert.strictEqual(bk.buf.subarray(0, 16).toString('latin1'), 'SQLite format 3\u0000'); assert.match(bk.headers.get('content-disposition'), /amfx1-portal-\d{4}-\d{2}-\d{2}\.db/);
    assert.ok(!bk.buf.includes(Buffer.from('desk@nord.example')), 'the CRM stays encrypted inside the backup');
    const after = (await call('/admin/system/info', { tok: jwt })).json.data.db.lastBackup; assert.ok(after && after.at > 0 && after.by === 'kim@amfb.my');
    assert.ok((await call('/admin/audit?limit=20', { tok: TOKEN })).json.entries.some(e => e.action === 'system.backup'));
    assert.ok(!fsx.readdirSync(os.tmpdir()).some(f => f.startsWith('amfx1-portal-backup-')), 'no backup copy left in the temp folder');
    // P5: the sign-in page: the company name comes from Settings; the e-mail is remembered in the browser only, never the password
    assert.strictEqual((await call('/portal/notice')).json.orgName, 'AMFB Audio Sdn Bhd');
    const login = (await call('/admin/login')).text, js = (await call('/admin/login.js')).text;
    assert.ok(login.includes('id="remember"') && login.includes('id="org"'));
    assert.ok(js.includes("localStorage.setItem(KEY, $('email').value.trim())") && !/localStorage\.setItem\([^)]*(pw|token)/.test(js));
  } finally { srv.close(); fsx.rmSync(dir, { recursive: true, force: true }); }
});

test('WEBSITE page: staff choose the release, hide files and publish; the portal offers only visible files; no path from outside the listing', async () => {
  const os = require('node:os'), fsx = require('node:fs'), pathx = require('node:path');
  const dir = fsx.mkdtempSync(pathx.join(os.tmpdir(), 'website-')), rel = pathx.join(dir, 'releases');
  for (const d of ['windows', 'android']) fsx.mkdirSync(pathx.join(rel, d), { recursive: true });
  fsx.writeFileSync(pathx.join(rel, 'features.json'), JSON.stringify({ features: [{ version: '1.25.0.0', date: '2026-11-20', title: 'new', items: ['a'] }, { version: '1.24.0.0', date: '2026-11-01', title: 'old', items: [] }] }));
  fsx.writeFileSync(pathx.join(rel, 'windows', 'AmF-X1-1.25.0.0-setup.exe'), 'setup');
  fsx.writeFileSync(pathx.join(rel, 'windows', 'AmF-X1-1.25.0.0-windows-x64.zip'), 'zip');
  fsx.writeFileSync(pathx.join(rel, 'windows', 'AmF X1.exe'), 'unversioned');
  fsx.writeFileSync(pathx.join(rel, 'windows', 'SHA256SUMS.txt'), 'f'.repeat(64) + '  AmF-X1-1.25.0.0-setup.exe\n');
  fsx.writeFileSync(pathx.join(rel, 'android', 'AmF-X1-1.25.0.0-b2-android.apk'), 'apk');
  fsx.writeFileSync(pathx.join(dir, 'secret.txt'), 'do not serve');
  const srv = createServer({ privateKey: pair.privateKey, token: TOKEN, dataFile: pathx.join(dir, 'licenses.json'), portalDb: pathx.join(dir, 'portal.db'), portalSecret: 'w'.repeat(48), releasesDir: rel, quiet: true });
  await new Promise(r => srv.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${srv.address().port}`;
  const call = async (p, { method, body, tok } = {}) => { const r = await fetch(base + p, { method: method || (body ? 'POST' : 'GET'), headers: { ...(tok ? { authorization: 'Bearer ' + tok } : {}), ...(body ? { 'content-type': 'application/json' } : {}) }, body: body ? JSON.stringify(body) : undefined }); const t = await r.text(); let j = null; try { j = JSON.parse(t); } catch (_) {} return { status: r.status, json: j, text: t, headers: r.headers }; };
  try {
    // the page itself is a fixed file; every call behind it needs a staff sign-in or the admin token
    const page = await call('/admin/website'); assert.strictEqual(page.status, 200); assert.ok(page.text.includes('website.js') && !page.text.includes(TOKEN));
    assert.strictEqual((await call('/admin/website/state')).status, 401);
    assert.strictEqual((await call('/admin/website/state', { method: 'PUT', body: { version: '1.25.0.0' } })).status, 401);
    assert.strictEqual((await call('/admin/website/publish', { method: 'POST' })).status, 401);
    await call('/admin/staff', { body: { email: 'web@amfb.my', name: 'Web', password: 'web long password' }, tok: TOKEN });
    const jwt = (await call('/admin/auth/login', { body: { email: 'web@amfb.my', password: 'web long password' } })).json.token;
    // nothing published yet: the newest version with every file of that version, never the unversioned one
    let pub = (await call('/portal/downloads')).json;
    assert.strictEqual(pub.version, '1.25.0.0'); assert.deepStrictEqual(pub.files.map(f => f.name).sort(), ['AmF-X1-1.25.0.0-b2-android.apk', 'AmF-X1-1.25.0.0-setup.exe', 'AmF-X1-1.25.0.0-windows-x64.zip']);
    const st = (await call('/admin/website/state', { tok: jwt })).json;
    assert.strictEqual(st.files.find(f => f.name === 'AmF-X1-1.25.0.0-setup.exe').sha256, 'f'.repeat(64));   // from SHA256SUMS.txt
    assert.strictEqual(st.files.find(f => f.name.endsWith('.apk')).sha256, crypto.createHash('sha256').update('apk').digest('hex'));   // computed
    // bad input is refused
    for (const body of [{ version: '9.9.9.9' }, { version: '1.25.0.0', hidden: ['../secret.txt'] }, { version: '1.25.0.0', hidden: ['../../licenses.json'] }, { version: '1.25.0.0', hidden: 'x' }, { version: '1.25.0.0', headline: 'h'.repeat(121) }, { version: '1.25.0.0', note: 'n'.repeat(601) }])
      assert.strictEqual((await call('/admin/website/state', { method: 'PUT', body, tok: jwt })).status, 400, JSON.stringify(body).slice(0, 60));
    // hide the zip, save (not public yet), then publish
    assert.strictEqual((await call('/admin/website/state', { method: 'PUT', body: { version: '1.25.0.0', hidden: ['AmF-X1-1.25.0.0-windows-x64.zip'], headline: 'Get AmF X1', note: 'Plain note.' }, tok: jwt })).status, 200);
    assert.strictEqual((await call('/portal/downloads')).json.files.length, 3);
    const p = await call('/admin/website/publish', { method: 'POST', tok: jwt }); assert.strictEqual(p.status, 200);
    pub = (await call('/portal/downloads')).json;
    assert.strictEqual(pub.headline, 'Get AmF X1'); assert.strictEqual(pub.note, 'Plain note.');
    assert.ok(!pub.files.some(f => f.name.endsWith('.zip')) && pub.files.length === 2);
    const setup = pub.files.find(f => f.name.endsWith('setup.exe')); assert.strictEqual(setup.size, 5); assert.strictEqual(setup.platform, 'windows');
    const got = await call(setup.path); assert.strictEqual(got.status, 200); assert.strictEqual(got.text, 'setup'); assert.match(got.headers.get('content-disposition'), /attachment/);
    // a hidden file, an unversioned one and anything outside the listing are not given out
    for (const u of ['/portal/download/windows/AmF-X1-1.25.0.0-windows-x64.zip', '/portal/download/windows/' + encodeURIComponent('AmF X1.exe'), '/portal/download/windows/..%2F..%2Fsecret.txt', '/portal/download/windows/%2E%2E%2Fsecret.txt', '/portal/download/../secret.txt', '/portal/download/linux/x.tar.gz'])
      assert.ok([404, 405].includes((await call(u)).status), u);
    const log = (await call('/admin/audit?limit=50', { tok: TOKEN })).json.entries;
    assert.ok(log.some(e => e.action === 'website.save' && /web@amfb\.my/.test(e.detail || '')) && log.some(e => e.action === 'website.publish' && /web@amfb\.my/.test(e.detail || '')));
    const js = (await call('/portal/app.js')).text; assert.ok(js.includes('/portal/downloads') && !js.includes('/admin') && js.includes('1.25.0.0'));
    assert.ok((await call('/portal/')).text.includes('placeholder="1.25.0.0"'));
  } finally { srv.close(); fsx.rmSync(dir, { recursive: true, force: true }); }
});

test('PRESETS on the WEBSITE page: staff publish a preset pack, /portal/presets serves it byte for byte; trusted only when the signed manifest has its sha256', async () => {
  const os = require('node:os'), fsx = require('node:fs'), pathx = require('node:path');
  const dir = fsx.mkdtempSync(pathx.join(os.tmpdir(), 'presets-')), rel = pathx.join(dir, 'releases');
  fsx.mkdirSync(pathx.join(rel, 'presets'), { recursive: true });
  fsx.writeFileSync(pathx.join(rel, 'features.json'), JSON.stringify({ features: [{ version: '1.25.0.0', date: '2026-11-20', title: 'new', items: [] }] }));
  const pack = v => ({ version: v, generated: '2026-11-20', mastering: [{ name: 'WARM', params: { low: 2.5, air: -1, on: true } }], effects: [{ kind: 'reverb', id: 1, name: 'Small Booth', category: 'Reverb', values: { decay: 0.06, mix: 1 } }], tone: [{ kind: 'chain', name: 'FLAT', category: 'UTILITY', dsp: {} }] });
  const text = p => JSON.stringify(p, null, 1) + '\n', sha = t => crypto.createHash('sha256').update(t).digest('hex');
  fsx.writeFileSync(pathx.join(rel, 'presets', 'presets-pack.json'), text(pack('1.0')));
  const srv = createServer({ privateKey: pair.privateKey, token: TOKEN, dataFile: pathx.join(dir, 'licenses.json'), portalDb: pathx.join(dir, 'portal.db'), portalSecret: 'p'.repeat(48), releasesDir: rel, quiet: true });
  await new Promise(r => srv.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${srv.address().port}`;
  const call = async (p, { method, body, tok } = {}) => { const r = await fetch(base + p, { method: method || (body ? 'POST' : 'GET'), headers: { ...(tok ? { authorization: 'Bearer ' + tok } : {}), ...(body ? { 'content-type': 'application/json' } : {}) }, body: body ? JSON.stringify(body) : undefined });
    const t = await r.text(); let j = null; try { j = JSON.parse(t); } catch (_) {} return { status: r.status, text: t, json: j, headers: r.headers }; };
  try {
    // staff only: no token, a wrong token
    assert.strictEqual((await call('/admin/website/presets')).status, 401);
    assert.strictEqual((await call('/admin/website/presets', { body: { pack: pack('2.0') } })).status, 401);
    assert.strictEqual((await call('/admin/website/presets', { body: { pack: pack('2.0') }, tok: 'y'.repeat(40) })).status, 401);
    await call('/admin/staff', { body: { email: 'presets@amfb.my', name: 'Presets', password: 'presets long password' }, tok: TOKEN });
    const jwt = (await call('/admin/auth/login', { body: { email: 'presets@amfb.my', password: 'presets long password' } })).json.token;
    // nothing published: the release folder's pack
    let pub = await call('/portal/presets'); assert.strictEqual(pub.status, 200); assert.strictEqual(pub.text, text(pack('1.0'))); assert.strictEqual(pub.headers.get('x-preset-sha256'), sha(text(pack('1.0'))));
    // bad packs are refused
    for (const bad of [null, { version: '2' }, { ...pack('2'), version: '../x' }, { ...pack('2'), mastering: [{ name: '<b>', params: {} }] }, { ...pack('2'), effects: [{ kind: 'reverb', id: 'x', name: 'n', values: {} }] }, { ...pack('2'), mastering: [{ name: 'A', params: { x: 'str' } }] }])
      assert.strictEqual((await call('/admin/website/presets', { body: { pack: bad }, tok: jwt })).status, 400, JSON.stringify(bad).slice(0, 60));
    // a big pack (more than the server's usual 4 KB body) is published; the portal serves exactly the text make-presets.js writes
    const big = pack('2.0'); for (let i = 2; i <= 600; i++) big.effects.push({ kind: 'delay', id: i, name: 'Echo ' + i, category: 'Delay', values: { time: i / 600, fb: 0.4, mix: 0.35 } });
    let r = await call('/admin/website/presets', { body: { pack: big }, tok: jwt }); assert.strictEqual(r.status, 200, r.text); assert.strictEqual(r.json.trusted, false);
    pub = await call('/portal/presets'); assert.strictEqual(pub.text, text(big)); assert.strictEqual(r.json.live.sha256, sha(text(big))); assert.strictEqual(pub.json.effects.length, 600);
    let st = (await call('/admin/website/presets', { tok: jwt })).json; assert.strictEqual(st.live.version, '2.0'); assert.strictEqual(st.live.effects, 600); assert.strictEqual(st.trusted, false); assert.strictEqual(st.live.publishedBy, 'presets@amfb.my');
    // the owner signs a manifest with this sha256: the page says the apps take it
    fsx.writeFileSync(pathx.join(rel, 'update.json'), JSON.stringify({ payload: JSON.stringify({ product: 'audio-mixer', version: '1.25.0.0', files: {}, presets: { version: '2.0', sha256: sha(text(big)) } }), signature: 'x' }));
    st = (await call('/admin/website/presets', { tok: jwt })).json; assert.strictEqual(st.trusted, true); assert.strictEqual(st.manifest.signed.version, '2.0');
    // the release folder's pack again
    r = await call('/admin/website/presets', { body: { fromBuiltIn: true }, tok: jwt }); assert.strictEqual(r.status, 200); assert.strictEqual((await call('/portal/presets')).text, text(pack('1.0')));
    const log = (await call('/admin/audit?limit=50', { tok: TOKEN })).json.entries;
    assert.ok(log.some(e => e.action === 'website.presets.publish' && /presets@amfb\.my/.test(e.detail || '')));
    const page = await call('/admin/website'); assert.ok(page.text.includes('packPublish'));
  } finally { srv.close(); fsx.rmSync(dir, { recursive: true, force: true }); }
});
