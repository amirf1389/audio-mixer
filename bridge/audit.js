#!/usr/bin/env node
'use strict';
// Security self-check for the bridge and the mixer page. Zero dependencies.
//   node audit.js                      -> starts the bridge in-process on a random port and probes it
//   node audit.js --url http://127.0.0.1:8765   -> probes an already running bridge
//   add --json for machine-readable output. Exit code 1 when any check FAILs.
const http = require('node:http');
const net = require('node:net');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const PAGE = path.resolve(__dirname, '..', 'gemini-code-1790419147591.html');

function request(base, p, { method = 'GET', headers = {} } = {}) {
  const u = new URL(base);
  return new Promise(resolve => {
    const req = http.request({ host: u.hostname, port: u.port, path: p, method, headers, timeout: 4000 }, res => {
      const chunks = [];
      res.on('data', c => chunks.push(c));
      res.on('end', () => resolve({ status: res.statusCode, headers: res.headers, body: Buffer.concat(chunks).toString() }));
    });
    req.on('timeout', () => req.destroy(new Error('timeout')));
    req.on('error', e => resolve({ status: 0, error: e.message, headers: {}, body: '' }));
    req.end();
  });
}

// Raw WebSocket handshake; resolves to { upgraded, socket }.
function wsHandshake(base, { origin, host } = {}) {
  const u = new URL(base);
  return new Promise(resolve => {
    const s = net.connect(Number(u.port), u.hostname);
    let buf = '';
    const done = r => { s.removeAllListeners('data'); resolve({ ...r, socket: s }); };
    s.setTimeout(3000, () => { s.destroy(); done({ upgraded: false }); });
    s.on('error', () => done({ upgraded: false }));
    s.on('close', () => done({ upgraded: false }));
    s.on('data', d => { buf += d; if (buf.includes('\r\n\r\n')) done({ upgraded: buf.startsWith('HTTP/1.1 101') }); });
    s.write(`GET /ws/output HTTP/1.1\r\nHost: ${host || u.host}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n` +
      `Sec-WebSocket-Key: ${crypto.randomBytes(16).toString('base64')}\r\nSec-WebSocket-Version: 13\r\n` +
      (origin ? `Origin: ${origin}\r\n` : '') + '\r\n');
  });
}

function closedAfter(socket, bytes) {
  return new Promise(resolve => {
    let done = false;
    const finish = v => { if (!done) { done = true; resolve(v); } };
    socket.once('close', () => finish(true));
    socket.once('end', () => finish(true));
    setTimeout(() => finish(socket.destroyed), 1500);
    socket.write(bytes);
  });
}

async function probe(base) {
  const results = [];
  const add = (id, level, title, detail) => results.push({ id, level, title, detail });
  const ok = (id, cond, title, failDetail, level = 'FAIL') => add(id, cond ? 'PASS' : level, title, cond ? '' : failDetail);

  const st = await request(base, '/api/status');
  if (st.status !== 200) { add('reach', 'FAIL', 'Bridge reachable', st.error || `HTTP ${st.status}`); return results; }
  add('reach', 'PASS', 'Bridge reachable', '');

  ok('origin', (await request(base, '/api/drivers', { headers: { Origin: 'https://evil.example' } })).status === 403,
    'Foreign web origins cannot read the API', 'a random website could read your device list');
  ok('rebind', (await request(base, '/api/drivers', { headers: { Host: 'attacker.example' } })).status === 403,
    'DNS-rebinding (foreign Host header) rejected', 'a rebinding attack could read the API without an Origin header');
  ok('method', (await request(base, '/api/status', { method: 'POST' })).status === 405,
    'Only GET is accepted', 'non-GET methods are accepted');

  const trav = await Promise.all(['/%2e%2e/%2e%2e/etc/passwd', '/..%2f..%2fetc%2fpasswd', '/%2e%2e%5c%2e%2e%5cwindows%5cwin.ini'].map(p => request(base, p)));
  ok('traversal', trav.every(r => r.status !== 200), 'Path traversal blocked', 'a ../ path returned a file');

  const bad = await request(base, '/%E0%A4%A');
  const alive = (await request(base, '/api/status')).status === 200;
  ok('malformed', bad.status === 400 && alive, 'Malformed URL encoding handled', `got HTTP ${bad.status}${alive ? '' : ' and the bridge stopped responding'}`);

  const hidden = await Promise.all(['/.git/config', '/.github/workflows/codeql.yml', '/.vscode/launch.json', '/.gitignore', '/bridge/server.js', '/bridge/package.json'].map(p => request(base, p)));
  ok('hidden', hidden.every(r => r.status !== 200), 'Dotfiles, .git and bridge sources not served', 'a private file is downloadable');

  ok('nosniff', st.headers['x-content-type-options'] === 'nosniff', 'X-Content-Type-Options: nosniff set', 'header missing', 'WARN');
  const page = await request(base, '/');
  ok('frame', page.status !== 200 || /sameorigin|deny/i.test(page.headers['x-frame-options'] || ''), 'Mixer page protected from clickjacking (X-Frame-Options)', 'header missing', 'WARN');

  const wsForeign = await wsHandshake(base, { origin: 'https://evil.example' }); wsForeign.socket.destroy();
  ok('ws-origin', !wsForeign.upgraded, 'WebSocket output rejects foreign origins', 'any website could stream audio to your interface');
  const wsHost = await wsHandshake(base, { origin: `http://localhost:${new URL(base).port}`, host: 'attacker.example' }); wsHost.socket.destroy();
  ok('ws-rebind', !wsHost.upgraded, 'WebSocket output rejects foreign Host header', 'DNS rebinding could stream audio to your interface');

  const ws = await wsHandshake(base, { origin: `http://localhost:${new URL(base).port}` });
  if (!ws.upgraded) add('ws-limit', 'WARN', 'Oversized WebSocket frames rejected', 'could not open a local WebSocket to test');
  else {
    const head = Buffer.alloc(14); head[0] = 0x82; head[1] = 0x80 | 127; head.writeBigUInt64BE(BigInt(64 * 1024 * 1024), 2);
    ok('ws-limit', await closedAfter(ws.socket, head), 'Oversized WebSocket frames rejected', 'a 64 MB frame was accepted (memory exhaustion)');
    ws.socket.destroy();
  }

  const nul = await request(base, '/api/status', { headers: { Origin: 'null' } });
  ok('null-origin', nul.status === 403, 'Origin "null" refused', 'allowed so file:// pages work, but sandboxed iframes on any site also send "null". If you open the mixer via http://localhost, set BRIDGE_ALLOW_NULL_ORIGIN=0', 'WARN');
  return results;
}

