'use strict';
// Tests for the customer licence site (/licence/): its two endpoints (validation, rate limit, no data of another customer, no enumeration),
// the static pages (every link resolves, strict CSP, no inline script, no secret) and that no staff route is reachable through the public site.
// A throwaway key pair, never the vendor key.   node --test license-server/licence-site.test.js
const test = require('node:test');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { createServer } = require('./server');
const { publicRoute } = require('../../deploy/portal/test/caddy-routes');

const pair = crypto.generateKeyPairSync('ec', { namedCurve: 'P-256' });
const TOKEN = crypto.randomBytes(24).toString('hex');
const SITE = path.join(__dirname, 'licence-site');
const PAGES = ['/licence/', '/licence/my', '/licence/activate', '/licence/request', '/licence/downloads', '/licence/help'];
let dir, srv, base, A, B;

async function call(p, { method = 'GET', body, token, ip } = {}) {
  const h = {}; if (body !== undefined) h['content-type'] = 'application/json'; if (token) h.authorization = 'Bearer ' + token; if (ip) h['x-forwarded-for'] = ip;
  const r = await fetch(base + p, { method, headers: h, body: body === undefined ? undefined : JSON.stringify(body), redirect: 'manual' });
  const text = await r.text(); let json = null; try { json = JSON.parse(text); } catch (_) { /* a page */ }
  return { status: r.status, headers: r.headers, text, json };
}
let n = 0; const ip = () => '10.9.' + Math.floor(++n / 250) + '.' + (n % 250);   // a fresh address per check, so the limiter is tested on purpose only

test.before(async () => {
  dir = fs.mkdtempSync(path.join(os.tmpdir(), 'amfx1-licsite-'));
  srv = createServer({ privateKey: pair.privateKey, token: TOKEN, dataFile: path.join(dir, 'licenses.json'), quiet: true, trustProxy: true, releasesDir: dir });
  await new Promise(r => srv.listen(0, '127.0.0.1', r)); base = 'http://127.0.0.1:' + srv.address().port;
  const issue = async (name, email, seats) => (await call('/admin/licenses', { method: 'POST', token: TOKEN, body: { plan: 'pro', name, email, seats, days: 365 } })).json;
  A = await issue('Alice Audio', 'alice@studio-a.example', 2); B = await issue('Bob Sound', 'bob@studio-b.example', 3);
  for (const [k, m] of [[A.key, 'AAAA-1111-AAAA-1111-AAAA'], [A.key, 'AAAA-2222-AAAA-2222-AAAA'], [B.key, 'BBBB-1111-BBBB-1111-BBBB']]) assert.equal((await call('/activate', { method: 'POST', body: { key: k, machine: m }, ip: ip() })).status, 200);
});
test.after(() => { srv.close(); fs.rmSync(dir, { recursive: true, force: true }); });

test('lookup: serial + e-mail gives that licence, its computers as references only', async () => {
  const r = await call('/licence/api/lookup', { method: 'POST', body: { serial: A.id.toLowerCase(), email: '  ALICE@studio-a.example ' }, ip: ip() });
  assert.equal(r.status, 200); assert.equal(r.json.id, A.id); assert.equal(r.json.plan, 'pro'); assert.equal(r.json.devices, 2); assert.equal(r.json.limit, 2);
  assert.ok(r.json.expires > Date.now()); assert.equal(r.json.revoked, false);
  assert.deepEqual(r.json.computers.map(c => c.ends), ['AAAA', 'AAAA']);
  for (const c of r.json.computers) assert.match(c.ref, /^[A-Za-z0-9_-]{22}$/);
  assert.ok(!r.text.includes('1111') && !r.text.includes('2222'), 'no machine code in the answer');
  assert.ok(!/alice|Alice/.test(r.text), 'no name or e-mail echoed back');
});

test('lookup: input is validated', async () => {
  for (const body of [{}, { serial: 'XYZ', email: 'a@b.example' }, { serial: A.id, email: 'not-an-email' }, { serial: A.id + '0', email: 'alice@studio-a.example' }, { serial: A.id, email: 'a'.repeat(110) + '@b.example' }, { serial: ['x'], email: {} }]) {
    const r = await call('/licence/api/lookup', { method: 'POST', body, ip: ip() }); assert.equal(r.status, 400, JSON.stringify(body)); assert.equal(r.json.ok, false);
  }
  const big = await fetch(base + '/licence/api/lookup', { method: 'POST', headers: { 'content-type': 'application/json', 'x-forwarded-for': ip() }, body: JSON.stringify({ serial: A.id, email: 'x'.repeat(9000) }) });
  assert.equal(big.status, 413);
  assert.equal((await call('/licence/api/lookup', { ip: ip() })).status, 405);
});

