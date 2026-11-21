'use strict';
// END-TO-END test of the portal without Docker: starts deploy/portal/start.js (licence server + OTA server) on random localhost ports with a
// throwaway P-256 key pair made in a temp folder (never the vendor key, never committed), then walks every public and staff flow.
//   node --test --test-force-exit deploy/portal/test/e2e.test.js
// No network beyond 127.0.0.1. The temp folder is removed at the end.
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const net = require('node:net');
const crypto = require('node:crypto');
const { spawn, spawnSync } = require('node:child_process');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const lic = require(path.join(ROOT, 'bridge', 'license'));
const update = require(path.join(ROOT, 'bridge', 'update'));
const { browserCommand } = require(path.join(ROOT, 'scripts', 'license-generator'));

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.unref(); s.on('error', rej); s.listen(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => res(p)); }); });
const rand = () => crypto.randomBytes(48).toString('base64url');
const sha = b => crypto.createHash('sha256').update(b).digest('hex');
const b64uJson = s => JSON.parse(Buffer.from(s, 'base64url').toString('utf8'));
// checks an AMXA1 / AMXR1 token (prefix.body.sig, ES256 over the body) with the test public key
const tokenOk = (t, prefix, jwk) => { const [p, body, sig] = String(t).split('.'); return p === prefix && crypto.verify('sha256', Buffer.from(body), { key: crypto.createPublicKey({ key: jwk, format: 'jwk' }), dsaEncoding: 'ieee-p1363' }, Buffer.from(sig, 'base64url')) ? b64uJson(body) : null; };

let dir, child, L, O, env, jwk, privateKey, logs = '';
async function req(base, p, { method = 'GET', token, body, headers = {}, raw } = {}) {
  const h = { ...headers }; if (token) h.authorization = 'Bearer ' + token;
  let data = body; if (body !== undefined && !raw) { data = JSON.stringify(body); h['content-type'] = 'application/json'; }
  const r = await fetch(base + p, { method, headers: h, body: data, redirect: 'manual' });
  const buf = Buffer.from(await r.arrayBuffer()); let json = null; try { json = JSON.parse(buf.toString('utf8')); } catch (_) { /* not JSON */ }
  return { status: r.status, headers: r.headers, buf, json };
}
// a GET with the path exactly as written (fetch would clean up "..", "%2e%2e" before sending): for the path traversal checks
const rawGet = (base, p) => new Promise((res, rej) => { const u = new URL(base); require('node:http').get({ host: u.hostname, port: u.port, path: p }, r => { const b = []; r.on('data', d => b.push(d)); r.on('end', () => res({ status: r.statusCode, buf: Buffer.concat(b) })); }).on('error', rej); });
async function waitUp(url, ms = 15000) {
  const end = Date.now() + ms;
  while (Date.now() < end) { try { const r = await fetch(url); if (r.ok) return; } catch (_) { /* not yet */ } await new Promise(r => setTimeout(r, 100)); }
  throw new Error('did not start: ' + url + '\n' + logs);
}

