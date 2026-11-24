'use strict';
// END-TO-END test of the customer licence site (/licence/) in a real headless browser. It starts the whole portal (deploy/portal/start.js:
// licence server + OTA server) on localhost with a throwaway P-256 key pair made in a temp folder (never the vendor key), and a small proxy
// that routes exactly as the public host of deploy/portal/Caddyfile does. Then, as a customer would: the console's MANAGE LICENCE ONLINE link
// (machine code prefilled), activate online with a product key, look the licence up by key and by serial + e-mail, free a computer, send a
// licence request, see the signed download, and check that no staff route answers on the public host.
//   NODE_PATH=/opt/node-tools/node_modules node --test --test-force-exit deploy/portal/test/licence-site.e2e.test.js
//   AMIX_SMOKE_LOG=<amixos --smoke output>   take the console's own link from the smoke run (else it is built the way Update.cpp builds it)
//   LICSITE_SHOTS=<folder>                    save a screenshot of every page, desktop (1280) and phone (390) width
// Skipped when Playwright (and its Chromium) is not installed. No network beyond 127.0.0.1. The temp folder is removed at the end.
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const net = require('node:net');
const http = require('node:http');
const crypto = require('node:crypto');
const { spawn } = require('node:child_process');
const { publicRoute } = require('./caddy-routes');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const update = require(path.join(ROOT, 'bridge', 'update'));
let chromium = null; try { ({ chromium } = require('playwright')); } catch (_) { /* skipped below */ }

const MACHINE = 'A1B2-C3D4-E5F6-0718-293A', MACHINE2 = 'FFFF-0000-1111-2222-3333', EMAIL = 'mia@e2e-studio.example';
const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.unref(); s.on('error', rej); s.listen(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => res(p)); }); });
const rand = () => crypto.randomBytes(48).toString('base64url');
const tokenBody = t => { const [, body] = String(t).split('.'); return JSON.parse(Buffer.from(body, 'base64url').toString('utf8')); };
const tokenOk = (t, jwk) => { const [, body, sig] = String(t).split('.'); return crypto.verify('sha256', Buffer.from(body), { key: crypto.createPublicKey({ key: jwk, format: 'jwk' }), dsaEncoding: 'ieee-p1363' }, Buffer.from(sig, 'base64url')); };

let dir, child, proxy, L, O, P, env, jwk, privateKey, browser, logs = '', lic, errors = [];
async function api(base, p, { method = 'GET', token, body, headers = {}, raw } = {}) {
  const h = { ...headers }; if (token) h.authorization = 'Bearer ' + token;
  let data = body; if (body !== undefined && !raw) { data = JSON.stringify(body); h['content-type'] = 'application/json'; }
  const r = await fetch(base + p, { method, headers: h, body: data, redirect: 'manual' }); const text = await r.text(); let json = null; try { json = JSON.parse(text); } catch (_) { /* not JSON */ }
  return { status: r.status, json, text };
}
async function waitUp(url, ms = 15000) { const end = Date.now() + ms; while (Date.now() < end) { try { if ((await fetch(url)).ok) return; } catch (_) { /* not yet */ } await new Promise(r => setTimeout(r, 100)); } throw new Error('did not start: ' + url + '\n' + logs); }
// the public host, as Caddy routes it: staff paths 404, the licence paths to the licence server, everything else to the OTA server
function startProxy(port) {
  const srv = http.createServer((req, res) => {
    const route = publicRoute(new URL(req.url, 'http://x').pathname);
    if (route === 'staff') { res.writeHead(404, { 'content-type': 'text/plain' }); return res.end('not found'); }
    const up = new URL(route === 'licence' ? L : O), fwd = http.request({ host: up.hostname, port: up.port, path: req.url, method: req.method, headers: { ...req.headers, host: up.host } }, r => { res.writeHead(r.statusCode, r.headers); r.pipe(res); });
    fwd.on('error', () => { res.writeHead(502); res.end(); }); req.pipe(fwd);
  });
  return new Promise(r => srv.listen(port, '127.0.0.1', () => r(srv)));
}
// the link the console's MANAGE LICENCE ONLINE opens: from the smoke run when given, on this test's public host
function consoleLink() {
  const f = process.env.AMIX_SMOKE_LOG; let link = null;
  if (f && fs.existsSync(f)) { const m = /MANAGE LICENCE ONLINE (\S+)/.exec(fs.readFileSync(f, 'utf8')); if (m) link = m[1]; }
  if (!link) return { from: 'built like Update::manageLicenceUrl', url: P + '/licence/?machine=' + MACHINE + '&edition=basic', machine: MACHINE };
  const u = new URL(link), machine = u.searchParams.get('machine');
  return { from: 'amixos --smoke', url: P + u.pathname + u.search, machine, original: link };
}
async function newPage(width = 1280) {
  const ctx = await browser.newContext({ viewport: { width, height: width < 600 ? 860 : 900 }, deviceScaleFactor: 1, acceptDownloads: true }), page = await ctx.newPage();
  page.on('pageerror', e => errors.push('pageerror ' + e.message));
  page.on('console', m => { if (m.type() === 'error' && !/the server responded with a status of 4\d\d/.test(m.text())) errors.push('console ' + m.text()); });
  return page;
}
const shot = async (page, name) => { const d = process.env.LICSITE_SHOTS; if (d) { fs.mkdirSync(d, { recursive: true }); await page.screenshot({ path: path.join(d, name + '.png'), fullPage: true }); } };