test('no enumeration and no cross-customer data: a wrong e-mail and an unknown serial get the same answer', async () => {
  const wrongMail = await call('/licence/api/lookup', { method: 'POST', body: { serial: A.id, email: 'bob@studio-b.example' }, ip: ip() });
  const unknown = await call('/licence/api/lookup', { method: 'POST', body: { serial: 'ABCDEF012345', email: 'bob@studio-b.example' }, ip: ip() });
  assert.equal(wrongMail.status, 404); assert.equal(unknown.status, 404); assert.equal(wrongMail.text, unknown.text);
  const b = await call('/licence/api/lookup', { method: 'POST', body: { serial: B.id, email: 'bob@studio-b.example' }, ip: ip() });
  assert.equal(b.status, 200); assert.equal(b.json.devices, 1); assert.ok(!b.text.includes(A.id), 'no other licence in the answer');
  // a computer reference of licence A does not free a seat of licence B
  const refA = (await call('/licence/api/lookup', { method: 'POST', body: { serial: A.id, email: 'alice@studio-a.example' }, ip: ip() })).json.computers[0].ref;
  const cross = await call('/licence/api/deactivate', { method: 'POST', body: { serial: B.id, email: 'bob@studio-b.example', ref: refA }, ip: ip() });
  assert.equal(cross.status, 404);
  const cross2 = await call('/licence/api/deactivate', { method: 'POST', body: { serial: A.id, email: 'bob@studio-b.example', ref: refA }, ip: ip() });
  assert.equal(cross2.status, 404); assert.equal(cross2.json.error, wrongMail.json.error);
  assert.equal((await call('/licence/api/lookup', { method: 'POST', body: { serial: A.id, email: 'alice@studio-a.example' }, ip: ip() })).json.devices, 2, 'nothing was freed');
});

test('deactivate: serial + e-mail + reference frees that one computer', async () => {
  const before = (await call('/licence/api/lookup', { method: 'POST', body: { serial: A.id, email: 'alice@studio-a.example' }, ip: ip() })).json;
  assert.equal((await call('/licence/api/deactivate', { method: 'POST', body: { serial: A.id, email: 'alice@studio-a.example', ref: 'short' }, ip: ip() })).status, 400);
  const r = await call('/licence/api/deactivate', { method: 'POST', body: { serial: A.id, email: 'alice@studio-a.example', ref: before.computers[1].ref }, ip: ip() });
  assert.equal(r.status, 200); assert.equal(r.json.devices, 1); assert.equal(r.json.computers[0].ref, before.computers[0].ref);
  // the key holder sees the same through the existing public routes
  const s = await call('/status', { method: 'POST', body: { key: A.key, list: true }, ip: ip() }); assert.deepEqual(s.json.computers, ['AAAA-1111-AAAA-1111-AAAA']);
  assert.equal((await call('/activate', { method: 'POST', body: { key: A.key, machine: 'AAAA-2222-AAAA-2222-AAAA' }, ip: ip() })).status, 200, 'the seat is free again');
});

test('rate limit: ten tries a minute from one address, and one serial is held after twenty', async () => {
  const one = ip(); let last;
  for (let i = 0; i < 11; i++) last = await call('/licence/api/lookup', { method: 'POST', body: { serial: B.id, email: 'guess' + i + '@x.example' }, ip: one });
  assert.equal(last.status, 429); assert.ok(Number(last.headers.get('retry-after')) > 0);
  assert.equal((await call('/licence/api/deactivate', { method: 'POST', body: { serial: B.id, email: 'bob@studio-b.example', ref: 'x'.repeat(22) }, ip: one })).status, 429, 'deactivate shares the address limit');
  const statuses = [];
  for (let i = 0; i < 22; i++) statuses.push((await call('/licence/api/lookup', { method: 'POST', body: { serial: 'C0FFEE000001', email: 'g' + i + '@x.example' }, ip: ip() })).status);
  assert.ok(statuses.slice(0, 20).every(s => s === 404) && statuses.slice(20).every(s => s === 429), statuses.join(','));
});