test.before(async () => {
  dir = fs.mkdtempSync(path.join(os.tmpdir(), 'amfx1-portal-e2e-'));
  const pair = crypto.generateKeyPairSync('ec', { namedCurve: 'P-256' });
  privateKey = pair.privateKey; jwk = pair.publicKey.export({ format: 'jwk' });
  fs.mkdirSync(path.join(dir, 'secrets'), { mode: 0o700 });
  fs.writeFileSync(path.join(dir, 'secrets', 'vendor-private.pem'), pair.privateKey.export({ type: 'pkcs8', format: 'pem' }), { mode: 0o600 });
  fs.writeFileSync(path.join(dir, 'secrets', 'licence-data.key'), rand(), { mode: 0o600 });
  fs.writeFileSync(path.join(dir, 'test-public.json'), JSON.stringify(jwk));
  // the files the customer portal offers, and a preset pack
  const site = path.join(dir, 'site-releases'); fs.mkdirSync(path.join(site, 'windows'), { recursive: true }); fs.mkdirSync(path.join(site, 'presets'));
  fs.writeFileSync(path.join(site, 'features.json'), JSON.stringify({ features: [{ version: '9.9.9.9', date: '2026-11-21', title: 'Test release', items: ['one'] }] }));
  fs.writeFileSync(path.join(site, 'windows', 'AmF-X1-9.9.9.9-setup.exe'), crypto.randomBytes(5000));
  fs.writeFileSync(path.join(site, 'presets', 'presets-pack.json'), JSON.stringify({ version: '2026.11.21', mastering: [{ name: 'Loud', params: { gain: 1 } }], effects: [{ kind: 'eq', id: 1, name: 'Bright', values: { hi: 2 } }], tone: [{ kind: 'warm', name: 'Warm' }] }));
  const lp = await freePort(), op = await freePort();
  L = 'http://127.0.0.1:' + lp; O = 'http://127.0.0.1:' + op;
  env = { LICENSE_ADMIN_TOKEN: rand(), OTA_ADMIN_TOKEN: rand(), PORTAL_JWT_SECRET: rand(), PORTAL_CRM_SECRET: rand() };
  fs.writeFileSync(path.join(dir, '.env'), [
    ...Object.entries(env).map(([k, v]) => `${k}=${v}`),
    'LICENSE_PRIVATE_KEY_FILE=secrets/vendor-private.pem', 'LICENSE_DATA_KEY_FILE=secrets/licence-data.key', 'DATA_DIR=data', 'BIND_HOST=127.0.0.1',
    `LICENSE_PORT=${lp}`, `OTA_PORT=${op}`, 'TRUST_PROXY=0', 'LICENSE_RELEASES_DIR=site-releases', `OTA_PUBLIC_URL=${O}`, `LICENSE_UPDATE_URL=${O}/update.json`,
    'OTA_ALLOW_HTTP=1', 'OTA_PUBLIC_KEY_FILE=test-public.json', 'OTA_RATE=40', '',
  ].join('\n'), { mode: 0o600 });
  child = spawn(process.execPath, [path.join(ROOT, 'deploy', 'portal', 'start.js'), '--env', path.join(dir, '.env')], { stdio: ['ignore', 'pipe', 'pipe'] });
  child.stdout.on('data', d => { logs += d; }); child.stderr.on('data', d => { logs += d; });
  await waitUp(L + '/health'); await waitUp(O + '/healthz');
});
test.after(() => {
  if (child && child.exitCode === null) child.kill('SIGTERM');
  if (dir) fs.rmSync(dir, { recursive: true, force: true });
});

test('health of both servers, and no secret in the log', async () => {
  assert.equal((await req(L, '/health')).json.ok, true);
  assert.equal((await req(O, '/healthz')).json.ok, true);
  for (const v of Object.values(env)) assert.ok(!logs.includes(v), 'a secret was printed');
});

test('staff routes refuse without a sign-in', async () => {
  for (const [base, p, method] of [[L, '/admin/licenses', 'GET'], [L, '/admin/staff', 'GET'], [L, '/issue', 'POST'], [L, '/admin/system/backup', 'GET'], [L, '/admin/website/presets', 'POST'], [L, '/admin/crm/customers', 'GET'],
    [O, '/admin/files', 'GET'], [O, '/admin/manifest/stable', 'PUT'], [O, '/admin/audit', 'GET']]) {
    const r = await req(base, p, { method, body: method === 'GET' ? undefined : {} });
    assert.ok([401, 429].includes(r.status), method + ' ' + p + ' ' + r.status);   // 429 once one address has failed 10 times a minute
    assert.ok(!r.json || !r.json.licenses, 'no data');
    const forged = await req(base, p, { method, token: 'eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiJ4In0.AAAA', body: method === 'GET' ? undefined : {} });
    assert.ok([401, 429].includes(forged.status), 'forged JWT ' + p);
  }
});

