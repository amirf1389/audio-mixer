#!/usr/bin/env node
'use strict';
// Local audio sandbox bridge: zero dependencies, listens on loopback only.
//   GET /api/status   -> { ok, name, version }
//   GET /api/drivers  -> native driver/device detection for this OS
//   GET /api/volume   -> system output / input volume + mute
//   WS  /ws/output    -> page streams Int16 PCM out through PortAudio (ASIO / WASAPI)
//   WS  /ws/input     -> bridge streams Int16 PCM captured from an ASIO / WASAPI input
//   GET /api/catalog  -> official audio drivers / stacks for this OS, with install detection
//   GET /api/nowplaying -> what Spotify / YouTube / YouTube Music / TIDAL / ... is playing (OS media sessions)
//   GET /api/interfaces -> every audio interface grouped across ASIO / WASAPI / DirectSound / Core Audio / ALSA ...
//   POST /api/catalog/download {id} -> saves the official installer (FlexASIO) to the download folder; never runs it
//   GET /*            -> serves the mixer page from the repo root (same-origin use)
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const { detect } = require('./detect');
const { accept } = require('./ws');
const { createSession } = require('./output');
const { createInputSession } = require('./input');
const { readVolume } = require('./volume');
const { listCatalog, downloadDriver, downloadDir } = require('./catalog');
const { cachedNowPlaying } = require('./nowplaying');
const { groupInterfaces } = require('./interfaces');

const VERSION = (() => { try { return require('../package.json').version; } catch (_) { return '1.0.0'; } })();
const PORT = Number(process.env.BRIDGE_PORT) || 8765;
const HOST = '127.0.0.1';
const ROOT = path.resolve(__dirname, '..');
const EXTRA_ORIGINS = (process.env.BRIDGE_ORIGINS || '').split(',').map(s => s.trim()).filter(Boolean);
const EXTRA_HOSTS = (process.env.BRIDGE_HOSTS || '').split(',').map(s => s.trim().toLowerCase()).filter(Boolean);
const ALLOW_NULL_ORIGIN = process.env.BRIDGE_ALLOW_NULL_ORIGIN !== '0'; // file:// pages send "null"; set 0 to refuse
const SECURITY_HEADERS = { 'X-Content-Type-Options': 'nosniff', 'Referrer-Policy': 'no-referrer' };
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.css': 'text/css', '.json': 'application/json', '.png': 'image/png', '.svg': 'image/svg+xml' };

// Only local pages (and file:// pages, which send "null") may read the API.
function originAllowed(o) {
  if (!o) return true;
  if (o === 'null') return ALLOW_NULL_ORIGIN;
  if (EXTRA_ORIGINS.includes(o)) return true;
  try { const h = new URL(o).hostname; return h === 'localhost' || h === '127.0.0.1' || h === '[::1]'; } catch (_) { return false; }
}

// DNS-rebinding guard: the Host header must name this machine's loopback, whatever the Origin says.
function hostAllowed(h) {
  if (!h) return false;
  const name = String(h).toLowerCase().replace(/:\d+$/, '');
  return name === 'localhost' || name === '127.0.0.1' || name === '[::1]' || EXTRA_HOSTS.includes(name);
}

function send(res, code, body, headers = {}) {
  res.writeHead(code, { 'Cache-Control': 'no-store', ...SECURITY_HEADERS, ...headers });
  res.end(body);
}
const json = (res, code, obj, h) => send(res, code, JSON.stringify(obj), { 'Content-Type': 'application/json', ...h });

const server = http.createServer(async (req, res) => {
  try { await handle(req, res); }
  catch (_) { if (!res.headersSent) json(res, 500, { ok: false, error: 'internal error' }); else res.destroy(); }
});

