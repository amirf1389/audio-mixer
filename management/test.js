#!/usr/bin/env node
'use strict';
// management/ test: a real licence server and a real OTA server (each on 127.0.0.1, a throw-away key pair and data folder, one shared staff
// database), the vendor MANAGER in front of them, and the customer MY ACCOUNT page. Checks the security rules and the flows end to end.
//   node management/test.js            run the checks (exit 0 = all good)
//   node management/test.js --demo     also keep the three servers running with sample data and print their addresses (for screenshots)
const assert = require('node:assert');
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');

const ROOT = path.join(__dirname, '..');
const { createServer: createLicenseServer } = require(path.join(ROOT, 'bridge', 'license-server', 'server'));
const { createOta } = require(path.join(ROOT, 'ota-server', 'server'));
const { createPortalAuth } = require(path.join(ROOT, 'bridge', 'portal-auth', 'auth'));
const { createManager } = require('./vendor/server');
const { scriptHashes } = require('./inline-page');

const listen = s => new Promise(r => s.listen(0, '127.0.0.1', () => r(s.address().port)));
async function req(base, p, { method = 'GET', body, headers = {} } = {}) {
  const r = await fetch(base + p, { method, headers: Object.assign(body ? { 'Content-Type': 'application/json' } : {}, headers), body: body ? JSON.stringify(body) : undefined, redirect: 'manual' });
  const text = await r.text(); let json = null; try { json = JSON.parse(text); } catch (_) {}
  return { status: r.status, headers: r.headers, text, json };
}
// a raw request with a chosen Host header (fetch does not let a page lie about it, but a DNS-rebinding attacker's browser would send its own name)
const rawHost = (port, p, host) => new Promise((ok, bad) => { const q = http.request({ host: '127.0.0.1', port, path: p, headers: { host } }, r => { r.resume(); r.on('end', () => ok(r.statusCode)); }); q.on('error', bad); q.end(); });