let staffToken;
test('first staff account (admin token), sign-in, one sign-in opens both dashboards', async () => {
  const add = spawnSync(process.execPath, [path.join(ROOT, 'deploy', 'portal', 'tools', 'add-staff.js'), '--env', path.join(dir, '.env'), 'owner@example.com', 'Owner'], { env: { ...process.env, LICENSE_ADMIN_TOKEN: '', STAFF_PASSWORD: 'correct horse battery staple' }, encoding: 'utf8' });
  assert.equal(add.status, 0, add.stderr);
  const bad = await req(L, '/admin/auth/login', { method: 'POST', body: { email: 'owner@example.com', password: 'wrong password!!' } });
  assert.equal(bad.status, 401);
  const r = await req(L, '/admin/auth/login', { method: 'POST', body: { email: 'owner@example.com', password: 'correct horse battery staple' } });
  assert.equal(r.status, 200); staffToken = r.json.token; assert.ok(r.json.expiresIn > 0 && r.json.expiresIn <= 12 * 3600, 'the sign-in ends');
  assert.equal((await req(L, '/admin/auth/me', { token: staffToken })).json.staff.email, 'owner@example.com');
  assert.equal((await req(O, '/admin/files', { token: staffToken })).status, 200);
  const pages = await req(L, '/admin/login');
  assert.equal(pages.status, 200); assert.match(pages.headers.get('content-security-policy'), /frame-ancestors 'none'/);
  assert.equal(pages.headers.get('x-content-type-options'), 'nosniff');
});

let issued;
test('issue a licence key; activate, status, deactivate; product key; revocation list', async () => {
  const r = await req(L, '/admin/licenses', { method: 'POST', token: staffToken, body: { name: 'Test Customer', email: 'c@example.com', plan: 'pro', seats: 2, days: 365 } });
  assert.equal(r.status, 200, JSON.stringify(r.json)); issued = r.json;
  assert.ok(lic.verifyKey(issued.key, { jwk }).ok, 'the key verifies with the test public key');
  assert.ok(!lic.verifyKey(issued.key).ok, 'and NOT with the shipped vendor key (a test key never makes real licences)');
  const m1 = 'TESTPC-0000-0001', m2 = 'TESTPC-0000-0002', m3 = 'TESTPC-0000-0003';
  assert.equal((await req(L, '/activate', { method: 'POST', body: { key: issued.key, machine: m1 } })).status, 200);
  assert.equal((await req(L, '/activate', { method: 'POST', body: { key: issued.key, machine: m2 } })).status, 200);
  assert.equal((await req(L, '/activate', { method: 'POST', body: { key: issued.key, machine: m3 } })).status, 409, 'seat limit');
  let st = (await req(L, '/status', { method: 'POST', body: { key: issued.key, machine: m1 } })).json;
  assert.equal(st.devices, 2); assert.equal(st.thisComputer, true); assert.equal(st.revoked, false);
  assert.equal((await req(L, '/deactivate', { method: 'POST', body: { key: issued.key, machine: m1 } })).status, 200);
  st = (await req(L, '/status', { method: 'POST', body: { key: issued.key, machine: m1 } })).json; assert.equal(st.devices, 1); assert.equal(st.thisComputer, false);
  // a tampered key is refused
  const parts = issued.key.split('.'); const forged = parts[0] + '.' + Buffer.from(JSON.stringify({ ...b64uJson(parts[1]), seats: 4 })).toString('base64url') + '.' + parts[2];
  assert.equal((await req(L, '/activate', { method: 'POST', body: { key: forged, machine: m3 } })).status, 400);
  // product key -> activation bound to this computer, signed
  const on = await req(L, '/activate-online', { method: 'POST', body: { code: issued.productKey, machine: m1 } });
  assert.equal(on.status, 200, JSON.stringify(on.json));
  const act = tokenOk(on.json.activation, 'AMXA1', jwk); assert.ok(act); assert.equal(act.lid, issued.id); assert.equal(act.mid, m1);
  // revoke -> on the signed list, and activation refused
  assert.equal((await req(L, `/admin/licenses/${issued.id}/revoke`, { method: 'POST', token: staffToken, body: { reason: 'test' } })).status, 200);
  const list = tokenOk((await req(L, '/revocations')).json.list, 'AMXR1', jwk); assert.ok(list); assert.ok(list.lids.includes(issued.id));
  assert.equal((await req(L, '/activate', { method: 'POST', body: { key: issued.key, machine: m3 } })).status, 403);
  assert.equal((await req(L, '/status', { method: 'POST', body: { key: issued.key } })).json.revoked, true);
  // the audit chain holds and carries no key
  const audit = await req(L, '/admin/audit/verify', { token: staffToken }); assert.equal(audit.json.chain.ok, true);
  assert.ok(!JSON.stringify((await req(L, '/admin/audit?limit=500', { token: staffToken })).json).includes(issued.key.split('.')[2]));
});

