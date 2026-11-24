#!/usr/bin/env node
'use strict';
// AmF X1 Key Manager: the vendor's licence key generator (scripts/license-generator.js) as a desktop app. It starts the generator's own server
// on 127.0.0.1 ONLY (port 8799 like the generator, a free port when that is taken), opens its page in an app window (Microsoft Edge or Chrome
// in --app mode with a private profile, else the default browser), and stops the server when that window closes. All signing, the PIN lock,
// single / batch keys, edition tokens, SEND TO AMF X1 ON THIS PC and PUBLISH UPDATE are the generator's code, unchanged; this file adds only
//   GET /km/keys          the KEY LIST page (tools/key-manager/keylist.js): issued keys + edition sales, decrypted in memory after unlock
//   GET /km/api/keys(.csv), POST /km/api/revoke     its data, CSV export and revocation marks
//   POST /km/alive, /km/bye                           the window's heartbeat: no open page for a while = the app was closed = stop
// and records every key issued through it, sealed to the vendor key, in <key folder>/issued.jsonl.
// The private key stays in the key folder (~/.audio-mixer-vendor, %USERPROFILE%\.audio-mixer-vendor on Windows, or --dir / AUDIO_MIXER_VENDOR_DIR);
// it is never bundled, copied or logged. No telemetry: the only outbound request is the generator's own public-IP lookup for PUBLISH VENDOR ADDRESS.
//   node tools/key-manager/main.js [--dir D] [--root R] [--port 8799|0] [--no-window]
const http = require('node:http');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawn } = require('node:child_process');
const gen = require('../../scripts/license-generator');
const { hostAllowed, originAllowed } = require('../../bridge/server');
const keylist = require('./keylist');

const NAME = 'AmF X1 Key Manager';
const arg = (a, n, d) => { const i = a.indexOf(n); return i >= 0 && a[i + 1] !== undefined ? a[i + 1] : d; };

function appDataDir(env = process.env, platform = process.platform, home = os.homedir()) {
  if (platform === 'win32') return path.join(env.LOCALAPPDATA || env.APPDATA || home, NAME);
  if (platform === 'darwin') return path.join(home, 'Library', 'Application Support', NAME);
  return path.join(env.XDG_CACHE_HOME || path.join(home, '.cache'), 'amf-x1-key-manager');
}

// Edge / Chrome in app mode (a window with no tabs or address bar). Windows has Edge on every supported version.
function findAppBrowser(platform = process.platform, env = process.env, exists = fs.existsSync) {
  let c = [];
  if (platform === 'win32') {
    const roots = [env['ProgramFiles(x86)'], env.ProgramFiles, env.LOCALAPPDATA].filter(Boolean);
    for (const r of roots) c.push(path.join(r, 'Microsoft', 'Edge', 'Application', 'msedge.exe'));
    for (const r of roots) c.push(path.join(r, 'Google', 'Chrome', 'Application', 'chrome.exe'));
  } else if (platform === 'darwin') {
    c = ['/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', '/Applications/Chromium.app/Contents/MacOS/Chromium'];
  } else c = ['/usr/bin/microsoft-edge', '/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser', '/snap/bin/chromium'];
  return c.find(f => { try { return exists(f); } catch (_) { return false; } }) || null;
}

// The window's own profile: no password manager (the PIN is never offered for saving), no autofill, no sync, no extensions.
function prepareProfile(dir) {
  const prefs = path.join(dir, 'Default', 'Preferences');
  if (fs.existsSync(prefs)) return;
  fs.mkdirSync(path.dirname(prefs), { recursive: true, mode: 0o700 });
  fs.writeFileSync(prefs, JSON.stringify({ credentials_enable_service: false, credentials_enable_autosignin: false, profile: { password_manager_enabled: false }, autofill: { enabled: false, profile_enabled: false, credit_card_enabled: false }, browser: { has_seen_welcome_page: true, check_default_browser: false }, translate: { enabled: false } }));
}
function appWindowArgs(url, profile) {
  return ['--app=' + url, '--user-data-dir=' + profile, '--no-first-run', '--no-default-browser-check', '--disable-sync', '--disable-extensions',
    '--disable-background-networking', '--disable-background-mode', '--disable-component-update', '--window-size=820,960'];
}

