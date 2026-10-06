#!/usr/bin/env node
'use strict';
// Local audio sandbox bridge: zero dependencies, listens on loopback only.
//   GET /api/status   -> { ok, name, version }
//   GET /api/drivers  -> native driver/device detection for this OS
//   GET /*            -> serves the mixer page from the repo root (same-origin use)
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const { detect } = require('./detect');

const PORT = Number(process.env.BRIDGE_PORT) || 8765;
const HOST = '127.0.0.1';
const ROOT = path.resolve(__dirname, '..');
const EXTRA_ORIGINS = (process.env.BRIDGE_ORIGINS || '').split(',').map(s => s.trim()).filter(Boolean);
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.css': 'text/css', '.json': 'application/json', '.png': 'image/png', '.svg': 'image/svg+xml' };

// Only local pages (and file:// pages, which send "null") may read the API.
function originAllowed(o) {
  if (!o) return true;
  if (o === 'null' || EXTRA_ORIGINS.includes(o)) return true;
  try { const h = new URL(o).hostname; return h === 'localhost' || h === '127.0.0.1' || h === '[::1]'; } catch (_) { return false; }
}

function send(res, code, body, headers = {}) {
  res.writeHead(code, { 'Cache-Control': 'no-store', ...headers });
  res.end(body);
}
const json = (res, code, obj, h) => send(res, code, JSON.stringify(obj), { 'Content-Type': 'application/json', ...h });

const server = http.createServer(async (req, res) => {
  const origin = req.headers.origin;
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

  // Static page (path-traversal safe)
  let rel = decodeURIComponent(url.pathname);
  if (rel === '/') rel = '/gemini-code-1790419147591.html';
  const file = path.resolve(ROOT, '.' + rel);
  if (file !== ROOT && !file.startsWith(ROOT + path.sep)) return json(res, 403, { ok: false, error: 'forbidden' });
  if (file.startsWith(path.join(ROOT, '.git')) || file.startsWith(__dirname + path.sep)) return json(res, 404, { ok: false, error: 'not found' });
  fs.readFile(file, (err, buf) => {
    if (err) return json(res, 404, { ok: false, error: 'not found' });
    send(res, 200, buf, { 'Content-Type': TYPES[path.extname(file)] || 'application/octet-stream' });
  });
});

if (require.main === module) {
  server.listen(PORT, HOST, () => console.log(`Audio Mixer bridge running: http://localhost:${PORT}  (open this URL to use the mixer)`));
}
module.exports = { server, originAllowed };
