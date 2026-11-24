'use strict';
// AmF X1 Key Manager tests: the PACKAGED app (the exact files build.js embeds, run from a staging folder like the .exe runs them after
// extraction) starts, listens on 127.0.0.1 only, serves its page, refuses a foreign Host, and stops when its window says goodbye; the SEA
// extraction checks hashes; the key list decrypts only after unlock. Every key pair here is a throwaway test pair in a temp folder.
//   node --test tools/key-manager/test/
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const net = require('node:net');
const path = require('node:path');
const http = require('node:http');
const crypto = require('node:crypto');
const { spawn } = require('node:child_process');

const KM = path.resolve(__dirname, '..');
const build = require(path.join(KM, 'build.js'));
const seaEntry = require(path.join(KM, 'sea-entry.js'));
const PIN = 'test-pin-' + crypto.randomBytes(4).toString('hex');
const tmp = () => fs.mkdtempSync(path.join(os.tmpdir(), 'amfx1-km-test-'));

function req(port, { method = 'GET', p = '/', headers = {}, body } = {}) {
  return new Promise((resolve, reject) => {
    const data = body === undefined ? null : Buffer.from(typeof body === 'string' ? body : JSON.stringify(body));
    const r = http.request({ host: '127.0.0.1', port, method, path: p, headers: { ...(data ? { 'Content-Length': data.length } : {}), ...headers } }, res => {
      const chunks = []; res.on('data', c => chunks.push(c)); res.on('end', () => { const text = Buffer.concat(chunks).toString('utf8'); let json = null; try { json = JSON.parse(text); } catch (_) { /* html / csv */ } resolve({ status: res.statusCode, headers: res.headers, text, json }); });
    });
    r.on('error', reject); if (data) r.write(data); r.end();
  });
}
const ACT = { 'Content-Type': 'application/json', 'X-Gen-Action': '1' };
const post = (port, p, body) => req(port, { method: 'POST', p, headers: ACT, body });