// The generator's page, branded, with a KEY LIST link and the heartbeat script; its CSP keeps the generator's hashes and adds this script's.
const HEARTBEAT = `(function () {
  var tab = Math.random().toString(36).slice(2), h = { 'Content-Type': 'application/json', 'X-Gen-Action': '1' };
  var ping = function () { fetch('/km/alive', { method: 'POST', headers: h, body: JSON.stringify({ tab: tab }) }).catch(function () {}); };
  ping(); setInterval(ping, 5000);
  addEventListener('pagehide', function () { try { fetch('/km/bye', { method: 'POST', headers: h, body: JSON.stringify({ tab: tab }), keepalive: true }); } catch (e) {} });
})();`;
let mainPage = null;
function brandedPage() {
  if (mainPage) return mainPage;
  const p = gen.pageFor('vendor');
  let body = p.body.replace(/<title>[^<]*<\/title>/, '<title>' + NAME + '</title>')
    .replace('<h1>AUDIO <b>MIXER</b></h1>', '<h1>AMF X1 <b>KEY MANAGER</b></h1>')
    .replace('<span class="pill">VENDOR KEY</span>', '<span class="pill">VENDOR KEY</span> <a class="pill" id="km-keys" href="/km/keys">KEY LIST</a>');
  body = body.replace(/<\/body>\s*<\/html>\s*$/, '<script>' + HEARTBEAT + '</script>\n</body>\n</html>');
  const csp = p.csp.replace('script-src ', 'script-src ' + keylist.hash(HEARTBEAT) + ' ');
  return (mainPage = { body, csp });
}
const heartbeatPage = pg => {   // the key list page gets the same heartbeat
  const body = pg.body.replace(/<\/body>\s*<\/html>\s*$/, '<script>' + HEARTBEAT + '</script>\n</body>\n</html>');
  return { body, csp: pg.csp.replace('script-src ', 'script-src ' + keylist.hash(HEARTBEAT) + ' ') };
};

// Tracks the open pages. Stop when none has been seen for idleMs (a minimized window may be throttled to one timer a minute), or graceMs after
// the last one said goodbye (closed) and no other page or reload followed, or when the app window process ends.
function lifeline({ graceMs = 4000, idleMs = 150000, firstMs = 180000, now = () => Date.now(), onEmpty }) {
  const tabs = new Map(), born = now(); let seen = false, timer = null, windowAlive = false, done = false;
  const check = () => {
    if (done) return;
    for (const [id, t] of tabs) if (now() - t > idleMs) tabs.delete(id);
    if (tabs.size || windowAlive) return;
    if (seen || now() - born > firstMs) { done = true; onEmpty(); }
  };
  const tick = setInterval(check, 5000); tick.unref();
  return {
    alive(id) { seen = true; tabs.set(String(id || '?').slice(0, 40), now()); },
    bye(id) { tabs.delete(String(id || '?').slice(0, 40)); clearTimeout(timer); timer = setTimeout(check, graceMs); timer.unref(); },
    window(on) { windowAlive = !!on; if (!on) { clearTimeout(timer); timer = setTimeout(check, graceMs); timer.unref(); } },
    check, stop() { done = true; clearInterval(tick); clearTimeout(timer); }, get size() { return tabs.size; },
  };
}