test.before(async () => {
  if (!chromium) return;
  dir = fs.mkdtempSync(path.join(os.tmpdir(), 'amfx1-licsite-e2e-'));
  const pair = crypto.generateKeyPairSync('ec', { namedCurve: 'P-256' }); privateKey = pair.privateKey; jwk = pair.publicKey.export({ format: 'jwk' });
  fs.mkdirSync(path.join(dir, 'secrets'), { mode: 0o700 });
  fs.writeFileSync(path.join(dir, 'secrets', 'vendor-private.pem'), pair.privateKey.export({ type: 'pkcs8', format: 'pem' }), { mode: 0o600 });
  fs.writeFileSync(path.join(dir, 'secrets', 'licence-data.key'), rand(), { mode: 0o600 });
  fs.writeFileSync(path.join(dir, 'test-public.json'), JSON.stringify(jwk)); fs.mkdirSync(path.join(dir, 'site-releases'));
  const [lp, op, pp] = [await freePort(), await freePort(), await freePort()];
  L = 'http://127.0.0.1:' + lp; O = 'http://127.0.0.1:' + op; P = 'http://127.0.0.1:' + pp;
  env = { LICENSE_ADMIN_TOKEN: rand(), OTA_ADMIN_TOKEN: rand(), PORTAL_JWT_SECRET: rand(), PORTAL_CRM_SECRET: rand() };
  fs.writeFileSync(path.join(dir, '.env'), [...Object.entries(env).map(([k, v]) => `${k}=${v}`),
    'LICENSE_PRIVATE_KEY_FILE=secrets/vendor-private.pem', 'LICENSE_DATA_KEY_FILE=secrets/licence-data.key', 'DATA_DIR=data', 'BIND_HOST=127.0.0.1',
    `LICENSE_PORT=${lp}`, `OTA_PORT=${op}`, 'TRUST_PROXY=0', 'LICENSE_RELEASES_DIR=site-releases', `OTA_PUBLIC_URL=${P}`, `LICENSE_UPDATE_URL=${O}/update.json`,
    'OTA_ALLOW_HTTP=1', 'OTA_PUBLIC_KEY_FILE=test-public.json', ''].join('\n'), { mode: 0o600 });
  child = spawn(process.execPath, [path.join(ROOT, 'deploy', 'portal', 'start.js'), '--env', path.join(dir, '.env')], { stdio: ['ignore', 'pipe', 'pipe'] });
  child.stdout.on('data', d => { logs += d; }); child.stderr.on('data', d => { logs += d; });
  await waitUp(L + '/health'); await waitUp(O + '/healthz'); proxy = await startProxy(pp);
  // the vendor issues a Pro licence for two computers (staff side, straight at the licence server: never through the public host)
  lic = (await api(L, '/admin/licenses', { method: 'POST', token: env.LICENSE_ADMIN_TOKEN, body: { plan: 'pro', name: 'Mia E2E', email: EMAIL, seats: 2, days: 365 } })).json;
  assert.ok(lic && lic.ok && lic.productKey, 'a test licence was issued');
  // and publishes a signed release on the OTA server
  const file = crypto.randomBytes(4096), name = 'AmF-X1-9.9.9.9-setup.exe', sha = crypto.createHash('sha256').update(file).digest('hex');
  assert.equal((await api(O, '/admin/files/' + name, { method: 'PUT', token: env.OTA_ADMIN_TOKEN, body: file, raw: true, headers: { 'x-sha256': sha } })).status, 201);
  const manifest = { product: 'audio-mixer', version: '9.9.9.9', channel: 'stable', released: '2026-11-24', notes: ['Licence site test release'], files: { 'win-x64-exe': { name, label: 'Windows 64-bit installer', url: P + '/releases/' + name, sha256: sha, size: file.length } } };
  assert.equal((await api(O, '/admin/manifest/stable', { method: 'PUT', token: env.OTA_ADMIN_TOKEN, body: update.signManifest(manifest, privateKey) })).status, 200);
  browser = await chromium.launch();
});
test.after(async () => {
  if (browser) await browser.close();
  if (proxy) proxy.close();
  if (child && child.exitCode === null) child.kill('SIGTERM');
  if (dir) fs.rmSync(dir, { recursive: true, force: true });
});
const t = (name, fn) => test(name, { skip: chromium ? false : 'Playwright is not installed (NODE_PATH=/opt/node-tools/node_modules)' }, fn);