test('every page renders under a strict CSP, with no inline script or style and no secret', async () => {
  const secretish = [TOKEN, /BEGIN [A-Z ]*PRIVATE KEY/, /Bearer\s+[A-Za-z0-9]/, /LICENSE_ADMIN_TOKEN|PORTAL_JWT_SECRET|PORTAL_CRM_SECRET|OTA_ADMIN_TOKEN/, /"d"\s*:/, /sk_live|api[_-]?key/i];
  for (const p of PAGES) {
    const r = await call(p); assert.equal(r.status, 200, p);
    assert.match(r.headers.get('content-type'), /^text\/html/);
    const csp = r.headers.get('content-security-policy');
    assert.match(csp, /default-src 'none'/); assert.match(csp, /script-src 'self'/); assert.doesNotMatch(csp, /unsafe-inline|unsafe-eval|https?:/, p);
    assert.equal(r.headers.get('x-frame-options'), 'DENY');
    assert.doesNotMatch(r.text, /<script(?![^>]*\bsrc=)[^>]*>/i, p + ': inline script');
    assert.doesNotMatch(r.text, /\son[a-z]+\s*=/i, p + ': inline event handler');
    assert.doesNotMatch(r.text, /\sstyle\s*=|<style/i, p + ': inline style');
    assert.doesNotMatch(r.text, /\/admin|\/issue\b/, p + ': links to a staff route');
    for (const s of secretish) assert.ok(typeof s === 'string' ? !r.text.includes(s) : !s.test(r.text), p + ': ' + s);
    assert.match(r.text, /<html lang="en">/); assert.match(r.text, /<main id="main"/); assert.match(r.text, /aria-current="page"/);
    for (const m of r.text.matchAll(/<(?:script|link|img)\b[^>]*\b(?:src|href)="([^"]+)"/g)) assert.ok(m[1].startsWith('/licence/') || m[1].startsWith('data:image/svg+xml,'), p + ': ' + m[1] + ' is not from this site (no CDN)');
    assert.match(csp, /font-src 'self'/); assert.match(csp, /img-src 'self' data:/); assert.doesNotMatch(r.text, /fonts\.googleapis|fonts\.gstatic/);
  }
  for (const f of ['site.js', 'site.css']) { const t = fs.readFileSync(path.join(SITE, f), 'utf8'); for (const s of secretish) assert.ok(typeof s === 'string' ? !t.includes(s) : !s.test(t), f + ': ' + s);
    assert.doesNotMatch(t, /\/admin|localStorage|sessionStorage|document\.cookie|innerHTML|eval\(|new Function/, f); }
});

test('every link of the site resolves: its own pages and files here, the rest on the public host', async () => {
  const seen = new Set();
  for (const p of PAGES) {
    const html = (await call(p)).text;
    for (const m of html.matchAll(/\b(?:href|src)="([^"#]+)"/g)) {
      const u = m[1]; if (seen.has(u)) continue; seen.add(u);
      if (u.startsWith('data:image/svg+xml,')) { assert.doesNotMatch(decodeURIComponent(u), /<script|on[a-z]+=|href=/i, 'the favicon is a plain picture'); continue; }
      if (/^https:\/\/wa\.me\/\d{8,15}$/.test(u)) continue;   // the WhatsApp chat, the pattern the website and the app already use
      assert.ok(u.startsWith('/'), u + ' is a relative or third-party link');
      const route = publicRoute(u.split('?')[0]);
      assert.notEqual(route, 'staff', u + ' would be a staff route');
      if (route === 'licence') { const r = await call(u); assert.equal(r.status, 200, u); }
      else assert.ok(['/latest'].includes(u), u + ' goes to the OTA server: it must be one of its public routes');
    }
  }
  for (const f of ['/licence/site.css', '/licence/site.js', '/licence/fonts/inter-latin-400-normal.woff2', '/licence/fonts/OFL-Inter.txt']) assert.ok(seen.has(f), f);
  // the self-hosted fonts the stylesheet names, and their licences
  const css = (await call('/licence/site.css')).text;
  for (const m of css.matchAll(/url\((\/licence\/fonts\/[^)]+\.woff2)\)/g)) { const r = await fetch(base + m[1]); assert.equal(r.status, 200, m[1]); assert.equal(r.headers.get('content-type'), 'font/woff2'); assert.equal(Buffer.from(await r.arrayBuffer()).subarray(0, 4).toString('latin1'), 'wOF2', m[1]); }
  assert.equal([...css.matchAll(/@font-face/g)].length, 5);
  for (const f of ['OFL-Inter.txt', 'OFL-SpaceGrotesk.txt']) assert.match((await call('/licence/fonts/' + f)).text, /SIL Open Font License, Version 1\.1/);
  for (const api of ['/status', '/deactivate', '/redeem', '/activate-online', '/licence/api/lookup', '/licence/api/deactivate', '/request-license', '/update.json', '/licence/vendor-key.json']) assert.notEqual(publicRoute(api), 'staff', api);
  assert.equal((await call('/licence')).status, 301);
});

test('the vendor key the Downloads page uses is the PUBLIC key only', async () => {
  const r = await call('/licence/vendor-key.json'); const pub = pair.publicKey.export({ format: 'jwk' });
  assert.deepEqual(Object.keys(r.json).sort(), ['crv', 'kty', 'x', 'y']); assert.equal(r.json.x, pub.x); assert.equal(r.json.y, pub.y);
});

test('staff routes are not reachable from the public site, and stay locked on the licence server itself', async () => {
  for (const p of ['/admin', '/admin/', '/admin/login', '/admin/licenses', '/admin/crm/customers', '/admin/system/backup', '/issue', '/admin/auth/login']) assert.equal(publicRoute(p), 'staff', p);
  for (const p of ['/licence/../admin/licenses', '/licence/%2e%2e/admin/']) assert.equal(publicRoute(p), 'staff', p + ' (Caddy cleans the path first)');
  // straight at the licence server (no Caddy): the admin API still needs the token, and the site's routes never accept it as a way in
  assert.equal((await call('/admin/licenses', { ip: ip() })).status, 401);
  assert.equal((await call('/licence/api/lookup', { method: 'POST', token: TOKEN, body: { serial: A.id, email: 'nobody@x.example' }, ip: ip() })).status, 404, 'the admin token gives the site no extra power');
  assert.equal((await call('/licence/admin', { ip: ip() })).status, 404);
  assert.equal((await call('/licence/api/licenses', { ip: ip() })).status, 404);
});
