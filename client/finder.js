#!/usr/bin/env node
'use strict';
// Audio Mixer finder: a tiny start page that finds the link of a running Audio Mixer server and opens it. No dependencies, built to cost almost nothing while it sits open.
//   node client/finder.js [--port 8780] [--no-open] [--idle 20] [--ports 8765-8774] [--lan]
// What it does: asks every candidate address for GET /api/status (the mixer answers { name: "audio-mixer-bridge", version }), lists the ones that answer as links
// (Open / Copy), can scan the local network on request (private /24 networks of this computer only) and can start the mixer server on this computer.
// Why it is cheap: the page is one static file (no framework, no images, no fonts, no animation, no CSS effects: nothing for the GPU to composite), it asks the finder every 5 s
// only while its tab is visible, the finder holds no timer except one idle check, it probes on request only (cached for 3 s, at most 32 probes at once, 400 ms each), runs at
// below-normal priority and ends by itself after --idle minutes without a request (0 = never).
// Safety: listens on 127.0.0.1 only; the Host header must be 127.0.0.1 / localhost; the page has a locked-down CSP; "start" needs a custom header (so a foreign page cannot
// trigger it); the network scan never leaves private address ranges and never goes beyond a /24 per network card.
const http = require('node:http');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawn } = require('node:child_process');

const PAGE = `<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Audio Mixer finder</title>
<style>:root{color-scheme:light dark}body{font:15px/1.4 system-ui,sans-serif;margin:0 auto;max-width:640px;padding:16px}h1{font-size:20px;margin:0 0 4px}p{margin:4px 0 12px;opacity:.8}
ul{list-style:none;padding:0;margin:0 0 12px}li{border:1px solid #8886;border-radius:6px;padding:8px 10px;margin:0 0 8px;display:flex;gap:8px;align-items:center;flex-wrap:wrap}
li b{flex:1 1 220px;word-break:break-all}button,a.b{font:inherit;padding:5px 10px;border:1px solid #8888;border-radius:6px;background:Canvas;color:CanvasText;text-decoration:none;cursor:pointer}
#st{min-height:1.4em}</style></head><body><h1>Audio Mixer finder</h1><p id="st">Looking for a running Audio Mixer...</p><ul id="ls"></ul>
<button id="sc">Scan this network</button> <button id="go">Start the mixer on this computer</button>
<script>"use strict";
var st=document.getElementById("st"),ls=document.getElementById("ls"),timer=0,lan=false,busy=false;
function row(m){var li=document.createElement("li"),b=document.createElement("b"),o=document.createElement("a"),c=document.createElement("button");
b.textContent=m.url+"  (v"+m.version+(m.local?", this computer":"")+")";o.className="b";o.textContent="Open";o.href=m.url;o.target="_blank";o.rel="noopener";
c.textContent="Copy link";c.onclick=function(){if(navigator.clipboard)navigator.clipboard.writeText(m.url).then(function(){c.textContent="Copied"})};li.append(b,o,c);return li}
function show(r){ls.replaceChildren.apply(ls,r.found.map(row));st.textContent=r.found.length?r.found.length+" mixer"+(r.found.length>1?"s":"")+" found":(lan?"Nothing found on this network.":"No Audio Mixer is running here. Start it with the button below.")}
function scan(){if(busy||document.hidden)return;busy=true;fetch("/api/scan"+(lan?"?lan=1":"")).then(function(r){return r.json()}).then(show).catch(function(){st.textContent="The finder stopped; run it again."}).then(function(){busy=false})}
document.getElementById("sc").onclick=function(){lan=true;st.textContent="Scanning the local network...";scan()};
document.getElementById("go").onclick=function(){st.textContent="Starting the mixer...";fetch("/api/start",{method:"POST",headers:{"X-Finder":"1"}}).then(function(r){return r.json()}).then(function(r){st.textContent=r.message;setTimeout(scan,1500)})};
function tick(){clearInterval(timer);if(!document.hidden){scan();timer=setInterval(scan,5000)}}
document.addEventListener("visibilitychange",tick);tick();
</script></body></html>`;