(async () => {
  const demo = process.argv.includes('--demo');
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'amfx1-mgmt-'));
  const pair = crypto.generateKeyPairSync('ec', { namedCurve: 'P-256' });   // a throw-away pair for this run only (never the vendor key)
  const TOKEN = crypto.randomBytes(32).toString('hex'), SECRET = crypto.randomBytes(32).toString('hex'), portalDb = path.join(dir, 'portal.db');
  const staff = createPortalAuth({ dbFile: portalDb, secret: SECRET });
  const PW = 'demo-' + crypto.randomBytes(9).toString('base64url');
  staff.addStaff({ email: 'ops@example.com', name: 'Ops Desk', password: PW });

  const lic = createLicenseServer({ privateKey: pair.privateKey, token: TOKEN, dataFile: path.join(dir, 'licenses.json'), portalDb, portalSecret: SECRET, quiet: true });
  const licPort = await listen(lic), LIC = `http://127.0.0.1:${licPort}`;
  const jwk = crypto.createPublicKey(pair.privateKey).export({ format: 'jwk' });
  const ota = createOta({ dataDir: path.join(dir, 'ota'), jwk, token: TOKEN, portalDb, portalSecret: SECRET, allowHttp: true }); await ota.reindex();
  const otaSrv = http.createServer(ota.handler), otaPort = await listen(otaSrv), OTA = `http://127.0.0.1:${otaPort}`;
  const mgr = createManager({ port: demo ? Number(process.env.MANAGER_PORT || 8790) : 0, up: { lic: LIC, ota: OTA } });
  await new Promise(r => mgr.on('listening', r)); const MGR = `http://127.0.0.1:${mgr.address().port}`;
  const ORIGIN = { Origin: MGR };

  // sample data: keys, a computer on one, a key request from the app, a release file
  const adm = { Authorization: 'Bearer ' + TOKEN };
  const issued = [];
  for (const [name, email, plan, days, seats] of [['Studio Nord', 'desk@studionord.example', 'studio', 365, 3], ['Grace Church Media', 'av@grace.example', 'pro', 20, 2], ['Kemal Arslan', 'kemal@example.com', 'basic', 0, 1], ['Lagos Live Sound', 'tech@lagoslive.example', 'pro', 180, 4]])
    issued.push((await req(LIC, '/admin/licenses', { method: 'POST', headers: adm, body: { name, email, plan, days, seats } })).json);
  assert.ok(issued.every(x => x && x.ok && x.key), "keys issued: " + JSON.stringify(issued.filter(x => !(x && x.ok && x.key))));
  for (const m of ['A1B2-C3D4-E5F6-0718', '9F8E-7D6C-5B4A-3921']) assert.strictEqual((await req(LIC, '/activate', { method: 'POST', body: { key: issued[0].key, machine: m } })).status, 200, 'activate ' + m);
  await req(LIC, '/admin/licenses/' + issued[3].id + '/revoke', { method: 'POST', headers: adm, body: { reason: 'refund' } });
  await req(OTA, '/request-license', { method: 'POST', body: { plan: 'pro', name: 'Maya Lindqvist', email: 'maya@example.com', machineId: 'C0FF-EE12-3456-7890', platform: 'win32', arch: 'x64', version: '1.23.0.1' } });
  const file = Buffer.from('AmF X1 sample release file for the manager test\n'), sha = crypto.createHash('sha256').update(file).digest('hex');
  { const r = await fetch(OTA + '/admin/files/AmF-X1-1.23.0.1-setup.exe', { method: 'PUT', headers: { ...adm, 'X-SHA256': sha }, body: file }); assert.strictEqual(r.status, 201, 'release file uploaded'); }
  await req(LIC, '/admin/crm/customers', { method: 'POST', headers: adm, body: { name: 'Studio Nord', email: 'desk@studionord.example', company: 'Studio Nord AB', status: 'customer' } });
  await req(LIC, '/admin/crm/customers', { method: 'POST', headers: adm, body: { name: 'Maya Lindqvist', email: 'maya@example.com', status: 'lead' } });

  // ---- the MANAGER: one inline page, its script allowed by hash only, Tailwind from the local pinned copy (never the CDN)
  const page = await req(MGR, '/');
  assert.strictEqual(page.status, 200);
  const csp = page.headers.get('content-security-policy');
  assert.ok(/script-src 'sha256-[A-Za-z0-9+/=]+' 'self';/.test(csp), 'script-src: the inline script hash and the local Tailwind only: ' + csp);
  assert.ok(!/cdn\.tailwindcss\.com/.test(page.text), 'no third-party script'); assert.strictEqual((await req(MGR, '/tailwind.js')).status, 200);
  assert.ok(!/script-src[^;]*unsafe-inline/.test(csp) && /frame-ancestors 'none'/.test(csp), 'no unsafe-inline scripts, no framing');
  assert.deepStrictEqual(scriptHashes(page.text).length, 1, 'one inline script');
  // only this computer's own name: a made-up host pointed at 127.0.0.1 is refused (DNS rebinding)
  assert.strictEqual(await rawHost(mgr.address().port, '/', 'evil.example:' + mgr.address().port), 421);
  // only admin routes pass the proxy: the public routes of the servers are not reachable through the manager
  for (const p of ['/lic/activate', '/lic/issue', '/lic/portal/notice', '/ota/update.json', '/ota/request-license', '/lic/../etc/passwd', '/nothing'])
    assert.strictEqual((await req(MGR, p)).status, 404, 'not passed: ' + p);
  // a change must come from the manager page itself
  assert.strictEqual((await req(MGR, '/lic/admin/auth/login', { method: 'POST', body: { email: 'ops@example.com', password: PW } })).status, 403, 'POST without Origin');
  assert.strictEqual((await req(MGR, '/lic/admin/auth/login', { method: 'POST', body: { email: 'ops@example.com', password: PW }, headers: { Origin: 'http://evil.example' } })).status, 403, 'foreign Origin');
  // no sign-in, no data
  assert.strictEqual((await req(MGR, '/lic/admin/licenses')).status, 401);
  assert.strictEqual((await req(MGR, '/lic/admin/auth/login', { method: 'POST', headers: ORIGIN, body: { email: 'ops@example.com', password: 'wrong' } })).status, 401, 'wrong password');
  // ONE SIGN-IN (JWT) for both servers
  const login = await req(MGR, '/lic/admin/auth/login', { method: 'POST', headers: ORIGIN, body: { email: 'ops@example.com', password: PW } });
  assert.strictEqual(login.status, 200); const jwt = login.json.token; assert.ok(/^[\w-]+\.[\w-]+\.[\w-]+$/.test(jwt), 'a JWT');
  const auth = { Authorization: 'Bearer ' + jwt, ...ORIGIN };
  const list = await req(MGR, '/lic/admin/licenses', { headers: auth }); assert.strictEqual(list.status, 200); assert.strictEqual(list.json.licenses.length, 4);
  const files = await req(MGR, '/ota/admin/files', { headers: auth }); assert.strictEqual(files.status, 200, 'the same JWT works on the OTA server'); assert.strictEqual(files.json.files[0].sha256, sha);
  assert.strictEqual((await req(MGR, '/ota/admin/license-requests', { headers: auth })).json.requests[0].name, 'Maya Lindqvist');
  const issue = await req(MGR, '/lic/admin/licenses', { method: 'POST', headers: auth, body: { name: 'Manager Test', plan: 'pro', days: 30, seats: 1 } });
  assert.strictEqual(issue.status, 200); assert.ok(issue.json.key.startsWith('AMIX1.') || issue.json.key.length > 40, 'a key from the manager');
  const audit = await req(MGR, '/lic/admin/audit', { headers: auth }); assert.ok(audit.json.entries.some(e => e.action === 'license.issue'));
  assert.strictEqual((await req(MGR, '/ota/healthz')).status, 200);

  // ---- MY ACCOUNT on the licence server: one inline page; a key shows its computers and frees one
  const acc = await req(LIC, '/account/'); assert.strictEqual(acc.status, 200);
  const acsp = acc.headers.get('content-security-policy'); assert.ok(/script-src 'sha256-[^']+' 'self';/.test(acsp) && !/script-src[^;]*unsafe-inline/.test(acsp), 'account CSP: ' + acsp);
  assert.strictEqual((await req(LIC, '/account/tailwind.js')).status, 200);
  assert.strictEqual((await req(LIC, '/account')).status, 301);
  const st = await req(LIC, '/status', { method: 'POST', body: { key: issued[0].key, list: true } });
  assert.deepStrictEqual(st.json.computers, ['A1B2-C3D4-E5F6-0718', '9F8E-7D6C-5B4A-3921']); assert.strictEqual(st.json.limit, 3);
  assert.ok(!('computers' in (await req(LIC, '/status', { method: 'POST', body: { key: issued[0].key } })).json), 'no list unless asked');
  assert.strictEqual((await req(LIC, '/deactivate', { method: 'POST', body: { key: issued[0].key, machine: '9F8E-7D6C-5B4A-3921' } })).status, 200);
  assert.strictEqual((await req(LIC, '/status', { method: 'POST', body: { key: issued[0].key, list: true } })).json.devices, 1, 'freed');
  await req(LIC, '/activate', { method: 'POST', body: { key: issued[0].key, machine: '9F8E-7D6C-5B4A-3921' } });   // back, for the demo
  const forged = issued[0].key.slice(0, -4) + (issued[0].key.endsWith('AAAA') ? 'BBBB' : 'AAAA');
  assert.notStrictEqual((await req(LIC, '/status', { method: 'POST', body: { key: forged, list: true } })).status, 200, 'a changed key shows nothing');

  console.log('management: manager (inline page, hash CSP, host / origin checks, admin-only proxy, one JWT for both servers) and MY ACCOUNT (computers, free one) OK');
  if (!demo) { mgr.close(); lic.close(); otaSrv.close(); staff.close(); fs.rmSync(dir, { recursive: true, force: true }); return; }
  fs.writeFileSync(path.join(dir, 'demo.json'), JSON.stringify({ manager: MGR, account: LIC + '/account/', email: 'ops@example.com', password: PW, key: issued[0].key }));
  console.log('DEMO ' + path.join(dir, 'demo.json'));
})().catch(e => { console.error('management test FAILED:', e && e.stack || e); process.exit(1); });