t("the console's MANAGE LICENCE ONLINE link opens the site with the machine code filled in", async () => {
  const link = consoleLink(), page = await newPage();
  if (link.original) assert.match(link.original, /^https:\/\/[^/]+\/licence\/\?machine=[A-Z0-9-]{8,64}&edition=[a-z]+$/, 'the console link: ' + link.original);
  const res = await page.goto(link.url); assert.equal(res.status(), 200);
  assert.match(await page.textContent('#currentEdition'), new RegExp('machine ' + link.machine));
  await page.click('nav >> text=Activate'); await page.waitForURL(/\/licence\/activate\?/); await page.waitForFunction(() => document.getElementById('actMachine').value !== '');
  assert.equal(await page.inputValue('#actMachine'), link.machine, 'the machine code follows to the Activate page (' + link.from + ')');
  await page.click('nav >> text=Request / Upgrade'); await page.waitForURL(/\/licence\/request\?/); await page.waitForFunction(() => document.getElementById('reqMachine').value !== '');
  assert.equal(await page.inputValue('#reqMachine'), link.machine);
  await page.context().close();
});

t('activate online with the product key: the result, the activation file for AmF X1, the seat taken', async () => {
  const page = await newPage();
  await page.goto(P + '/licence/activate?machine=' + MACHINE + '&edition=basic');
  await page.fill('#actKey', 'not a key'); await page.click('#actBtn');
  assert.match(await page.textContent('#actMsg'), /Paste a product key/); assert.equal(await page.getAttribute('#actKey', 'aria-invalid'), 'true');
  await page.fill('#actKey', lic.productKey.toLowerCase()); await page.click('#actBtn');
  await page.waitForSelector('#actResult:not([hidden])');
  const facts = await page.textContent('#actFacts');
  assert.match(facts, /Pro/); assert.match(facts, new RegExp(MACHINE)); assert.match(facts, /1 of 2 activated/); assert.match(facts, new RegExp(lic.id));
  const [dl] = await Promise.all([page.waitForEvent('download'), page.click('#openApp')]);
  assert.equal(dl.suggestedFilename(), 'amfx1-activation.amixkey');
  const token = fs.readFileSync(await dl.path(), 'utf8').trim();
  assert.ok(token.startsWith('AMXA1.') && tokenOk(token, jwk), 'the file holds an activation signed by the licence server');
  const body = tokenBody(token); assert.equal(body.mid, MACHINE); assert.equal(body.plan, 'pro'); assert.equal(body.lid, lic.id);
  await shot(page, 'activate-desktop');
  // the same key again on this computer is fine; the server knows the seat
  assert.equal((await api(P, '/status', { method: 'POST', body: { key: lic.key, machine: MACHINE } })).json.thisComputer, true);
  await page.context().close();
});

