'use strict';
// Network control for the hybrid mixer. No packages.
//  - listens on 127.0.0.1 only; anything from another machine comes through nginx (TLS) or an ngrok tunnel
//  - a six-digit pairing PIN, new every minute and after every use, shown on this machine only
//  - a PIN buys a session bound to the caller's address, ten minutes after its last use
//  - wrong PINs are counted per address and in total, then refused for a minute
const http = require('http');
const crypto = require('crypto');
const fs = require('fs');
const os = require('os');
const path = require('path');

const PIN_MS = 60_000, SESSION_MS = 10 * 60_000, MAX_SESSIONS = 8, MAX_BODY = 4096;
const WRONG_PER_ADDRESS = 5, WRONG_TOTAL = 30, WINDOW_MS = 60_000;
const PUBLIC = path.join(__dirname, 'public');
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8' };
const HEADERS = {
  'X-Content-Type-Options': 'nosniff', 'X-Frame-Options': 'DENY', 'Referrer-Policy': 'no-referrer', 'Cache-Control': 'no-store',
  'Content-Security-Policy': "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self' data:; base-uri 'none'; form-action 'none'; frame-ancestors 'none'",
};

function loadAddon() {
  for (const p of [process.env.AMFB_ADDON, './build/Release/amfb_mixer.node', './amfb_mixer.node']) {
    if (!p) continue;
    try { return require(path.resolve(__dirname, p)); } catch (_) { /* try the next place */ }
  }
  return null;
}

