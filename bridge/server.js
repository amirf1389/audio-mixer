#!/usr/bin/env node
'use strict';
// Local audio sandbox bridge: zero dependencies, listens on loopback only.
//   GET /api/status   -> { ok, name, version }
//   GET /api/drivers  -> native driver/device detection for this OS
//   GET /api/volume   -> system output / input volume + mute
//   WS  /ws/output    -> page streams Int16 PCM out through PortAudio (ASIO / WASAPI)
//   WS  /ws/input     -> bridge streams Int16 PCM captured from an ASIO / WASAPI input
//   GET /*            -> serves the mixer page from the repo root (same-origin use)
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const { detect } = require('./detect');
const { accept } = require('./ws');
const { createSession } = require('./output');
const { createInputSession } = require('./input');
const { readVolume } = require('./volume');

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
  if (req.method === 'OPTIONS') return send(res, 204, '', { ...cors, 'Access-Control-Allow-Methods': 'GET', 'Access-Control-Allow-Headers': 'Content-Type' });
  if (req.method !== 'GET') return json(res, 405, { ok: false, error: 'method not allowed' }, cors);

  const url = new URL(req.url, `http://${HOST}`);
  if (url.pathname === '/api/status') return json(res, 200, { ok: true, name: 'audio-mixer-bridge', version: '1.0.0' }, cors);
  if (url.pathname === '/api/drivers') {
    try { return json(res, 200, { ok: true, ...(await detect()) }, cors); }
    catch (e) { return json(res, 500, { ok: false, error: e.message }, cors); }
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

if (require.main === module) {
  server.listen(PORT, HOST, () => console.log(`Audio Mixer bridge running: http://localhost:${PORT}  (open this URL to use the mixer)`));
}
module.exports = { server, originAllowed, hostAllowed, HOST };
