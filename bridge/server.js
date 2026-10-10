#!/usr/bin/env node
'use strict';
// Local audio sandbox bridge: zero dependencies, listens on loopback only.
//   GET /api/status   -> { ok, name, version }
//   GET /api/drivers  -> native driver/device detection for this OS
//   GET /api/volume   -> system output / input volume + mute
//   GET /api/license, POST /api/license/activate|deactivate -> consumer license (plan, machine code)
//   GET /api/update, POST /api/update/download -> OTA updates (signed manifest, checksum-verified download)
//   GET /api/universal -> universal ASIO driver: all input / output sources, ranked, with the automatic pick
//   GET /api/plugins -> VST3 / VST2 plugins (.vst3 / .dll / .vst) found and validated
//   GET /api/audify, /api/framesize -> Audify (RtAudio) engine devices and automatic frame size
//   WS  /ws/output    -> page streams Int16 PCM out through PortAudio (ASIO / WASAPI)
//   GET /api/inserts, POST /api/inserts (X-Mixer-Action: inserts) -> plugin insert slots of the PHASE and FX pages
//   WS  /ws/insert    -> audio through a plugin of an insert slot (native plugin host, VST2 .dll / .vst)
//   WS  /ws/duplex    -> read AND write one interface through a single native stream
//   WS  /ws/input     -> bridge streams Int16 PCM captured from an ASIO / WASAPI input
//   GET /api/catalog  -> official audio drivers / stacks for this OS, with install detection
//   GET /api/nowplaying -> what Spotify / YouTube / YouTube Music / TIDAL / ... is playing (OS media sessions)
//   GET /api/interfaces -> every audio interface grouped across ASIO / WASAPI / DirectSound / Core Audio / ALSA ...
//   POST /api/catalog/download {id} -> saves the official installer (FlexASIO) to the download folder; never runs it
//   GET /*            -> serves the mixer page from the repo root (same-origin use)
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const { detect, detectPortAudio } = require('./detect');
const { accept } = require('./ws');
const { createSession } = require('./output');
const { createInputSession } = require('./input');
const { createDuplexSession } = require('./duplex');
const { readVolume } = require('./volume');
const { listCatalog, downloadDriver, downloadDir } = require('./catalog');
const { cachedNowPlaying } = require('./nowplaying');
const { groupInterfaces } = require('./interfaces');
const audifyEngine = require('./audify');
const pluginScan = require('./plugins');
const universalDriver = require('./universal');
const license = require('./license');
const updater = require('./update');
const streamRegistry = require('./streams');
const security = require('./security');
const inserts = require('./inserts');
const pluginHost = require('./pluginhost');