// Static review of the mixer page for known risky patterns.
function scanPage(file = PAGE) {
  const results = [];
  const add = (id, level, title, detail) => results.push({ id, level, title, detail });
  let src;
  try { src = fs.readFileSync(file, 'utf8'); } catch (_) { add('page', 'WARN', 'Mixer page found', file + ' not readable'); return results; }
  const notify = /window\.notify\s*=\s*function[\s\S]{0,1200}?\n\s*};/.exec(src);
  add('notify-xss', notify && /\.textContent\s*=\s*m\b|textContent = String\(m/.test(notify[0]) ? 'PASS' : 'FAIL', 'Notifications render text, not HTML',
    'window.notify writes messages with innerHTML; device names in messages can inject script');
  const ble = /renderBLEDeviceList = function[\s\S]{0,5000}?join\(''\)/.exec(src);
  add('ble-xss', ble && !/\$\{d\.name\}/.test(ble[0]) ? 'PASS' : 'FAIL', 'Bluetooth device names escaped', 'a nearby device advertising an HTML name can inject script');
  add('pin-lockout', /pinLockedUntil/.test(src) ? 'PASS' : 'WARN', 'Admin PIN has a brute-force lockout', 'unlimited PIN guesses');
  const noSri = [...src.matchAll(/<script[^>]+src="(https?:[^"]+)"(?![^>]*integrity=)/g)].map(m => m[1]);
  add('sri', noSri.length ? 'WARN' : 'PASS', 'Third-party scripts pinned with Subresource Integrity',
    noSri.length ? `${noSri.join(', ')} load without integrity hashes (a compromised CDN could run code in the page)` : '');
  add('default-pin', /DEFAULT_ADMIN_PIN\s*=/.test(src) ? 'WARN' : 'PASS', 'No hard-coded admin PIN',
    'the default admin PIN is in the page source; it is a UI lock only, not a security boundary');
  const evalUse = (src.match(/\beval\(|new Function\(/g) || []).length;
  add('eval', evalUse ? 'WARN' : 'PASS', 'No eval()/new Function()', evalUse ? `${evalUse} use(s) found` : '');
  return results;
}

async function main() {
  const args = process.argv.slice(2);
  const asJson = args.includes('--json');
  const i = args.indexOf('--url');
  let base = i >= 0 ? args[i + 1] : null, server = null;
  const results = [];
  if (!base) {
    const mod = require('./server');
    server = mod.server;
    results.push({ id: 'bind', level: mod.HOST === '127.0.0.1' ? 'PASS' : 'FAIL', title: 'Listens on loopback only', detail: mod.HOST === '127.0.0.1' ? '' : `binds ${mod.HOST}` });
    await new Promise(r => server.listen(0, '127.0.0.1', r));
    base = `http://127.0.0.1:${server.address().port}`;
  }
  results.push(...await probe(base), ...scanPage());
  if (server) { server.closeAllConnections(); server.close(); }
  const fails = results.filter(r => r.level === 'FAIL').length, warns = results.filter(r => r.level === 'WARN').length;
  if (asJson) console.log(JSON.stringify({ base, fails, warns, results }, null, 2));
  else {
    const icon = { PASS: '✔', WARN: '!', FAIL: '✘' };
    console.log(`Audio Mixer security audit (${base})\n`);
    for (const r of results) console.log(`${icon[r.level]} ${r.level.padEnd(4)} ${r.title}${r.detail ? `\n         ${r.detail}` : ''}`);
    console.log(`\n${results.length - fails - warns} passed, ${warns} warning(s), ${fails} failure(s)`);
  }
  process.exitCode = fails ? 1 : 0;
}

if (require.main === module) main();
module.exports = { probe, scanPage };