function start({ dirArgv = [], root, port = 8799, openWindow = true, graceMs, idleMs, firstMs, log = () => {}, onStop } = {}) {
  const session = gen.makeSession();
  const vendorDir = () => gen.KINDS.vendor.mod.vendorDir(dirArgv);
  const genServer = gen.createServer({ kind: 'vendor', dirArgv, root, session });
  const genHandler = genServer.listeners('request')[0];
  const send = (res, code, body, headers = {}) => { res.writeHead(code, { 'Content-Type': typeof body === 'string' ? 'text/html; charset=utf-8' : 'application/json; charset=utf-8', 'X-Content-Type-Options': 'nosniff', 'Cache-Control': 'no-store', 'X-Frame-Options': 'DENY', 'Referrer-Policy': 'no-referrer', ...headers }); res.end(typeof body === 'string' ? body : JSON.stringify(body)); };
  const readJson = async req => { const chunks = []; let n = 0; for await (const c of req) { n += c.length; if (n > 8192) return null; chunks.push(c); } try { return JSON.parse(Buffer.concat(chunks).toString('utf8') || '{}'); } catch (_) { return null; } };
  let stopping = null, child = null;
  const life = lifeline({ graceMs, idleMs, firstMs, onEmpty: () => stop('window closed') });

  const server = http.createServer(async (req, res) => {
    try {
      const url = new URL(req.url, 'http://x');
      if (!hostAllowed(req.headers.host)) return send(res, 403, { ok: false, error: 'host not allowed' });   // DNS rebinding: only 127.0.0.1 / localhost
      const km = url.pathname === '/' || url.pathname.startsWith('/km/');
      if (km) {
        if (!originAllowed(req.headers.origin) || req.headers.origin === 'null') return send(res, 403, { ok: false, error: 'origin not allowed' });
        // like the generator: actions (and here the data reads too) need this page's own X-Gen-Action header, which no foreign page can send
        const api = url.pathname.startsWith('/km/api/') || req.method === 'POST';
        if (api && req.headers['x-gen-action'] !== '1') return send(res, 403, { ok: false, error: 'actions need the Key Manager page' });
        if (req.method === 'POST' && !String(req.headers['content-type'] || '').startsWith('application/json')) return send(res, 403, { ok: false, error: 'actions need the Key Manager page' });
        if (req.method === 'GET' && url.pathname === '/') { const p = brandedPage(); return send(res, 200, p.body, { 'Content-Security-Policy': p.csp }); }
        if (req.method === 'GET' && url.pathname === '/km/keys') { const p = heartbeatPage(keylist.pageWithCsp()); return send(res, 200, p.body, { 'Content-Security-Policy': p.csp }); }
        if (req.method === 'GET' && (url.pathname === '/km/api/keys' || url.pathname === '/km/api/keys.csv')) {
          const r = keylist.listAll(vendorDir(), session);
          if (url.pathname === '/km/api/keys') return send(res, 200, { ok: true, ...r });
          if (r.locked) return send(res, 401, { ok: false, error: 'the key folder is locked: unlock it with your PIN', locked: true });
          const q = url.searchParams.get('q') || '', type = url.searchParams.get('type') || '';
          const rows = r.rows.filter(x => (type === 'revoke' ? !!x.revoke : !type || x.type === type) && keylist.matches(x, q));
          res.writeHead(200, { 'Content-Type': 'text/csv; charset=utf-8', 'Content-Disposition': 'attachment; filename="amfx1-keys.csv"', 'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff' });
          return res.end(keylist.toCsv(rows));
        }
        if (req.method === 'POST' && url.pathname === '/km/api/revoke') {
          const b = await readJson(req); if (!b) return send(res, 400, { ok: false, error: 'the body is not JSON' });
          if (!keylist.privateKeyFor(vendorDir(), session)) return send(res, 401, { ok: false, error: 'the key folder is locked: unlock it with your PIN', locked: true });
          try { keylist.setMark(vendorDir(), b.id, !!b.on, b.reason); return send(res, 200, { ok: true }); } catch (e) { return send(res, e.status || 500, { ok: false, error: e.message }); }
        }
        if (req.method === 'POST' && (url.pathname === '/km/alive' || url.pathname === '/km/bye')) {
          const b = (await readJson(req)) || {};
          if (url.pathname === '/km/alive') life.alive(b.tab); else life.bye(b.tab);
          return send(res, 200, { ok: true });
        }
        return send(res, 404, { ok: false, error: 'not found' });
      }
      if (req.method === 'POST' && url.pathname === '/issue') {   // the generator signs; the key list records what it returned (sealed)
        const end = res.end;
        res.end = function (chunk, ...rest) {
          try { if (res.statusCode === 200 && chunk) keylist.recordIssued(vendorDir(), JSON.parse(String(chunk))); }
          catch (e) { log('could not record an issued key in the key list: ' + e.message); }   // the message never contains the key
          return end.call(this, chunk, ...rest);
        };
      }
      return genHandler(req, res);
    } catch (e) { try { send(res, 500, { ok: false, error: e.message }); } catch (_) { /* already answered */ } }
  });
  server.headersTimeout = 10000; server.requestTimeout = 30000;

  let resolveStopped; const stopped = new Promise(r => { resolveStopped = r; });
  function stop(reason) {
    if (stopping) return stopping;
    life.stop(); session.close();   // the unlocked key is dropped from memory first
    if (child && child.exitCode === null) { try { child.kill(); } catch (_) { /* gone */ } }
    stopping = new Promise(r => { server.close(() => r()); if (server.closeAllConnections) server.closeAllConnections(); }).then(() => { log(NAME + ' stopped (' + reason + ').'); resolveStopped(reason); if (onStop) onStop(reason); return reason; });
    return stopping;
  }

  const ready = new Promise((resolve, reject) => {
    server.once('error', e => {
      if (e.code === 'EADDRINUSE' && port) { log('Port ' + port + ' is in use: using a free port instead.'); server.once('error', reject); server.listen(0, '127.0.0.1'); }
      else reject(e);
    });
    server.once('listening', () => {
      const url = `http://127.0.0.1:${server.address().port}/`;
      log(NAME + ' on ' + url + ' (key folder ' + vendorDir() + '). Close the window to stop.');
      if (openWindow) openAppWindow(url);
      resolve({ url, port: server.address().port });
    });
    server.listen(port, '127.0.0.1');   // loopback only, never another interface
  });

  function openAppWindow(url) {
    const exe = findAppBrowser();
    if (exe) {
      const profile = path.join(appDataDir(), 'window-profile');
      try { prepareProfile(profile); } catch (_) { /* the window still works */ }
      const t0 = Date.now();
      try {
        child = spawn(exe, appWindowArgs(url, profile), { stdio: 'ignore', shell: false, windowsHide: false });
        life.window(true);
        child.on('error', () => { child = null; life.window(false); fallback(url); });
        // a window that ends within seconds handed over to an already running copy of the browser: the heartbeat decides then
        child.on('exit', () => { life.window(false); if (Date.now() - t0 > 5000) life.check(); });
        log('App window: ' + path.basename(exe));
        return;
      } catch (_) { /* fall back */ }
    }
    fallback(url);
  }
  function fallback(url) {
    const opener = gen.browserCommand(url);   // the generator's safe opener (no shell)
    if (!opener) return log('Open ' + url + ' in a browser.');
    try { const ch = spawn(opener[0], opener[1], { stdio: 'ignore', detached: true, shell: false, windowsHide: true }); ch.on('error', () => log('Open ' + url + ' in a browser.')); ch.unref(); } catch (_) { log('Open ' + url + ' in a browser.'); }
  }

  return { server, session, ready, stop, stopped, life };
}