test('the packaged entry starts on 127.0.0.1 only, serves the page, refuses a foreign Host, and shuts down when the window closes', async t => {
  const stage = tmp(), home = tmp();
  t.after(() => { fs.rmSync(stage, { recursive: true, force: true }); fs.rmSync(home, { recursive: true, force: true }); });
  const man = build.stage(stage);
  assert.ok(man.files['scripts/license-generator.js'] && man.files['tools/key-manager/main.js'] && man.files['bridge/license.js'], 'generator + key manager are embedded');
  assert.ok(!Object.keys(man.files).some(f => /private\.pem|\.enc$|edition-token/.test(f)), 'no key material is embedded');
  const env = { ...process.env, HOME: home, USERPROFILE: home, XDG_CACHE_HOME: path.join(home, '.cache'), AUDIO_MIXER_VENDOR_DIR: path.join(home, 'vendor') };
  const child = spawn(process.execPath, [path.join(stage, 'tools', 'key-manager', 'main.js'), '--no-window', '--port', '0', '--grace-ms', '300'], { env, stdio: ['ignore', 'pipe', 'pipe'] });
  t.after(() => { if (child.exitCode === null) child.kill(); });
  let out = '';
  const url = await new Promise((resolve, reject) => {
    const to = setTimeout(() => reject(new Error('did not start: ' + out)), 15000);
    child.stdout.on('data', d => { out += d; const m = /http:\/\/127\.0\.0\.1:(\d+)\//.exec(out); if (m) { clearTimeout(to); resolve(m[0]); } });
    child.on('exit', c => reject(new Error('exited ' + c + ': ' + out)));
  });
  const port = Number(/:(\d+)\//.exec(url)[1]);

  // bound to loopback only: the same port on any other address of this computer does not answer
  const other = Object.values(os.networkInterfaces()).flat().find(a => a && a.family === 'IPv4' && !a.internal);
  if (other) {
    const refused = await new Promise(resolve => { const s = net.connect({ host: other.address, port }); s.setTimeout(2000); s.on('connect', () => { s.destroy(); resolve(false); }); s.on('error', () => resolve(true)); s.on('timeout', () => { s.destroy(); resolve(true); }); });
    assert.ok(refused, 'not reachable on ' + other.address);
  }
  const page = await req(port, { headers: { Host: '127.0.0.1:' + port } });
  assert.equal(page.status, 200);
  assert.match(page.text, /<title>AmF X1 Key Manager<\/title>/);
  assert.match(page.text, /id="km-keys" href="\/km\/keys"/, 'the KEY LIST link is in the generator page');
  assert.match(page.text, /GENERATE A LICENSE KEY/, 'it is the generator page');
  const csp = page.headers['content-security-policy'];
  assert.match(csp, /default-src 'none'/); assert.doesNotMatch(csp, /unsafe-inline/);
  const scripts = [...page.text.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m => "'sha256-" + crypto.createHash('sha256').update(m[1]).digest('base64') + "'");
  assert.equal(scripts.length, 2); for (const h of scripts) assert.ok(csp.includes(h), 'every inline script is allowed by its hash');
  const list = await req(port, { p: '/km/keys' });
  assert.equal(list.status, 200); assert.match(list.text, /KEY LIST/);

  for (const p of ['/', '/km/keys', '/status', '/km/api/keys']) {
    const r = await req(port, { p, headers: { Host: 'evil.example:' + port, 'X-Gen-Action': '1' } });
    assert.equal(r.status, 403, 'foreign Host refused on ' + p);
  }
  assert.equal((await req(port, { p: '/km/api/keys' })).status, 403, 'the data needs the page header');
  assert.equal((await req(port, { p: '/km/api/keys', headers: { 'X-Gen-Action': '1', Origin: 'https://evil.example' } })).status, 403, 'foreign Origin refused');
  assert.equal((await req(port, { method: 'POST', p: '/km/bye', headers: { 'Content-Type': 'text/plain' }, body: '{}' })).status, 403, 'a foreign form cannot close the app');

  assert.equal((await post(port, '/km/alive', { tab: 't1' })).status, 200);
  const exited = new Promise(resolve => child.on('exit', c => resolve(c)));
  assert.equal((await post(port, '/km/bye', { tab: 't1' })).status, 200);
  const code = await Promise.race([exited, new Promise(r => setTimeout(() => r('timeout'), 8000))]);
  assert.equal(code, 0, 'stopped by itself once the window closed');
  await assert.rejects(req(port, {}), 'nothing listens any more');
});

test('the .exe extraction writes the embedded files, re-checks hashes every start and refuses a bad asset', t => {
  const stage = tmp(), base = tmp();
  t.after(() => { fs.rmSync(stage, { recursive: true, force: true }); fs.rmSync(base, { recursive: true, force: true }); });
  const man = build.stage(stage);
  const get = name => fs.readFileSync(path.join(stage, name));
  const { dir } = seaEntry.extract(get, base);
  assert.equal(fs.readFileSync(path.join(dir, 'tools', 'key-manager', 'main.js'), 'utf8'), fs.readFileSync(path.join(KM, 'main.js'), 'utf8'));
  fs.writeFileSync(path.join(dir, 'scripts', 'license.js'), '/* changed */');
  seaEntry.extract(get, base);
  assert.equal(crypto.createHash('sha256').update(fs.readFileSync(path.join(dir, 'scripts', 'license.js'))).digest('hex'), man.files['scripts/license.js'], 'a changed file is rewritten');
  fs.rmSync(path.join(dir, 'scripts', 'license.js'));
  assert.throws(() => seaEntry.extract(n => (n === 'scripts/license.js' ? Buffer.from('evil') : get(n)), base), /does not match its hash/);
  assert.equal(seaEntry.safeRel('../x.js'), false); assert.equal(seaEntry.safeRel('/etc/x'), false); assert.equal(seaEntry.safeRel('bridge/license.js'), true);
});

test('the build marks the Windows binary as a GUI program (no console window)', () => {
  const b = Buffer.alloc(512); b.writeUInt16LE(0x5a4d, 0); b.writeUInt32LE(0x80, 0x3c); b.writeUInt32LE(0x4550, 0x80); b.writeUInt16LE(0x20b, 0x80 + 24); b.writeUInt16LE(3, 0x80 + 24 + 68);
  assert.equal(build.setWindowsGui(b), 3); assert.equal(b.readUInt16LE(0x80 + 24 + 68), 2);
  assert.throws(() => build.setWindowsGui(Buffer.alloc(512)), /not a PE/);
});

test('key list: issued keys and edition sales are sealed on disk and decrypted in memory only after unlock', async t => {
  const dir = path.join(tmp(), 'vendor');
  t.after(() => fs.rmSync(path.dirname(dir), { recursive: true, force: true }));
  require('../../../scripts/license.js').initKeys(dir, { pin: PIN });   // a throwaway test pair, PIN-locked
  const { start } = require(path.join(KM, 'main.js'));
  const keylist = require(path.join(KM, 'keylist.js'));
  const app = start({ dirArgv: ['--dir', dir], root: path.dirname(dir), port: 0, openWindow: false });
  t.after(() => app.stop('test'));
  const { port } = await app.ready;

  let r = await req(port, { p: '/km/api/keys', headers: { 'X-Gen-Action': '1' } });
  assert.equal(r.json.locked, true);
  assert.equal((await post(port, '/issue', { plan: 'pro', name: 'Zed Tester' })).status, 401, 'issuing needs the PIN');
  assert.equal((await post(port, '/unlock', { pin: PIN })).status, 200);

  const one = await post(port, '/issue', { plan: 'pro', name: 'Zed Tester', email: 'zed@example.com', machine: 'ABCD-1234-EFGH-5678-IJKL' });
  assert.equal(one.status, 200); assert.match(one.json.key, /^AMIX1\./);
  const batch = await post(port, '/issue', { plan: 'studio', count: 3, days: 30 });
  assert.equal(batch.json.keys.length, 3);
  const sale = await post(port, '/edition', { edition: 'pro', order: 'ORD-77', buyer: 'Buyer Bee', whatsapp: '60123456789' });
  assert.equal(sale.status, 200, sale.text);

  const disk = fs.readFileSync(path.join(dir, keylist.ISSUED), 'utf8') + fs.readFileSync(path.join(dir, keylist.SALES), 'utf8');
  for (const secret of ['Zed Tester', 'zed@example.com', 'ABCD-1234', 'AMIX1.', 'Buyer Bee', '60123456789', one.json.key.split('.')[1]]) assert.ok(!disk.includes(secret), 'not in the clear on disk: ' + secret);
  assert.equal(disk.trim().split('\n').length, 5);

  r = await req(port, { p: '/km/api/keys', headers: { 'X-Gen-Action': '1' } });
  assert.equal(r.json.locked, false); assert.equal(r.json.rows.length, 5); assert.equal(r.json.damaged, 0);
  const zed = r.json.rows.find(x => x.name === 'Zed Tester');
  assert.ok(zed && zed.type === 'key' && zed.email === 'zed@example.com' && zed.machine === 'ABCD-1234-EFGH-5678-IJKL' && zed.key === one.json.key && zed.planName === 'PRO');
  assert.equal(r.json.rows.filter(x => x.type === 'key' && x.plan === 'studio' && x.expires).length, 3);
  const ed = r.json.rows.find(x => x.type === 'edition');
  assert.ok(ed.name === 'Buyer Bee' && ed.order === 'ORD-77' && ed.whatsapp === '60123456789' && ed.id === sale.json.serial);

  const csv = await req(port, { p: '/km/api/keys.csv?q=zed', headers: { 'X-Gen-Action': '1' } });
  assert.equal(csv.status, 200); assert.match(csv.headers['content-type'], /text\/csv/);
  const lines = csv.text.trim().split('\n');
  assert.equal(lines.length, 2); assert.match(lines[0], /^date,type,serial/); assert.ok(lines[1].includes('"Zed Tester"') && lines[1].includes(one.json.key));
  assert.ok(keylist.toCsv([{ at: 1, type: 'key', id: 'X', planName: 'PRO', name: '=HYPERLINK("x")' }]).includes('"\'=HYPERLINK(""x"")"'), 'a formula is never live in the CSV');

  assert.equal((await post(port, '/km/api/revoke', { id: zed.id, on: true, reason: 'refund' })).status, 200);
  assert.equal((await post(port, '/km/api/revoke', { id: '../x', on: true })).status, 400);
  r = await req(port, { p: '/km/api/keys', headers: { 'X-Gen-Action': '1' } });
  assert.equal(r.json.rows.find(x => x.id === zed.id).revoke.reason, 'refund'); assert.deepEqual(r.json.marked, [zed.id]);
  assert.ok(!fs.readFileSync(path.join(dir, keylist.MARKS), 'utf8').includes('Zed'), 'the marks file holds serials, not customers');

  assert.equal((await post(port, '/lock', {})).status, 200);
  r = await req(port, { p: '/km/api/keys', headers: { 'X-Gen-Action': '1' } });
  assert.deepEqual(Object.keys(r.json).sort(), ['count', 'dir', 'locked', 'ok']); assert.equal(r.json.count, 5);
  assert.equal((await req(port, { p: '/km/api/keys.csv', headers: { 'X-Gen-Action': '1' } })).status, 401);
  assert.equal((await post(port, '/km/api/revoke', { id: zed.id, on: false })).status, 401);
  // with the wrong key (another test pair) nothing opens
  const other = path.join(path.dirname(dir), 'other');
  require('../../../scripts/license.js').initKeys(other, {});
  fs.copyFileSync(path.join(dir, keylist.ISSUED), path.join(other, keylist.ISSUED));
  const wrong = keylist.listAll(other, null);
  assert.equal(wrong.rows.length, 0); assert.equal(wrong.damaged, 4);
});

test('the app window: Edge / Chrome app mode with its own locked-down profile, else the default browser', () => {
  const { findAppBrowser, appWindowArgs, prepareProfile } = require(path.join(KM, 'main.js'));
  const env = { 'ProgramFiles(x86)': 'C:\\PF86', ProgramFiles: 'C:\\PF', LOCALAPPDATA: 'C:\\L' };
  assert.equal(findAppBrowser('win32', env, f => f === path.join('C:\\PF86', 'Microsoft', 'Edge', 'Application', 'msedge.exe')), path.join('C:\\PF86', 'Microsoft', 'Edge', 'Application', 'msedge.exe'));
  assert.equal(findAppBrowser('win32', env, () => false), null);
  const a = appWindowArgs('http://127.0.0.1:8799/', 'P');
  assert.ok(a.includes('--app=http://127.0.0.1:8799/') && a.includes('--user-data-dir=P') && a.includes('--disable-sync'));
  const d = tmp(); prepareProfile(d);
  const prefs = JSON.parse(fs.readFileSync(path.join(d, 'Default', 'Preferences'), 'utf8'));
  assert.equal(prefs.credentials_enable_service, false, 'the PIN is never offered for saving');
  fs.rmSync(d, { recursive: true, force: true });
});

test('lifeline: stops when the last page is gone, not while a page or the window lives', async () => {
  const { lifeline } = require(path.join(KM, 'main.js'));
  let stopped = 0; let now = 0;
  const l = lifeline({ graceMs: 10, idleMs: 1000, firstMs: 5000, now: () => now, onEmpty: () => stopped++ });
  l.check(); assert.equal(stopped, 0, 'waits for the first page');
  l.alive('a'); l.alive('b'); l.bye('a'); await new Promise(r => setTimeout(r, 30)); assert.equal(stopped, 0, 'another page is open');
  l.window(true); l.bye('b'); await new Promise(r => setTimeout(r, 30)); assert.equal(stopped, 0, 'the window still runs');
  l.window(false); await new Promise(r => setTimeout(r, 30)); assert.equal(stopped, 1);
  const m = lifeline({ graceMs: 10, idleMs: 1000, firstMs: 5000, now: () => now, onEmpty: () => stopped++ });
  m.alive('x'); now += 2000; m.check(); assert.equal(stopped, 2, 'a page silent for idleMs counts as closed'); m.stop(); l.stop();
});