const sha = s => "'sha256-" + crypto.createHash('sha256').update(s, 'utf8').digest('base64') + "'";
const between = (s, a, b) => s.slice(s.indexOf(a) + a.length, s.indexOf(b, s.indexOf(a)));
const CSP = `default-src 'none'; script-src ${sha(between(PAGE, '<script>', '</script>'))}; style-src ${sha(between(PAGE, '<style>', '</style>'))}; connect-src 'self'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'`;

function parsePorts(spec) {
  const out = new Set();
  for (const part of String(spec).split(',')) {
    const m = /^(\d+)(?:-(\d+))?$/.exec(part.trim()); if (!m) continue;
    const a = Number(m[1]), b = Number(m[2] || m[1]);
    for (let p = a; p <= Math.min(b, a + 63); p++) if (p >= 1 && p <= 65535) out.add(p);
  }
  return [...out];
}
const isPrivate = ip => /^(10\.|192\.168\.|172\.(1[6-9]|2\d|3[01])\.)/.test(ip);

// does an Audio Mixer server answer at host:port?
function probe(host, port, timeoutMs = 400) {
  return new Promise(resolve => {
    const req = http.get({ host, port, path: '/api/status', timeout: timeoutMs, agent: false, headers: { Connection: 'close' } }, res => {
      let body = ''; res.setEncoding('utf8');
      res.on('data', d => { if (body.length < 2048) body += d; });
      res.on('end', () => { try { const j = JSON.parse(body); resolve(j && j.ok && j.name === 'audio-mixer-bridge' ? { version: String(j.version || '') } : null); } catch (_) { resolve(null); } });
    });
    req.on('timeout', () => { req.destroy(); resolve(null); });
    req.on('error', () => resolve(null));
  });
}
// run fn over items, at most `limit` at once
async function pool(items, limit, fn) {
  let i = 0; const out = [];
  await Promise.all(Array.from({ length: Math.min(limit, items.length) }, async () => { while (i < items.length) { const x = items[i++]; const r = await fn(x); if (r) out.push(r); } }));
  return out;
}
// the /24 neighbours of this computer's private IPv4 addresses (the addresses of this computer itself are probed as "local")
function lanHosts() {
  const own = new Set(), others = new Set();
  for (const list of Object.values(os.networkInterfaces())) for (const n of list || []) {
    if (n.family !== 'IPv4' || n.internal || !isPrivate(n.address)) continue;
    own.add(n.address); const base = n.address.split('.').slice(0, 3).join('.');
    for (let h = 1; h <= 254; h++) others.add(base + '.' + h);
  }
  for (const a of own) others.delete(a);
  return { own: [...own], others: [...others] };
}

function createFinder({ ports = parsePorts('8765-8774'), probeFn = probe, lan = lanHosts, startMixer = null, cacheMs = 3000 } = {}) {
  const cache = new Map();   // key (lan or not) -> { at, result }
  async function scan(withLan) {
    const key = withLan ? 'lan' : 'local', hit = cache.get(key);
    if (hit && Date.now() - hit.at < cacheMs) return hit.result;
    const found = [];
    const local = await pool(ports, 8, async p => { const r = await probeFn('127.0.0.1', p); return r && { url: `http://127.0.0.1:${p}/`, host: '127.0.0.1', port: p, version: r.version, local: true }; });
    found.push(...local.sort((a, b) => a.port - b.port));
    if (withLan) {
      const { others } = lan(), targets = [];
      for (const h of others) for (const p of ports.slice(0, 2)) targets.push([h, p]);       // the first two ports per neighbour: a /24 costs at most 508 short probes
      const remote = await pool(targets, 32, async ([h, p]) => { const r = await probeFn(h, p); return r && { url: `http://${h}:${p}/`, host: h, port: p, version: r.version, local: false }; });
      found.push(...remote.sort((a, b) => a.host.localeCompare(b.host, 'en', { numeric: true }) || a.port - b.port));
    }
    const result = { ok: true, found, scannedAt: new Date().toISOString() };
    cache.set(key, { at: Date.now(), result }); return result;
  }
  const hostOk = h => /^(127\.0\.0\.1|localhost)(:\d+)?$/i.test(String(h || ''));
  let last = Date.now();
  const server = http.createServer(async (req, res) => {
    last = Date.now();
    const send = (code, type, body, extra = {}) => { res.writeHead(code, { 'Content-Type': type, 'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff', 'Referrer-Policy': 'no-referrer', ...extra }); res.end(body); };
    const json = (code, o) => send(code, 'application/json; charset=utf-8', JSON.stringify(o));
    if (!hostOk(req.headers.host)) return json(403, { ok: false, message: 'forbidden' });
    const url = new URL(req.url, 'http://127.0.0.1');
    if (req.method === 'GET' && url.pathname === '/') return send(200, 'text/html; charset=utf-8', PAGE, { 'Content-Security-Policy': CSP });
    if (req.method === 'GET' && url.pathname === '/api/scan') return json(200, await scan(url.searchParams.get('lan') === '1'));
    if (req.method === 'POST' && url.pathname === '/api/start') {
      if (req.headers['x-finder'] !== '1') return json(403, { ok: false, message: 'forbidden' });
      if (!startMixer) return json(200, { ok: false, message: 'Starting the mixer is not available here.' });
      cache.clear(); return json(200, await startMixer());
    }
    return json(404, { ok: false, message: 'not found' });
  });
  server.maxHeadersCount = 40; server.keepAliveTimeout = 1000; server.headersTimeout = 5000; server.requestTimeout = 10000;
  return { server, scan, idleMs: () => Date.now() - last };
}