async function handle(req, res) {
  const origin = req.headers.origin;
  if (!hostAllowed(req.headers.host)) return json(res, 403, { ok: false, error: 'host not allowed' });
  if (!originAllowed(origin)) return json(res, 403, { ok: false, error: 'origin not allowed' });
  const cors = origin ? { 'Access-Control-Allow-Origin': origin, 'Vary': 'Origin', 'Access-Control-Allow-Private-Network': 'true' } : {};
  if (req.method === 'OPTIONS') return send(res, 204, '', { ...cors, 'Access-Control-Allow-Methods': 'GET, POST', 'Access-Control-Allow-Headers': 'Content-Type, X-Mixer-Action' });
  const url = new URL(req.url, `http://${HOST}`);
  if (req.method === 'POST' && url.pathname === '/api/catalog/download') return handleDownload(req, res, cors);
  if (req.method !== 'GET') return json(res, 405, { ok: false, error: 'method not allowed' }, cors);
  if (url.pathname === '/api/status') return json(res, 200, { ok: true, name: 'audio-mixer-bridge', version: VERSION }, cors);
  if (url.pathname === '/api/drivers') {
    try { return json(res, 200, { ok: true, ...(await detect()) }, cors); }
    catch (e) { return json(res, 500, { ok: false, error: e.message }, cors); }
  }

  if (url.pathname === '/api/nowplaying') {
    try { return json(res, 200, { ok: true, ...(await cachedNowPlaying()) }, cors); }
    catch (e) { return json(res, 500, { ok: false, error: e.message }, cors); }
  }

  if (url.pathname === '/api/interfaces') {
    try {
      const info = await detect();
      const devices = info.portaudio ? info.portaudio.devices : [];
      return json(res, 200, { ok: true, platform: info.platform, portaudio: !!info.portaudio, asio: info.asio, interfaces: groupInterfaces(devices, info.asio) }, cors);
    } catch (e) { return json(res, 500, { ok: false, error: e.message }, cors); }
  }

  if (url.pathname === '/api/catalog') {
    try {
      const info = await detect();
      return json(res, 200, { ok: true, platform: info.platform, downloadDir: downloadDir(), items: listCatalog(info) }, cors);
    } catch (e) { return json(res, 500, { ok: false, error: e.message }, cors); }
  }

  if (url.pathname === '/api/volume') {
    try { return json(res, 200, { ok: true, ...(await readVolume()) }, cors); }
    catch (e) { return json(res, 500, { ok: false, error: e.message }, cors); }
  }

  // Static page (path-traversal safe)
  let rel;
  try { rel = decodeURIComponent(url.pathname); } catch (_) { return json(res, 400, { ok: false, error: 'bad request' }); }
  if (rel.includes('\0')) return json(res, 400, { ok: false, error: 'bad request' });
  if (rel === '/') rel = '/gemini-code-1790419147591.html';
  const file = path.resolve(ROOT, '.' + rel);
  if (file !== ROOT && !file.startsWith(ROOT + path.sep)) return json(res, 403, { ok: false, error: 'forbidden' });
  // Never serve dotfiles/dirs (.git, .github, .vscode, .env ...) or the bridge's own sources.
  if (path.relative(ROOT, file).split(path.sep).some(p => p.startsWith('.')) || file.startsWith(__dirname + path.sep)) return json(res, 404, { ok: false, error: 'not found' });
  fs.readFile(file, (err, buf) => {
    if (err) return json(res, 404, { ok: false, error: 'not found' });
    const type = TYPES[path.extname(file)] || 'application/octet-stream';
    send(res, 200, buf, { 'Content-Type': type, ...(type.startsWith('text/html') ? { 'X-Frame-Options': 'SAMEORIGIN' } : {}) });
  });
}

// Driver download: browsers must send a custom header (forces a CORS preflight, so foreign pages cannot trigger it).
let downloading = false;
async function handleDownload(req, res, cors) {
  if (req.headers['x-mixer-action'] !== 'download' || !String(req.headers['content-type'] || '').startsWith('application/json')) {
    return json(res, 400, { ok: false, error: 'missing X-Mixer-Action header or JSON body' }, cors);
  }
  let body = '';
  for await (const chunk of req) { body += chunk; if (body.length > 1024) return json(res, 413, { ok: false, error: 'request too large' }, cors); }
  let id;
  try { id = JSON.parse(body).id; } catch (_) { return json(res, 400, { ok: false, error: 'invalid JSON' }, cors); }
  if (typeof id !== 'string' || !/^[a-z0-9-]{1,32}$/.test(id)) return json(res, 400, { ok: false, error: 'invalid id' }, cors);
  if (downloading) return json(res, 409, { ok: false, error: 'a download is already running' }, cors);
  downloading = true;
  try { return json(res, 200, await downloadDriver(id), cors); }
  catch (e) { return json(res, e.status || 500, { ok: false, error: e.message }, cors); }
  finally { downloading = false; }
}

// WebSocket /ws/output: page streams Int16 PCM, bridge plays it via PortAudio (ASIO when available).
server.on('upgrade', (req, socket) => {
  let pathname = '';
  try { pathname = new URL(req.url, `http://${HOST}`).pathname; } catch (_) { /* rejected below */ }
  if ((pathname !== '/ws/output' && pathname !== '/ws/input') || !hostAllowed(req.headers.host) || !originAllowed(req.headers.origin)) { socket.destroy(); return; }
  const handlers = {};
  const conn = accept(req, socket, handlers);
  if (!conn) return;
  Object.assign(handlers, pathname === '/ws/input' ? createInputSession(conn) : createSession(conn));
});

// Starts listening (used by the client launcher); resolves with the port.
function start(port = PORT) {
  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(port, HOST, () => { server.removeListener('error', reject); resolve(server.address().port); });
  });
}

if (require.main === module) {
  start().then(port => console.log(`Audio Mixer bridge running: http://localhost:${port}  (open this URL to use the mixer)`));
}
module.exports = { server, originAllowed, hostAllowed, HOST, PORT, start };