test('OTA: upload a release, publish a signed manifest, fetch + verify it, download with checksum and ranges', async () => {
  const file = crypto.randomBytes(200000), name = 'AmF-X1-9.9.9.9-setup.exe', hash = sha(file);
  const wrong = await req(O, '/admin/files/' + name, { method: 'PUT', token: env.OTA_ADMIN_TOKEN, body: file, raw: true, headers: { 'x-sha256': '0'.repeat(64) } });
  assert.equal(wrong.status, 400, 'a damaged upload is refused');
  const up = await req(O, '/admin/files/' + name, { method: 'PUT', token: env.OTA_ADMIN_TOKEN, body: file, raw: true, headers: { 'x-sha256': hash } });
  assert.equal(up.status, 201, JSON.stringify(up.json));
  const manifest = { product: 'audio-mixer', version: '9.9.9.9', channel: 'stable', released: '2026-11-21', notes: ['test'], files: { 'win-x64-exe': { name, url: O + '/releases/' + name, sha256: hash, size: file.length } } };
  const otherKey = crypto.generateKeyPairSync('ec', { namedCurve: 'P-256' }).privateKey;
  assert.equal((await req(O, '/admin/manifest/stable', { method: 'PUT', token: staffToken, body: update.signManifest(manifest, otherKey) })).status, 400, 'a manifest signed by another key is refused');
  const pub = await req(O, '/admin/manifest/stable', { method: 'PUT', token: staffToken, body: update.signManifest(manifest, privateKey) });
  assert.equal(pub.status, 200, JSON.stringify(pub.json));
  const env1 = (await req(O, '/update.json')).json, v = update.verifyManifest(env1, jwk);
  assert.ok(v.ok); assert.equal(v.manifest.version, '9.9.9.9');
  const dl = await req(O, '/releases/' + name); assert.equal(dl.status, 200); assert.equal(sha(dl.buf), v.manifest.files['win-x64-exe'].sha256);
  const part = await req(O, '/releases/' + name, { headers: { range: 'bytes=100-199' } }); assert.equal(part.status, 206); assert.deepEqual(part.buf, file.subarray(100, 200));
  const latest = await req(O, '/api/latest?os=windows&arch=x64'); assert.equal(latest.json.file.name, name);
  for (const p of ['/releases/..%2F..%2Fetc%2Fpasswd', '/releases/%2e%2e%2findex.json', '/releases/index.json', '/releases/../index.json', '/../../etc/passwd', '/stable%2F..%2F..%2Fupdate.json'])
    { const r = await rawGet(O, p); assert.equal(r.status, 404, 'path traversal ' + p + ' ' + r.buf.slice(0, 80)); }
});

test('customer portal: published downloads with checksum, presets, path traversal refused', async () => {
  const d = (await req(L, '/portal/downloads')).json; assert.equal(d.version, '9.9.9.9');
  const f = d.files[0]; assert.equal(f.name, 'AmF-X1-9.9.9.9-setup.exe');
  const dl = await req(L, f.path); assert.equal(dl.status, 200); assert.equal(sha(dl.buf), f.sha256);
  for (const p of ['/portal/download/windows/..%2F..%2F.env', '/portal/download/..%2Fsecrets/vendor-private.pem', '/portal/download/presets/presets-pack.json', '/portal/download/windows/%2e%2e', '/portal/download/windows/../../../.env', '/portal/download/windows/..'])
    assert.equal((await rawGet(L, p)).status, 404, p);
  const pr = await req(L, '/portal/presets'); assert.equal(pr.status, 200); assert.equal(pr.headers.get('x-preset-sha256'), sha(pr.buf));
  assert.equal((await req(L, '/portal/updates')).json.version, '9.9.9.9', 'the portal reads the OTA manifest');
  const page = await req(L, '/portal/'); assert.equal(page.status, 200); assert.match(page.headers.get('content-security-policy'), /default-src 'none'/);
});