function start(opts = {}) {
  const port = opts.port ?? Number(process.env.AMFB_PORT || 8377);
  const mixer = opts.mixer || loadAddon();
  if (!mixer) throw new Error('the add-on is not built: run "npm run build" in hybrid/node');
  const pinFile = opts.pinFile || path.join(os.homedir(), '.amfb-hybrid', 'pin');
  const now = opts.now || Date.now;
  const log = opts.log || console.log;
  // extra host names this server answers to, e.g. the nginx name or the ngrok address
  const hosts = new Set(['127.0.0.1', 'localhost', '[::1]', ...String(opts.hosts ?? process.env.AMFB_HOSTS ?? '').split(',').map(s => s.trim().toLowerCase()).filter(Boolean)]);

  let pin = '', pinBorn = 0, lastBeat = -1, lastBeatAt = 0, alive = false;
  const sessions = new Map();           // sha256(token) -> { ip, used }
  const wrong = new Map();              // ip -> [times]
  let wrongAll = [];

  function newPin() {
    pin = String(crypto.randomInt(0, 1_000_000)).padStart(6, '0');
    pinBorn = now();
    try {
      fs.mkdirSync(path.dirname(pinFile), { recursive: true, mode: 0o700 });
      fs.writeFileSync(pinFile, pin + '\n', { mode: 0o600 });
    } catch (e) { log('cannot write the PIN file: ' + e.message); }
    if (opts.showPin !== false) log(`pairing PIN ${pin} (valid ${PIN_MS / 1000} s, one use)`);
  }
  const currentPin = () => { if (now() - pinBorn >= PIN_MS) newPin(); return pin; };
  newPin();
  const pinTimer = setInterval(currentPin, 5000); pinTimer.unref();

  const hash = t => crypto.createHash('sha256').update(t).digest('hex');
  const recent = list => list.filter(t => now() - t < WINDOW_MS);
  function sweep() {
    for (const [k, s] of sessions) if (now() - s.used > SESSION_MS) sessions.delete(k);
    for (const [k, l] of wrong) { const r = recent(l); if (r.length) wrong.set(k, r); else wrong.delete(k); }
    wrongAll = recent(wrongAll);
  }

  // The peer is always this machine (we listen on loopback). A proxy on this machine names the real caller.
  function callerOf(req) {
    const fwd = String(req.headers['x-forwarded-for'] || '').split(',').map(s => s.trim()).filter(Boolean);
    const ip = fwd.length ? fwd[fwd.length - 1] : req.socket.remoteAddress || '';
    return ip.replace(/^::ffff:/, '').slice(0, 64);
  }
  function sessionOf(req, ip) {
    const m = /^Bearer ([A-Za-z0-9_-]{43})$/.exec(req.headers.authorization || '');
    if (!m) return null;
    const s = sessions.get(hash(m[1]));
    if (!s || s.ip !== ip || now() - s.used > SESSION_MS) return null;
    s.used = now();
    return s;
  }
  function engineAlive() {
    try {
      if (!mixer.open()) { alive = false; return false; }
      const beat = mixer.state().heartbeat;
      if (beat !== lastBeat) { lastBeat = beat; lastBeatAt = now(); alive = true; }
      else if (now() - lastBeatAt > 1500) { alive = false; mixer.close(); }   // the engine stopped: let go, attach to the next one
      return alive;
    } catch (_) { alive = false; return false; }
  }

  function send(res, code, body, extra = {}) {
    const data = typeof body === 'string' || Buffer.isBuffer(body) ? body : JSON.stringify(body);
    res.writeHead(code, { 'Content-Type': 'application/json; charset=utf-8', ...HEADERS, ...extra, 'Content-Length': Buffer.byteLength(data) });
    res.end(data);
  }
  function readJson(req) {
    return new Promise((resolve, reject) => {
      if (!/^application\/json\b/i.test(req.headers['content-type'] || '')) return reject(Object.assign(new Error('JSON only'), { code: 415 }));
      let size = 0; const parts = [];
      req.on('data', c => { size += c.length; if (size > MAX_BODY) { reject(Object.assign(new Error('too large'), { code: 413 })); req.destroy(); } else parts.push(c); });
      req.on('end', () => { try { const v = JSON.parse(Buffer.concat(parts).toString('utf8')); if (!v || typeof v !== 'object' || Array.isArray(v)) throw 0; resolve(v); } catch (_) { reject(Object.assign(new Error('bad JSON'), { code: 400 })); } });
      req.on('error', reject);
    });
  }

  async function handle(req, res) {
    sweep();
    const host = String(req.headers.host || '').toLowerCase().replace(/:\d+$/, '');
    if (!hosts.has(host)) return send(res, 421, { error: 'unknown host' });          // DNS rebinding stops here
    const url = new URL(req.url, 'http://x');
    const ip = callerOf(req);
    const write = req.method === 'POST';
    if (write) {                                                                     // a page of another site cannot post here
      const origin = req.headers.origin;
      if (origin) { let oh = ''; try { oh = new URL(origin).hostname.toLowerCase(); } catch (_) {} if (oh !== host.replace(/^\[|\]$/g, '') && '[' + oh + ']' !== host) return send(res, 403, { error: 'cross-site request refused' }); }
    } else if (req.method !== 'GET') return send(res, 405, { error: 'method not allowed' }, { Allow: 'GET, POST' });

    if (!url.pathname.startsWith('/api/')) {
      if (write) return send(res, 405, { error: 'method not allowed' });
      const name = url.pathname === '/' ? 'index.html' : url.pathname.slice(1);
      if (!/^[a-z0-9.-]+$/.test(name) || !TYPES[path.extname(name)]) return send(res, 404, { error: 'not found' });
      return fs.readFile(path.join(PUBLIC, name), (e, data) => e ? send(res, 404, { error: 'not found' }) : send(res, 200, data, { 'Content-Type': TYPES[path.extname(name)] }));
    }

    if (url.pathname === '/api/pair' && write) {
      const mine = recent(wrong.get(ip) || []);
      if (mine.length >= WRONG_PER_ADDRESS || wrongAll.length >= WRONG_TOTAL) return send(res, 429, { error: 'too many wrong PINs, wait a minute' }, { 'Retry-After': '60' });
      const body = await readJson(req);
      const given = Buffer.from(String(body.pin ?? '').padEnd(6).slice(0, 6)), want = Buffer.from(currentPin());
      if (!/^\d{6}$/.test(String(body.pin ?? '')) || !crypto.timingSafeEqual(given, want)) {
        mine.push(now()); wrong.set(ip, mine); wrongAll.push(now());
        return send(res, 401, { error: 'wrong PIN' });
      }
      newPin();                                                                      // one use
      if (sessions.size >= MAX_SESSIONS) sessions.delete([...sessions].sort((a, b) => a[1].used - b[1].used)[0][0]);
      const token = crypto.randomBytes(32).toString('base64url');
      sessions.set(hash(token), { ip, used: now() });
      return send(res, 200, { token, expiresInSeconds: SESSION_MS / 1000 });
    }

    if (!sessionOf(req, ip)) return send(res, 401, { error: 'pair first' });
    if (url.pathname === '/api/logout' && write) { sessions.delete(hash(req.headers.authorization.slice(7))); return send(res, 200, { ok: true }); }
    if (!engineAlive()) return send(res, 503, { error: 'the mixer engine is not running' });

    if (!write) {
      if (url.pathname === '/api/state') return send(res, 200, { ...mixer.state(), alive: true });
      if (url.pathname === '/api/meters') {
        const m = mixer.meters(), out = [];
        for (let i = 0; i < m.length; i += 9) out.push(Array.from(m.subarray(i, i + 9), v => Math.round(v * 1e5) / 1e5));
        return send(res, 200, { meters: out });
      }
      return send(res, 404, { error: 'not found' });
    }
    const body = await readJson(req);
    let m;
    try {
      if ((m = /^\/api\/strip\/(\d{1,2})$/.exec(url.pathname))) mixer.setStrip(Number(m[1]) - 1, body);
      else if ((m = /^\/api\/eq\/(\d{1,2})\/(\d)$/.exec(url.pathname))) mixer.setEq(Number(m[1]) - 1, Number(m[2]) - 1, body);
      else if (url.pathname === '/api/master') mixer.setMaster(body.gainDb);
      else return send(res, 404, { error: 'not found' });
    } catch (e) { return send(res, 400, { error: String(e.message || e) }); }
    return send(res, 200, { ok: true });
  }

  const server = http.createServer((req, res) => {
    handle(req, res).catch(e => { if (!res.headersSent) send(res, e.code || 500, { error: e.code ? e.message : 'internal error' }); });
  });
  server.headersTimeout = 10_000; server.requestTimeout = 15_000; server.keepAliveTimeout = 5_000; server.maxHeadersCount = 40;
  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(port, '127.0.0.1', () => {                                         // never any other interface
      log(`hybrid control on http://127.0.0.1:${server.address().port} (this machine only)`);
      resolve({ server, port: server.address().port, pinFile, currentPin, close: () => new Promise(r => { clearInterval(pinTimer); try { fs.unlinkSync(pinFile); } catch (_) {} server.close(r); server.closeAllConnections?.(); }) });
    });
  });
}

module.exports = { start };
if (require.main === module) start().catch(e => { console.error('error: ' + e.message); process.exit(1); });