// The command line / the packaged .exe (sea-entry.js calls this with { sea: true }).
function cli(argv = process.argv.slice(2), { sea = false, exeDir } = {}) {
  const dataDir = appDataDir();
  const logFile = sea ? path.join(dataDir, 'key-manager.log') : null;
  const log = m => {
    const line = new Date().toISOString() + '  ' + m;
    try { console.log(m); } catch (_) { /* no console (the .exe has none) */ }
    if (logFile) try { fs.mkdirSync(dataDir, { recursive: true }); fs.appendFileSync(logFile, line + '\n'); } catch (_) { /* best effort */ }
  };
  const dirArgv = argv.includes('--dir') ? ['--dir', arg(argv, '--dir')] : [];
  // where PUBLISH UPDATE looks for releases/ and writes vendor.json: --root, else this project (from source), else the folder of the .exe
  const root = arg(argv, '--root') || (sea ? exeDir : path.resolve(__dirname, '..', '..'));
  const want = arg(argv, '--port', '8799'), port = want === '0' ? 0 : Number(want) || 8799;
  const ms = n => (arg(argv, n) && Number(arg(argv, n)) > 0 ? Number(arg(argv, n)) : undefined);
  const app = start({ dirArgv, root, port, openWindow: !argv.includes('--no-window'), graceMs: ms('--grace-ms'), idleMs: ms('--idle-ms'), firstMs: ms('--first-ms'), log, onStop: () => setTimeout(() => process.exit(0), 50) });
  app.ready.catch(e => { log('Could not start: ' + e.message); if (sea) alertBox('The Key Manager could not start: ' + e.message); process.exitCode = 1; setTimeout(() => process.exit(1), 50); });
  for (const s of ['SIGINT', 'SIGTERM', 'SIGHUP']) { try { process.on(s, () => app.stop('signal ' + s)); } catch (_) { /* not on this OS */ } }
  return app;
}
// a message box for the windowless .exe (the text goes in through the environment, never into the command)
function alertBox(text) {
  if (process.platform !== 'win32') return;
  try { spawn('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', "Add-Type -AssemblyName PresentationFramework; [System.Windows.MessageBox]::Show($env:KM_MSG, 'AmF X1 Key Manager') | Out-Null"], { stdio: 'ignore', windowsHide: true, env: { ...process.env, KM_MSG: String(text).slice(0, 500) } }); } catch (_) { /* nothing more to do */ }
}

module.exports = { start, cli, lifeline, findAppBrowser, appWindowArgs, appDataDir, brandedPage, prepareProfile, NAME };
if (require.main === module) cli();