test('errors do not leak, oversize bodies are refused', async () => {
  const big = await req(L, '/activate', { method: 'POST', body: { key: 'x'.repeat(10000), machine: 'TESTPC-0000-0001' } }); assert.equal(big.status, 413);
  const junk = await req(L, '/status', { method: 'POST', body: '{not json', raw: true, headers: { 'content-type': 'application/json' } }); assert.equal(junk.status, 400);
  assert.ok(!/\/|Error|at /.test(junk.json.error), 'no path or stack in the error');
});

test('rate limits kick in (staff sign-in, public API, OTA downloads)', async () => {
  let last = 0; for (let i = 0; i < 12; i++) last = (await req(L, '/admin/auth/login', { method: 'POST', body: { email: 'owner@example.com', password: 'nope nope nope' } })).status;
  assert.equal(last, 429);
  let pub = 0; for (let i = 0; i < 35; i++) pub = (await req(L, '/status', { method: 'POST', body: { key: 'a.b.c' } })).status;
  assert.equal(pub, 429);
  let dl = 0; for (let i = 0; i < 45; i++) dl = (await req(O, '/api/latest?os=windows')).status;
  assert.equal(dl, 429);
});

test('security fixes: browser opener, short OTA token, readable private key file', async () => {
  // the generator only ever opens its own page, never through cmd.exe
  assert.deepEqual(browserCommand('http://127.0.0.1:8790/', 'win32'), ['rundll32.exe', ['url.dll,FileProtocolHandler', 'http://127.0.0.1:8790/']]);
  for (const bad of ['http://127.0.0.1:8790/&calc', 'http://127.0.0.1:8790/" & calc', 'https://evil.example/', 'file:///etc/passwd', 'http://127.0.0.1:1/|x', null]) assert.equal(browserCommand(bad, 'win32'), null, String(bad));
  // the OTA server refuses a short admin token
  const short = spawnSync(process.execPath, [path.join(ROOT, 'ota-server', 'server.js'), '--port', '0', '--data', path.join(dir, 'ota-short')], { env: { PATH: process.env.PATH, OTA_ADMIN_TOKEN: 'short' }, encoding: 'utf8', timeout: 10000 });
  assert.notEqual(short.status, 0); assert.match(short.stderr, /at least 32 characters/); assert.ok(!short.stderr.includes('short\n'));
  // ...and a private key there instead of a public one
  fs.writeFileSync(path.join(dir, 'priv.jwk'), JSON.stringify(privateKey.export({ format: 'jwk' })));
  const priv = spawnSync(process.execPath, [path.join(ROOT, 'ota-server', 'server.js'), '--port', '0', '--data', path.join(dir, 'ota-priv')], { env: { PATH: process.env.PATH, OTA_PUBLIC_KEY_FILE: path.join(dir, 'priv.jwk') }, encoding: 'utf8', timeout: 10000 });
  assert.notEqual(priv.status, 0); assert.match(priv.stderr, /PUBLIC key/);
  // the licence server refuses a private key file other users can read
  if (process.platform !== 'win32') {
    const open = path.join(dir, 'open.pem'); fs.copyFileSync(path.join(dir, 'secrets', 'vendor-private.pem'), open); fs.chmodSync(open, 0o644);
    const r = spawnSync(process.execPath, [path.join(ROOT, 'bridge', 'license-server', 'server.js')], { env: { PATH: process.env.PATH, LICENSE_ADMIN_TOKEN: rand(), LICENSE_PRIVATE_KEY_FILE: open, LICENSE_PORT: '0', LICENSE_DATA_FILE: path.join(dir, 'x', 'l.json') }, encoding: 'utf8', timeout: 10000 });
    assert.notEqual(r.status, 0); assert.match(r.stderr, /chmod 600/); assert.ok(!r.stderr.includes('PRIVATE KEY-----'), 'the key is never printed');
  }
});
