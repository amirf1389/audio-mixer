#!/usr/bin/env node
'use strict';
// Audio Mixer OTA server: the vendor-side server that the app's "Check for updates" asks (BRIDGE_UPDATE_URL).
//
//   GET  /update.json                    signed manifest of the stable channel   (GET /<channel>/update.json for other channels, e.g. beta)
//   GET  /releases/<file>                installer / package, HEAD and Range (resume) supported
//   GET  /healthz                        { ok, channels: { stable: "1.4.1.0" } }
//   --- administration, only with OTA_ADMIN_TOKEN set (Authorization: Bearer <token>) ---
//   PUT  /admin/files/<file>             upload a release file; header X-SHA256 (hex) is required and checked; an existing file is not replaced (?overwrite=1)
//   GET  /admin/files                    files held: name, size, sha256, referenced by which manifests
//   DELETE /admin/files/<file>           remove a file that no published manifest lists
//   PUT  /admin/manifest/<channel>       publish a manifest envelope (signed by the vendor, see scripts/ota.js)
//   GET  /admin/stats                    manifest checks and downloads per day
//   GET  /admin/config                   public URL, public key, limits (for the dashboard)
//   GET  /admin/history/<channel>        every manifest ever published on the channel (newest first);  /admin/history/<channel>/<version> the signed envelope
//   DELETE /admin/manifest/<channel>     unpublish the channel (the apps get 404; the history stays, a roll back publishes an old version again)
//   GET  /admin/verify                   hashes every file on disk again and reports damage
//   GET  /admin/  (+ app.js, style.css)  the vendor web dashboard (ota-server/dashboard/): needs OTA_ADMIN_TOKEN, the token is typed into the page and used as the Bearer token
//
// The server never holds the vendor PRIVATE key: manifests are signed on the vendor's machine. A manifest is accepted only when its signature
// verifies with the Audio Mixer public key, every file it lists is here with the same size and SHA-256, its version is newer than the published
// one (?force=1 overrides) and, with OTA_PUBLIC_URL set, its file URLs point at this server. So the app can never be sent to a missing or changed file.
//
//   node ota-server/server.js [--port 8780] [--host 127.0.0.1] [--data ./ota-data] [--seed <releases dir>]
//   OTA_ADMIN_TOKEN        admin token (admin routes answer 404 without it); OTA_PUBLIC_URL  https://ota.example.com
//   OTA_TLS_CERT / OTA_TLS_KEY  serve HTTPS directly (the app only talks to https servers); or run behind nginx / Caddy (deploy/ota)
//   OTA_TRUST_PROXY=1      take the client address from X-Forwarded-For (behind a proxy only)
//   OTA_RATE               requests per minute per address for downloads (default 120); OTA_ALLOW_HTTP=1  accept http file URLs (development)
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const https = require('node:https');
const crypto = require('node:crypto');
const update = require('../bridge/update');
const { createLimiter } = require('../bridge/security');

const MAX_FILE = 400 * 1024 * 1024;                 // the app refuses larger downloads
const NAME = /^[A-Za-z0-9][A-Za-z0-9._ \-]{0,119}$/;
const CHANNEL = /^[a-z0-9][a-z0-9-]{0,19}$/;
const HEX = /^[0-9a-f]{64}$/;