const VERSION = (() => { try { return require('../package.json').version; } catch (_) { return '1.0.0'; } })();
const PORT = Number(process.env.BRIDGE_PORT) || 8765;
const HOST = '127.0.0.1';
const ROOT = path.resolve(__dirname, '..');
const EXTRA_ORIGINS = (process.env.BRIDGE_ORIGINS || '').split(',').map(s => s.trim()).filter(Boolean);
const EXTRA_HOSTS = (process.env.BRIDGE_HOSTS || '').split(',').map(s => s.trim().toLowerCase()).filter(Boolean);
const ALLOW_NULL_ORIGIN = process.env.BRIDGE_ALLOW_NULL_ORIGIN !== '0'; // file:// pages send "null"; set 0 to refuse
const SECURITY_HEADERS = security.HEADERS;
const RATE = process.env.BRIDGE_RATE_LIMIT === '0' ? 0 : 1;   // 0 turns the request limits off
const limiter = security.createLimiter();
const MAX_SOCKETS = 24;                                         // open audio WebSockets at the same time
let openSockets = 0;
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
  if (RATE && req.url.startsWith('/api/')) {   // per page origin: reads 900/min, actions (POST) 40/min
    const post = req.method === 'POST', r = limiter.allow((post ? 'p:' : 'g:') + (origin || 'local'), post ? 40 : 900);
    if (!r.ok) return json(res, 429, { ok: false, error: 'too many requests, retry in ' + r.retryAfter + ' s' }, { 'Retry-After': String(r.retryAfter) });
  }
  const cors = origin ? { 'Access-Control-Allow-Origin': origin, 'Vary': 'Origin', 'Access-Control-Allow-Private-Network': 'true' } : {};
  if (req.method === 'OPTIONS') return send(res, 204, '', { ...cors, 'Access-Control-Allow-Methods': 'GET, POST', 'Access-Control-Allow-Headers': 'Content-Type, X-Mixer-Action' });
  const url = new URL(req.url, `http://${HOST}`);
  if (req.method === 'POST' && url.pathname === '/api/catalog/download') return handleDownload(req, res, cors);
  if (req.method === 'POST' && url.pathname === '/api/license/activate') return handleAction(req, res, cors, 'license', async body => {
    const r = license.activate(body.key);
    return r.ok ? [200, r] : [400, r];
  });
  if (req.method === 'POST' && url.pathname === '/api/license/deactivate') return handleAction(req, res, cors, 'license', async () => [200, license.deactivate()], false);
  if (req.method === 'POST' && url.pathname === '/api/update/download') return handleAction(req, res, cors, 'update', async () => {
    if (!license.hasFeature(license.status(), 'ota')) return [402, { ok: false, needs: 'ota', error: 'Downloading updates needs the PRO or STUDIO plan. You can still check for updates.' }];
    return [200, await updater.download({ current: VERSION })];
  }, false);
  if (req.method === 'POST' && url.pathname === '/api/inserts') return handleAction(req, res, cors, 'inserts', async body => {
    if (!license.hasFeature(license.status(), 'plugins')) return [402, { ok: false, error: 'Plugin inserts need the PRO or STUDIO plan', needs: 'plugins' }];
    return [200, { ok: true, ...inserts.set(body) }];
  });
  if (req.method !== 'GET') return json(res, 405, { ok: false, error: 'method not allowed' }, cors);
  if (url.pathname === '/api/status') return json(res, 200, { ok: true, name: 'audio-mixer-bridge', version: VERSION, node: process.version, pid: process.pid, uptimeSec: Math.round(process.uptime()), streams: streamRegistry.list(), time: Date.now() }, cors);
  // Consumer licensing: the plan this copy runs as, this computer's machine code, and the stored key's state
  if (url.pathname === '/api/inserts') return json(res, 200, { ok: true, host: { available: !!pluginHost.hostPath(), formats: ['VST2'] }, ...inserts.read() }, cors);
  if (url.pathname === '/api/license') return json(res, 200, { ok: true, version: VERSION, ...license.status() }, cors);

  // OTA updates: is a newer, signed version published? (cached for 5 minutes, ?force=1 asks again)
  if (url.pathname === '/api/update') {
    try {
      const force = url.searchParams.get('force') === '1';
      if (force || !updateCache || Date.now() - updateCache.at > 300000) updateCache = { at: Date.now(), data: await updater.check({ current: VERSION }) };
      return json(res, 200, updateCache.data, cors);
    } catch (e) { return json(res, e.status || 502, { ok: false, current: VERSION, error: e.message }, cors); }
  }

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
      const src = url.searchParams.get('engine') === 'audify' && info.audify ? info.audify : info.portaudio;
      const devices = src ? src.devices : (info.native ? info.native.devices : []);
      return json(res, 200, { ok: true, platform: info.platform, portaudio: !!info.portaudio, engine: src ? src.engine : (info.native ? info.native.engine : null), asio: info.asio, interfaces: groupInterfaces(devices, info.asio) }, cors);
    } catch (e) { return json(res, 500, { ok: false, error: e.message }, cors); }
  }

  // Audify (RtAudio) engine: every compiled host API with its devices and the automatic frame size for each.
  if (url.pathname === '/api/audify') {
    try { return json(res, 200, { ok: true, ...audifyEngine.describe() }, cors); }
    catch (e) { return json(res, 500, { ok: false, error: e.message }, cors); }
  }

  // Universal ASIO driver: every input and output source on this PC (PortAudio + Audify, all host APIs), best first, with the automatic pick
  if (url.pathname === '/api/universal') {
    try {
      const pa = detectPortAudio(), au = audifyEngine.detectAudify();
      const lists = [];
      if (pa) lists.push({ engine: 'naudiodon', devices: pa.devices });
      if (au) lists.push({ engine: 'audify', devices: au.devices });
      return json(res, 200, { ok: true, engines: { naudiodon: !!pa, audify: !!au }, ...universalDriver.detectSources(lists), time: Date.now() }, cors);
    } catch (e) { return json(res, 500, { ok: false, error: e.message }, cors); }
  }

  // Plugin system: VST3 / VST2 (.vst3, .dll, .vst) in the standard folders and the app's own plugin folder, each binary checked.
  if (url.pathname === '/api/plugins') {
    try { return json(res, 200, { ok: true, ...pluginScan.scan({ hash: url.searchParams.get('hash') === '1' }) }, cors); }
    catch (e) { return json(res, 500, { ok: false, error: e.message }, cors); }
  }

  // Automatic ASIO / host-API buffer size: /api/framesize?api=ASIO&sampleRate=48000&channels=2[&latency=ms]
  if (url.pathname === '/api/framesize') {
    const q = url.searchParams, num = (k, d) => (q.has(k) ? Number(q.get(k)) : d);
    const sampleRate = num('sampleRate', 48000), channels = num('channels', 2), latencyMs = q.has('latency') ? Number(q.get('latency')) : undefined;
    if (!(sampleRate >= 8000 && sampleRate <= 384000) || !(Number.isInteger(channels) && channels >= 1 && channels <= 128) || (latencyMs !== undefined && !(latencyMs > 0 && latencyMs <= 1000))) {
      return json(res, 400, { ok: false, error: 'sampleRate (8000-384000), channels (1-128) or latency (ms) out of range' }, cors);
    }
    const api = q.get('api') || 'ASIO';
    if (!audifyEngine.apiKey(api)) return json(res, 400, { ok: false, error: 'unknown api: use ASIO, WASAPI, DirectSound, CoreAudio, JACK, ALSA, Pulse or OSS' }, cors);
    const p = audifyEngine.plan('auto', { api, sampleRate, channels, latencyMs });
    return json(res, 200, { ok: true, ...p.recommended, candidates: p.candidates, min: audifyEngine.MIN_FRAMES, max: audifyEngine.MAX_FRAMES }, cors);
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
  if (rel === '/') rel = '/index.html';
  const file = path.resolve(ROOT, '.' + rel);
  if (file !== ROOT && !file.startsWith(ROOT + path.sep)) return json(res, 403, { ok: false, error: 'forbidden' });
  // Never serve dotfiles/dirs (.git, .github, .vscode, .env ...) or the bridge's own sources.
  if (path.relative(ROOT, file).split(path.sep).some(p => p.startsWith('.')) || file.startsWith(__dirname + path.sep) || !security.staticAllowed(ROOT, file)) return json(res, 404, { ok: false, error: 'not found' });
  fs.readFile(file, (err, buf) => {
    if (err) return json(res, 404, { ok: false, error: 'not found' });
    const type = TYPES[path.extname(file).toLowerCase()] || 'application/octet-stream';
    send(res, 200, buf, { 'Content-Type': type });
  });
}