// start bin/server of this installation at below-normal priority (Windows: no console window), unless one already answers on the port
function mixerStarter(port) {
  return async () => {
    if (await probe('127.0.0.1', port)) return { ok: true, message: `The mixer is already running on port ${port}.` };
    const script = path.resolve(__dirname, '..', 'bridge', 'server.js');
    const child = spawn(process.execPath, [script], { cwd: path.dirname(script), env: { ...process.env, BRIDGE_PORT: String(port) }, detached: true, stdio: 'ignore', windowsHide: true });
    child.on('error', () => {}); child.unref();
    try { os.setPriority(child.pid, os.constants.priority.PRIORITY_BELOW_NORMAL); } catch (_) { /* not allowed: keep the normal priority */ }
    return { ok: true, message: `Starting the mixer on port ${port}...` };
  };
}

if (require.main === module) {
  const a = process.argv.slice(2), arg = (n, d) => { const i = a.indexOf(n); return i >= 0 ? a[i + 1] : d; };
  const port = Number(arg('--port', 8780)), idleMin = Number(arg('--idle', 20)), mixerPort = Number(process.env.BRIDGE_PORT) || 8765;
  try { os.setPriority(0, os.constants.priority.PRIORITY_BELOW_NORMAL); } catch (_) { /* keep the normal priority */ }
  const f = createFinder({ ports: parsePorts(arg('--ports', '8765-8774')), startMixer: mixerStarter(mixerPort) });
  f.server.on('error', e => { console.error(e.code === 'EADDRINUSE' ? `Port ${port} is in use: another finder is probably open (try --port).` : e.message); process.exit(1); });
  f.server.listen(port, '127.0.0.1', () => {
    const link = `http://127.0.0.1:${f.server.address().port}/`; console.log(`Audio Mixer finder: ${link}${idleMin > 0 ? ` (closes by itself after ${idleMin} min without use)` : ''}`);
    if (!a.includes('--no-open')) {
      const [cmd, args] = process.platform === 'win32' ? ['cmd', ['/c', 'start', '', link]] : process.platform === 'darwin' ? ['open', [link]] : ['xdg-open', [link]];
      try { const c = spawn(cmd, args, { stdio: 'ignore', detached: true, windowsHide: true }); c.on('error', () => {}); c.unref(); } catch (_) { /* open the link by hand */ }
    }
    if (idleMin > 0) setInterval(() => { if (f.idleMs() > idleMin * 60000) { console.log('Closing: no use for ' + idleMin + ' minutes.'); process.exit(0); } }, 30000).unref();   // the one timer, and it does not keep the process alive by itself
  });
}
module.exports = { createFinder, probe, parsePorts, lanHosts, isPrivate, PAGE, CSP };