function createOta({ dataDir, token = '', publicUrl = '', jwk, rate = 120, trustProxy = false, allowHttp = false, log = () => {}, now = () => new Date() } = {}) {
  if (!dataDir) throw new Error('dataDir is required');
  const dirs = { releases: path.join(dataDir, 'releases'), manifests: path.join(dataDir, 'manifests'), history: path.join(dataDir, 'manifests', 'history') };
  Object.values(dirs).forEach(d => fs.mkdirSync(d, { recursive: true }));
  const indexFile = path.join(dataDir, 'index.json'), statsFile = path.join(dataDir, 'stats.json');
  const readJson = (f, d) => { try { return JSON.parse(fs.readFileSync(f, 'utf8')); } catch (_) { return d; } };
  const writeAtomic = (f, text) => { const t = f + '.' + process.pid + '.tmp'; fs.writeFileSync(t, text, { mode: 0o640 }); fs.renameSync(t, f); };
  const limiter = createLimiter();
  let index = readJson(indexFile, {}), stats = readJson(statsFile, { days: {} }), statsDirty = false;
  const sha256File = f => new Promise((res, rej) => { const h = crypto.createHash('sha256'); fs.createReadStream(f).on('data', c => h.update(c)).on('end', () => res(h.digest('hex'))).on('error', rej); });

  // the file index (size + SHA-256 of what is on disk); files added by hand are picked up here
  async function reindex() {
    const next = {};
    for (const n of fs.readdirSync(dirs.releases)) {
      if (!NAME.test(n) || n.endsWith('.part')) continue;
      const st = fs.statSync(path.join(dirs.releases, n)); if (!st.isFile()) continue;
      const old = index[n];
      next[n] = old && old.size === st.size && old.mtime === st.mtimeMs ? old : { size: st.size, mtime: st.mtimeMs, sha256: await sha256File(path.join(dirs.releases, n)) };
    }
    index = next; writeAtomic(indexFile, JSON.stringify(index, null, 1));
  }
  const manifestFile = ch => path.join(dirs.manifests, ch + '.json');
  const readEnvelope = ch => readJson(manifestFile(ch), null);
  const channels = () => fs.readdirSync(dirs.manifests).filter(n => n.endsWith('.json')).map(n => n.slice(0, -5)).filter(c => CHANNEL.test(c));
  const versionOf = ch => { const e = readEnvelope(ch); const v = e && update.verifyManifest(e, jwk); return v && v.ok ? v.manifest.version : null; };
  const referencedBy = name => channels().filter(c => { const e = readEnvelope(c), v = e && update.verifyManifest(e, jwk); return v && v.ok && Object.values(v.manifest.files).some(f => f.name === name); });

  const day = () => now().toISOString().slice(0, 10);
  function count(kind, key) { const d = stats.days[day()] || (stats.days[day()] = { manifest: {}, download: {} }); d[kind][key] = (d[kind][key] || 0) + 1; statsDirty = true; }
  const flush = () => { if (statsDirty) { statsDirty = false; writeAtomic(statsFile, JSON.stringify(stats)); } };

  const send = (res, code, body, headers = {}) => { const t = typeof body === 'string' || Buffer.isBuffer(body) ? body : JSON.stringify(body); res.writeHead(code, { 'Content-Type': typeof body === 'object' && !Buffer.isBuffer(body) ? 'application/json; charset=utf-8' : 'text/plain; charset=utf-8', 'X-Content-Type-Options': 'nosniff', 'Cache-Control': 'no-store', ...headers }); res.end(t); };
  const fail = (res, code, error) => send(res, code, { ok: false, error });
  const addr = req => (trustProxy && String(req.headers['x-forwarded-for'] || '').split(',')[0].trim()) || req.socket.remoteAddress || '?';
  const authed = req => { const m = /^Bearer (.+)$/.exec(String(req.headers.authorization || '')); if (!m || !token) return false; const a = crypto.createHash('sha256').update(m[1]).digest(), b = crypto.createHash('sha256').update(token).digest(); return crypto.timingSafeEqual(a, b); };

  async function readBody(req, max) {
    const chunks = []; let n = 0;
    for await (const c of req) { n += c.length; if (n > max) throw Object.assign(new Error('body too large'), { status: 413 }); chunks.push(c); }
    return Buffer.concat(chunks);
  }

  // ── public routes ──
  function serveManifest(req, res, ch) {
    const f = manifestFile(ch);
    if (!fs.existsSync(f)) return fail(res, 404, 'no update is published on the ' + ch + ' channel');
    const body = fs.readFileSync(f), etag = '"' + crypto.createHash('sha256').update(body).digest('hex').slice(0, 32) + '"';
    count('manifest', ch);
    if (req.headers['if-none-match'] === etag) return send(res, 304, '', { ETag: etag, 'Cache-Control': 'no-cache' });
    send(res, 200, req.method === 'HEAD' ? '' : body, { 'Content-Type': 'application/json; charset=utf-8', 'Content-Length': body.length, ETag: etag, 'Cache-Control': 'no-cache' });
  }
  function serveFile(req, res, name) {
    if (!NAME.test(name) || !index[name]) return fail(res, 404, 'no such file');
    const f = path.join(dirs.releases, name), size = index[name].size, etag = '"' + index[name].sha256.slice(0, 32) + '"';
    const base = { 'Content-Type': 'application/octet-stream', 'Content-Disposition': 'attachment; filename="' + name.replace(/"/g, '') + '"', 'Accept-Ranges': 'bytes', ETag: etag, 'Cache-Control': 'public, max-age=3600, immutable', 'X-Content-Type-Options': 'nosniff' };
    let start = 0, end = size - 1, code = 200;
    const rg = req.headers.range && /^bytes=(\d*)-(\d*)$/.exec(String(req.headers.range));
    if (rg && (rg[1] || rg[2])) {
      if (rg[1] === '') { start = Math.max(0, size - Number(rg[2])); } else { start = Number(rg[1]); if (rg[2] !== '') end = Math.min(end, Number(rg[2])); }
      if (start > end || start >= size) return send(res, 416, '', { 'Content-Range': 'bytes */' + size });
      code = 206; base['Content-Range'] = `bytes ${start}-${end}/${size}`;
    }
    base['Content-Length'] = end - start + 1;
    res.writeHead(code, base);
    if (req.method === 'HEAD') return res.end();
    if (start === 0) count('download', name);
    fs.createReadStream(f, { start, end }).on('error', () => res.destroy()).pipe(res);
  }

  // ── administration ──
  async function putFile(req, res, name, query) {
    if (!NAME.test(name) || name.endsWith('.part')) return fail(res, 400, 'bad file name');
    const want = String(req.headers['x-sha256'] || '').toLowerCase();
    if (!HEX.test(want)) return fail(res, 400, 'header X-SHA256 (64 hex characters) is required');
    if (Number(req.headers['content-length'] || 0) > MAX_FILE) return fail(res, 413, 'file too large (limit 400 MB)');
    const target = path.join(dirs.releases, name);
    if (fs.existsSync(target) && query.get('overwrite') !== '1') {
      if (index[name] && index[name].sha256 === want) return send(res, 200, { ok: true, name, size: index[name].size, sha256: want, unchanged: true });
      return fail(res, 409, name + ' exists with other content (use ?overwrite=1; published versions should get a new file name)');
    }
    const part = target + '.part', out = fs.createWriteStream(part, { mode: 0o640 }), h = crypto.createHash('sha256'); let n = 0;
    try {
      for await (const c of req) { n += c.length; if (n > MAX_FILE) throw Object.assign(new Error('file too large'), { status: 413 }); h.update(c); if (!out.write(c)) await new Promise(r => out.once('drain', r)); }
      await new Promise((r, e) => out.end(err => (err ? e(err) : r())));
    } catch (e) { out.destroy(); try { fs.unlinkSync(part); } catch (_) { /* gone */ } return fail(res, e.status || 400, e.message); }
    const got = h.digest('hex');
    if (got !== want) { fs.unlinkSync(part); return fail(res, 400, 'checksum mismatch: the upload is damaged (server computed ' + got + ')'); }
    fs.renameSync(part, target);
    const st = fs.statSync(target); index[name] = { size: st.size, mtime: st.mtimeMs, sha256: got }; writeAtomic(indexFile, JSON.stringify(index, null, 1));
    send(res, 201, { ok: true, name, size: n, sha256: got });
  }
  async function putManifest(req, res, ch, query) {
    if (!CHANNEL.test(ch)) return fail(res, 400, 'bad channel name');
    let env; try { env = JSON.parse((await readBody(req, 200 * 1024)).toString('utf8')); } catch (e) { return fail(res, e.status || 400, e.status ? e.message : 'the body is not JSON'); }
    const v = update.verifyManifest(env, jwk);
    if (!v.ok) return fail(res, 400, 'rejected: ' + v.error);
    const m = v.manifest, problems = [];
    if (m.channel && m.channel !== ch) problems.push('the manifest says channel "' + m.channel + '", published as "' + ch + '"');
    if (!/^\d+(\.\d+){1,3}$/.test(m.version)) problems.push('bad version ' + m.version);
    for (const [key, f] of Object.entries(m.files)) {
      const have = index[f && f.name];
      if (!f || !NAME.test(String(f.name || ''))) { problems.push(key + ': bad file name'); continue; }
      if (!have) problems.push(key + ': ' + f.name + ' has not been uploaded');
      else if (String(f.sha256).toLowerCase() !== have.sha256 || (f.size != null && f.size !== have.size)) problems.push(key + ': ' + f.name + ' differs from the uploaded file (size / SHA-256)');
      let u; try { u = new URL(f.url); } catch (_) { problems.push(key + ': bad url'); continue; }
      if (u.protocol !== 'https:' && !allowHttp) problems.push(key + ': the app only downloads over https');
      if (publicUrl && !f.url.startsWith(publicUrl.replace(/\/+$/, '') + '/releases/')) problems.push(key + ': url is not under ' + publicUrl + '/releases/');
    }
    if (problems.length) return send(res, 400, { ok: false, error: 'rejected', problems });
    const cur = versionOf(ch), stored = readEnvelope(ch), same = !!stored && stored.payload === env.payload;      // signatures differ every time: the signed text decides
    if (same) return send(res, 200, { ok: true, channel: ch, version: m.version, unchanged: true });
    if (cur && update.cmpVersion(m.version, cur) <= 0 && query.get('force') !== '1') return fail(res, 409, 'version ' + m.version + ' is not newer than the published ' + cur + ' (use ?force=1 to replace it)');
    const text = JSON.stringify(env, null, 2) + '\n';
    writeAtomic(path.join(dirs.history, ch + '-' + m.version + '.json'), text); writeAtomic(manifestFile(ch), text);
    log({ event: 'publish', channel: ch, version: m.version, files: Object.keys(m.files) });
    send(res, 200, { ok: true, channel: ch, version: m.version, previous: cur, files: Object.keys(m.files) });
  }

  // ── vendor dashboard (static files; the page itself holds no secret, every API call carries the token) ──
  const DASH = { '/admin/': ['index.html', 'text/html; charset=utf-8'], '/admin/app.js': ['app.js', 'text/javascript; charset=utf-8'], '/admin/style.css': ['style.css', 'text/css; charset=utf-8'] };
  const CSP = "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self' data:; base-uri 'none'; form-action 'none'; frame-ancestors 'none'";
  function serveDashboard(req, res, p) {
    const [file, type] = DASH[p], body = fs.readFileSync(path.join(__dirname, 'dashboard', file));
    send(res, 200, req.method === 'HEAD' ? '' : body, { 'Content-Type': type, 'Content-Length': body.length, 'Content-Security-Policy': CSP, 'X-Frame-Options': 'DENY', 'Referrer-Policy': 'no-referrer', 'Cache-Control': 'no-cache' });
  }
  function history(ch) {
    return fs.readdirSync(dirs.history).filter(n => n.startsWith(ch + '-') && n.endsWith('.json')).map(n => {
      const env = readJson(path.join(dirs.history, n), null), v = env && update.verifyManifest(env, jwk);
      if (!v || !v.ok || v.manifest.channel !== ch && v.manifest.channel) return null;
      const m = v.manifest; return { version: m.version, released: m.released || null, notes: Array.isArray(m.notes) ? m.notes.map(String) : [], files: Object.keys(m.files), current: versionOf(ch) === m.version };
    }).filter(Boolean).sort((a, b) => update.cmpVersion(b.version, a.version));
  }
  async function verifyDisk() {
    const out = [];
    for (const [name, f] of Object.entries(index)) {
      const file = path.join(dirs.releases, name); let actual = null;
      try { actual = fs.statSync(file).size === f.size ? await sha256File(file) : 'size differs'; } catch (_) { actual = 'missing'; }
      out.push({ name, ok: actual === f.sha256, expected: f.sha256, actual });
    }
    return out;
  }

  async function handler(req, res) {
    const t0 = Date.now(), url = new URL(req.url, 'http://x'), p = url.pathname, ip = addr(req);
    res.on('finish', () => log({ ip, method: req.method, path: p, status: res.statusCode, ms: Date.now() - t0 }));
    try {
      if (p === '/admin' && token) return send(res, 301, '', { Location: '/admin/' });
      if (DASH[p] && token && (req.method === 'GET' || req.method === 'HEAD')) return serveDashboard(req, res, p);
      if (p.startsWith('/admin/')) {
        if (!token) return fail(res, 404, 'not found');
        const lim = limiter.allow('a:' + ip, 60); if (!lim.ok) return send(res, 429, { ok: false, error: 'too many requests' }, { 'Retry-After': lim.retryAfter });
        if (!authed(req)) { const bad = limiter.allow('bad:' + ip, 10); return send(res, bad.ok ? 401 : 429, { ok: false, error: 'a valid admin token is required' }, bad.ok ? { 'WWW-Authenticate': 'Bearer' } : { 'Retry-After': bad.retryAfter }); }
        let m;
        if ((m = /^\/admin\/files\/([^/]+)$/.exec(p))) {
          let name; try { name = decodeURIComponent(m[1]); } catch (_) { return fail(res, 400, 'bad file name'); }
          if (req.method === 'PUT') return await putFile(req, res, name, url.searchParams);
          if (req.method === 'DELETE') {
            if (!NAME.test(name) || !index[name]) return fail(res, 404, 'no such file');
            const used = referencedBy(name); if (used.length) return fail(res, 409, name + ' is listed by the ' + used.join(', ') + ' manifest');
            fs.unlinkSync(path.join(dirs.releases, name)); delete index[name]; writeAtomic(indexFile, JSON.stringify(index, null, 1)); return send(res, 200, { ok: true, deleted: name });
          }
        }
        if (p === '/admin/files' && req.method === 'GET') return send(res, 200, { ok: true, files: Object.entries(index).map(([name, f]) => ({ name, size: f.size, sha256: f.sha256, referencedBy: referencedBy(name) })) });
        if ((m = /^\/admin\/manifest\/([^/]+)$/.exec(p)) && req.method === 'PUT') return await putManifest(req, res, m[1], url.searchParams);
        if ((m = /^\/admin\/manifest\/([^/]+)$/.exec(p)) && req.method === 'DELETE') {
          if (!CHANNEL.test(m[1]) || !fs.existsSync(manifestFile(m[1]))) return fail(res, 404, 'nothing is published on that channel');
          const was = versionOf(m[1]); fs.unlinkSync(manifestFile(m[1])); log({ event: 'unpublish', channel: m[1], version: was }); return send(res, 200, { ok: true, channel: m[1], unpublished: was });
        }
        if (p === '/admin/config' && req.method === 'GET') return send(res, 200, { ok: true, publicUrl: publicUrl.replace(/\/+$/, ''), jwk, maxFile: MAX_FILE, allowHttp, channels: Object.fromEntries(channels().map(c => [c, versionOf(c)])) });
        if ((m = /^\/admin\/history\/([a-z0-9-]+)(?:\/(\d+(?:\.\d+){1,3}))?$/.exec(p)) && req.method === 'GET' && CHANNEL.test(m[1])) {
          if (!m[2]) return send(res, 200, { ok: true, channel: m[1], versions: history(m[1]) });
          const f = path.join(dirs.history, m[1] + '-' + m[2] + '.json'); if (!fs.existsSync(f)) return fail(res, 404, 'no such version');
          return send(res, 200, fs.readFileSync(f), { 'Content-Type': 'application/json; charset=utf-8' });
        }
        if (p === '/admin/verify' && req.method === 'GET') { const r = await verifyDisk(); return send(res, 200, { ok: true, allGood: r.every(x => x.ok), files: r }); }
        if (p === '/admin/stats' && req.method === 'GET') { flush(); return send(res, 200, { ok: true, days: stats.days }); }
        return fail(res, 404, 'not found');
      }
      if (req.method !== 'GET' && req.method !== 'HEAD') return fail(res, 405, 'method not allowed');
      if (p === '/healthz') return send(res, 200, { ok: true, channels: Object.fromEntries(channels().map(c => [c, versionOf(c)])) });
      const lim = limiter.allow('d:' + ip, rate); if (!lim.ok) return send(res, 429, { ok: false, error: 'too many requests' }, { 'Retry-After': lim.retryAfter });
      let m;
      if (p === '/update.json') return serveManifest(req, res, 'stable');
      if ((m = /^\/([a-z0-9-]+)\/update\.json$/.exec(p)) && CHANNEL.test(m[1])) return serveManifest(req, res, m[1]);
      if ((m = /^\/releases\/([^/]+)$/.exec(p))) { let name; try { name = decodeURIComponent(m[1]); } catch (_) { return fail(res, 400, 'bad name'); } return serveFile(req, res, name); }
      return fail(res, 404, 'not found');
    } catch (e) { if (!res.headersSent) fail(res, e.status || 500, e.status ? e.message : 'server error'); else res.destroy(); log({ event: 'error', message: String(e && e.message) }); }
  }

  const timer = setInterval(flush, 30000); timer.unref();
  return { handler, reindex, flush, channels, versionOf, dirs, jwk, index: () => index };
}

// Takes a releases/ folder (files + a signed update.json) into the data folder. The manifest is checked like an uploaded one.
async function seed(ota, from, log = console.log) {
  for (const n of fs.readdirSync(from)) { if (/^(update\.json|.*\.sha256|.*\.cer)$/.test(n) || !fs.statSync(path.join(from, n)).isFile()) continue; if (!fs.existsSync(path.join(ota.dirs.releases, n))) fs.copyFileSync(path.join(from, n), path.join(ota.dirs.releases, n)); }
  await ota.reindex();
  const m = path.join(from, 'update.json');
  if (!fs.existsSync(m)) return log('seeded the files of ' + from + ' (no update.json there: publish one with scripts/ota.js)');
  const v = update.verifyManifest(JSON.parse(fs.readFileSync(m, 'utf8')), ota.jwk), idx = ota.index();
  const missing = v.ok ? Object.values(v.manifest.files).filter(f => !idx[f.name] || idx[f.name].sha256 !== String(f.sha256).toLowerCase()).map(f => f.name) : [];
  if (!v.ok || missing.length) return log('seeded the files of ' + from + ', but its update.json was not taken over: ' + (v.ok ? 'it lists files that are not here or differ (' + missing.join(', ') + ')' : v.error) + ' Publish a new one with scripts/ota.js.');
  fs.copyFileSync(m, path.join(ota.dirs.manifests, 'stable.json')); log('seeded files and update.json (version ' + v.manifest.version + ') from ' + from);
}

function start(opts = {}) {
  const a = process.argv.slice(2), arg = (n, d) => { const i = a.indexOf(n); return i >= 0 ? a[i + 1] : d; };
  const port = Number(arg('--port', process.env.OTA_PORT || 8780)), host = arg('--host', process.env.OTA_HOST || '127.0.0.1'), dataDir = path.resolve(arg('--data', process.env.OTA_DATA || path.join(__dirname, '..', 'ota-data')));
  const jwk = require('../bridge/license').PUBLIC_JWK;
  const ota = createOta({ dataDir, jwk, token: process.env.OTA_ADMIN_TOKEN || '', publicUrl: process.env.OTA_PUBLIC_URL || '', rate: Number(process.env.OTA_RATE) || 120, trustProxy: process.env.OTA_TRUST_PROXY === '1', allowHttp: process.env.OTA_ALLOW_HTTP === '1', log: o => console.log(JSON.stringify({ t: new Date().toISOString(), ...o })), ...opts });
  const cert = process.env.OTA_TLS_CERT, key = process.env.OTA_TLS_KEY;
  const server = cert && key ? https.createServer({ cert: fs.readFileSync(cert), key: fs.readFileSync(key) }, ota.handler) : http.createServer(ota.handler);
  server.requestTimeout = 0; server.headersTimeout = 30000;                                    // large uploads take a while; headers must come quickly
  return (async () => {
    if (arg('--seed', '')) await seed(ota, path.resolve(arg('--seed')));
    else await ota.reindex();
    await new Promise(r => server.listen(port, host, r));
    console.log(`Audio Mixer OTA server on ${cert ? 'https' : 'http'}://${host}:${port}  data: ${dataDir}  admin: ${process.env.OTA_ADMIN_TOKEN ? 'on' : 'off (set OTA_ADMIN_TOKEN)'}`);
    if (!cert && host !== '127.0.0.1' && host !== 'localhost') console.log('No TLS here: the app only talks to https, so put nginx / Caddy in front (deploy/ota) or set OTA_TLS_CERT and OTA_TLS_KEY.');
    return { server, ota };
  })();
}

if (require.main === module) start().catch(e => { console.error('Error: ' + e.message); process.exit(1); });
module.exports = { createOta, seed, start };