t('my licence by key: edition, end, computers; free a second computer', async () => {
  assert.equal((await api(P, '/activate-online', { method: 'POST', body: { key: lic.key, machine: MACHINE2 } })).status, 200, 'a second computer');
  const page = await newPage();
  await page.goto(P + '/licence/my?machine=' + MACHINE);
  assert.equal(await page.inputValue('#keyMachine'), MACHINE);
  await page.fill('#keyIn', lic.key); await page.click('#byKey button[type=submit]');
  await page.waitForSelector('#licResult:not([hidden]) ul.devices li');
  const text = await page.textContent('#licResult');
  assert.match(text, /Pro licence/); assert.match(text, /2 of 2 activated/); assert.match(text, /active/); assert.match(text, new RegExp(MACHINE + '\\s*THIS COMPUTER'));
  assert.match(text, new RegExp(MACHINE2)); assert.match(text, /Ends\s*\d{4}-\d\d-\d\d/);
  await shot(page, 'my-desktop');
  page.once('dialog', d => d.accept());
  await page.locator('#licResult li', { hasText: MACHINE2 }).locator('button').click();
  await page.waitForFunction(() => /1 of 2 activated/.test(document.getElementById('licResult').textContent));
  assert.ok(!(await page.textContent('#licResult')).includes(MACHINE2), 'the freed computer is gone');
  assert.deepEqual((await api(P, '/status', { method: 'POST', body: { key: lic.key, list: true } })).json.computers, [MACHINE]);
  await page.context().close();
});

t('my licence by serial + e-mail: a wrong e-mail tells nothing, the right one shows it; free the computer', async () => {
  const page = await newPage();
  await page.goto(P + '/licence/my');
  await page.fill('#serialIn', lic.id); await page.fill('#emailIn', 'someone-else@e2e-studio.example'); await page.click('#bySerial button[type=submit]');
  await page.waitForFunction(() => /No licence matches/.test(document.getElementById('serialMsg').textContent));
  assert.equal(await page.isHidden('#licResult'), true);
  await page.fill('#emailIn', EMAIL.toUpperCase()); await page.click('#bySerial button[type=submit]');
  await page.waitForSelector('#licResult:not([hidden]) ul.devices li');
  const text = await page.textContent('#licResult');
  assert.match(text, /1 of 2 activated/); assert.match(text, new RegExp('Computer ending ' + MACHINE.replace(/-/g, '').slice(-4)));
  assert.ok(!text.includes(MACHINE), 'the full machine code is not shown on the serial lookup');
  page.once('dialog', d => d.accept()); await page.locator('#licResult li button').first().click();
  await page.waitForFunction(() => /0 of 2 activated/.test(document.getElementById('licResult').textContent));
  assert.equal((await api(P, '/status', { method: 'POST', body: { key: lic.key, list: true } })).json.devices, 0);
  await page.context().close();
});

