#!/usr/bin/env node
'use strict';
// AmF X1 MANAGER: the vendor's one place for licence keys, customers, releases (OTA), key requests and the activity of both servers.
// It runs on the vendor's own computer and answers this computer only (127.0.0.1, never another address); it is never deployed to the
// public site. It holds no key and no data of its own: every action goes, through the small proxy below, to the two servers that already
// exist, which check the staff member's sign-in (one JWT for both, bridge/portal-auth) on every call:
//
//   /lic/<path>  ->  the licence server   (LICENSE_SERVER, default http://127.0.0.1:8787; bridge/license-server/server.js)
//   /ota/<path>  ->  the OTA server       (OTA_SERVER,     default http://127.0.0.1:8780; ota-server/server.js)
//
// Only the administration routes, the sign-in and the health checks are passed on; the public routes of the servers are not reachable
// through here. The sign-in token stays in the page (sessionStorage) and is sent as the Authorization header: this process never stores it.
//
//   node management/vendor/server.js            then open http://127.0.0.1:8790/   (Windows: management/windows/AmF X1 Manager.cmd)
//   MANAGER_PORT=8790  LICENSE_SERVER=https://licence.example  OTA_SERVER=https://ota.example   (an SSH tunnel to 127.0.0.1 is the safe way)
const http = require('node:http');
const path = require('node:path');

const HOST = '127.0.0.1';   // fixed: the manager is never reachable from another computer
const MAX_BODY = 1024 * 1024, MAX_REPLY = 8 * 1024 * 1024, TIMEOUT_MS = 15000;
const { inlinePage } = require('../inline-page');   // the page is one file (Tailwind Play CDN + its inline script, allowed by hash)
// what may pass: the administration of each server, its sign-in, and its health check
const ALLOW = { lic: /^\/(admin\/[A-Za-z0-9_./:%-]*|health)$/, ota: /^\/(admin\/[A-Za-z0-9_./:%-]*|healthz)$/ };

function upstreams(env = process.env) {
  const clean = (u, d) => { const s = String(u || d).replace(/\/+$/, ''); const x = new URL(s); if (x.protocol !== 'http:' && x.protocol !== 'https:') throw new Error('not an http(s) address: ' + s); return s; };
  return { lic: clean(env.LICENSE_SERVER, 'http://127.0.0.1:8787'), ota: clean(env.OTA_SERVER, 'http://127.0.0.1:8780') };
}

function createManager({ port = 8790, up = upstreams(), dir = __dirname } = {}) {
  const page = inlinePage(path.join(dir, 'index.html'));
  const sec = { 'X-Content-Type-Options': 'nosniff', 'Referrer-Policy': 'no-referrer', 'X-Frame-Options': 'DENY', 'Cache-Control': 'no-store', 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Resource-Policy': 'same-origin' };
  const send = (res, status, body, headers = {}) => { const b = typeof body === 'string' || Buffer.isBuffer(body) ? body : JSON.stringify(body);
    res.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8', ...sec, ...headers }); res.end(b); };
  const ownHost = h => { const p = server.address() && server.address().port; return h === `127.0.0.1:${p}` || h === `localhost:${p}`; };
  const readBody = req => new Promise((ok, bad) => { const parts = []; let n = 0;
    req.on('data', d => { n += d.length; if (n > MAX_BODY) { bad(Object.assign(new Error('the request is too large'), { status: 413 })); req.destroy(); } else parts.push(d); });
    req.on('end', () => ok(Buffer.concat(parts))); req.on('error', bad); });

  const server = http.createServer(async (req, res) => {
    try {
      // SECURITY: a page from anywhere else (or a made-up name pointed at 127.0.0.1) cannot use the manager: the Host must be this one, and a
      // request that changes something must come from this page (same Origin)
      if (!ownHost(String(req.headers.host || ''))) return send(res, 421, { ok: false, error: 'wrong host' });
      const origin = req.headers.origin; if (origin && !ownHost(origin.replace(/^https?:\/\//, ''))) return send(res, 403, { ok: false, error: 'foreign origin' });
      const url = new URL(req.url, 'http://x'), p = url.pathname;
      if ((p === '/' || p === '/index.html') && (req.method === 'GET' || req.method === 'HEAD'))
        return send(res, 200, req.method === 'HEAD' ? '' : page.body, { 'Content-Type': 'text/html; charset=utf-8', 'Content-Security-Policy': page.csp });
      if (p === '/config' && req.method === 'GET') return send(res, 200, { ok: true, licenseServer: up.lic, otaServer: up.ota });
      const m = /^\/(lic|ota)(\/.*)$/.exec(p);
      if (!m || !ALLOW[m[1]].test(m[2])) return send(res, 404, { ok: false, error: 'not found' });
      if (!['GET', 'POST', 'PUT', 'DELETE'].includes(req.method)) return send(res, 405, { ok: false, error: 'method not allowed' });
      if (req.method !== 'GET' && !origin) return send(res, 403, { ok: false, error: 'a change must come from the manager page' });
      const body = req.method === 'GET' ? undefined : await readBody(req);
      const headers = { accept: 'application/json' }; if (req.headers.authorization) headers.authorization = req.headers.authorization; if (body) headers['content-type'] = 'application/json';
      let r;
      try { r = await fetch(up[m[1]] + m[2] + url.search, { method: req.method, headers, body, redirect: 'manual', signal: AbortSignal.timeout(TIMEOUT_MS) }); }
      catch (e) { return send(res, 502, { ok: false, error: `the ${m[1] === 'lic' ? 'licence' : 'OTA'} server at ${up[m[1]]} does not answer (${e.cause && e.cause.code || e.name})` }); }
      const buf = Buffer.from(await r.arrayBuffer()); if (buf.length > MAX_REPLY) return send(res, 502, { ok: false, error: 'the reply is too large' });
      const type = String(r.headers.get('content-type') || 'application/json');
      const extra = /text\/csv/.test(type) ? { 'Content-Disposition': r.headers.get('content-disposition') || 'attachment' } : {};
      return send(res, r.status, buf, { 'Content-Type': type, ...extra });
    } catch (e) { return send(res, e.status || 500, { ok: false, error: e.status ? e.message : 'internal error' }); }
  });
  server.listen(port, HOST);
  return server;
}

module.exports = { createManager, upstreams, ALLOW };

if (require.main === module) {
  const port = Number(process.env.MANAGER_PORT || 8790); const up = upstreams();
  const s = createManager({ port, up });
  s.on('listening', () => console.log(`AmF X1 Manager on http://${HOST}:${s.address().port}/  (licence server ${up.lic}, OTA server ${up.ota}; this computer only)`));
  s.on('error', e => { console.error('AmF X1 Manager: ' + (e.code === 'EADDRINUSE' ? `port ${port} is in use (MANAGER_PORT=...)` : e.message)); process.exit(1); });
}