let updateCache = null;
// JSON POST actions (license, updates): the custom header forces a CORS preflight, so foreign pages cannot trigger them.
async function handleAction(req, res, cors, action, fn, wantBody = true) {
  if (req.headers['x-mixer-action'] !== action) return json(res, 400, { ok: false, error: 'missing X-Mixer-Action: ' + action }, cors);
  let body = {};
  if (wantBody) {
    if (!String(req.headers['content-type'] || '').startsWith('application/json')) return json(res, 400, { ok: false, error: 'JSON body required' }, cors);
    let raw = '';
    for await (const chunk of req) { raw += chunk; if (raw.length > 8192) return json(res, 413, { ok: false, error: 'request too large' }, cors); }
    try { body = JSON.parse(raw || '{}'); } catch (_) { return json(res, 400, { ok: false, error: 'invalid JSON' }, cors); }
  } else { for await (const _ of req) { /* drain */ } }
  try { const [code, data] = await fn(body); return json(res, code, data, cors); }
  catch (e) { return json(res, e.status || 500, { ok: false, error: e.message }, cors); }
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
  if ((pathname !== '/ws/output' && pathname !== '/ws/input' && pathname !== '/ws/duplex' && pathname !== '/ws/insert') || !hostAllowed(req.headers.host) || !originAllowed(req.headers.origin)) { socket.destroy(); return; }
  if (openSockets >= MAX_SOCKETS || (RATE && !limiter.allow('ws', 120).ok)) { socket.end('HTTP/1.1 429 Too Many Requests\r\nConnection: close\r\n\r\n'); return; }
  const handlers = {};
  const conn = accept(req, socket, handlers);
  if (!conn) return;
  openSockets++; socket.once('close', () => { openSockets--; });
  Object.assign(handlers, pathname === '/ws/input' ? createInputSession(conn) : pathname === '/ws/duplex' ? createDuplexSession(conn) : pathname === '/ws/insert' ? pluginHost.createInsertSession(conn) : createSession(conn));
});

server.headersTimeout = 15000; server.requestTimeout = 60000; server.maxHeadersCount = 64; server.keepAliveTimeout = 5000;
server.on('clientError', (_, socket) => { try { socket.end('HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n'); } catch (_) { /* gone */ } });

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