t('request / upgrade: the request reaches the vendor; the WhatsApp link carries the edition and machine code', async () => {
  const page = await newPage();
  await page.goto(P + '/licence/request?machine=' + MACHINE + '&edition=pro');
  assert.equal(await page.inputValue('#reqPlan'), 'master', 'Pro asks for the next edition up');
  const wa = await page.getAttribute('#waLink', 'href');
  assert.ok(wa.startsWith('https://wa.me/601120871185?text=')); const msg = decodeURIComponent(wa.split('text=')[1]);
  assert.match(msg, /AmF X1 Studio Master/); assert.match(msg, new RegExp('Machine ID: ' + MACHINE));
  await page.click('#reqForm button[type=submit]'); assert.match(await page.textContent('#reqMsg'), /Type your name/);
  await page.fill('#reqName', 'Mia E2E'); await page.fill('#reqEmail', EMAIL); await page.click('#reqForm button[type=submit]');
  await page.waitForFunction(() => /Request received/.test(document.getElementById('reqMsg').textContent));
  await shot(page, 'request-desktop');
  const got = (await api(O, '/admin/license-requests?limit=5', { token: env.OTA_ADMIN_TOKEN })).json.requests;
  assert.ok(got.some(r => r.machineId === MACHINE && r.plan === 'master' && r.email === EMAIL && r.platform === 'web'), 'the vendor sees the request');
  await page.context().close();
});

t('downloads: the signed release, its signature checked in the browser', async () => {
  const page = await newPage();
  await page.goto(P + '/licence/downloads');
  await page.waitForSelector('#dlRelease:not([hidden])');
  assert.match(await page.textContent('#dlTitle'), /AmF X1 9\.9\.9\.9/); assert.match(await page.textContent('#dlBadge'), /Signature checked/);
  const href = await page.getAttribute('#dlFiles a', 'href'); assert.equal(href, P + '/releases/AmF-X1-9.9.9.9-setup.exe');
  assert.equal((await fetch(href)).status, 200);
  await page.context().close();
});

t('the public host has no staff route; every page loads with no CSP or script error; screenshots', async () => {
  for (const p of ['/admin', '/admin/', '/admin/login', '/admin/licenses', '/admin/crm', '/admin/auth/login', '/issue', '/admin/license-requests']) {
    const r = await api(P, p, p === '/issue' || p === '/admin/auth/login' ? { method: 'POST', body: {} } : {});
    assert.equal(r.status, 404, p); assert.ok(!/<html|"ok"/.test(r.text), p + ' answered with a page or API');
  }
  assert.equal((await api(P, '/admin/licenses', { token: env.LICENSE_ADMIN_TOKEN })).status, 404, 'not even with the admin token');
  for (const width of [1280, 390]) {
    const page = await newPage(width);
    for (const [p, name] of [['/licence/?machine=' + MACHINE + '&edition=pro', 'overview'], ['/licence/my?machine=' + MACHINE, 'my'], ['/licence/activate?machine=' + MACHINE, 'activate'], ['/licence/request?machine=' + MACHINE + '&edition=pro', 'request'], ['/licence/downloads', 'downloads'], ['/licence/help', 'help']]) {
      const res = await page.goto(P + p); assert.equal(res.status(), 200, p);
      if (name === 'downloads') await page.waitForSelector('#dlRelease:not([hidden])');
      assert.ok(await page.evaluate(() => document.fonts.ready.then(() => document.fonts.check('16px Inter') && document.fonts.check('700 16px "Space Grotesk"') && [...document.fonts].some(f => f.family.includes('Inter') && f.status === 'loaded'))), p + ': the self-hosted fonts load');
      const overflow = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
      assert.ok(overflow <= 0, p + ' scrolls sideways at ' + width + ' px (' + overflow + ')');
      await shot(page, name + (width < 600 ? '-phone' : '-desktop-page'));
    }
    await page.context().close();
  }
  assert.deepEqual(errors, [], 'no CSP violation or script error in any page');
});
