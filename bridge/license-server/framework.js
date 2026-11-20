'use strict';
// A small backend framework for the vendor's own servers: plain Node.js, no packages. It gives a server the parts every one of them needs and
// should not write twice: routes with parameters, a JSON body with a size limit, security headers on every answer, a bearer token checked in
// constant time, a rate limiter per address, static files from a fixed list (never a path taken from the request), and one place where an
// error becomes a JSON answer instead of a crash.
//   const app = createApp({ maxBody: 4096, trustProxy: false });
//   app.get('/health', c => c.json(200, { ok: true }));
//   app.post('/admin/items/:id/close', admin, async c => c.json(200, await close(c.params.id, await c.body())));
//   http.createServer(app.handler).listen(8787, '127.0.0.1');
// A route's functions run in order; one that answers (c.json / c.send) ends the chain, one that returns without answering lets the next run.
const crypto = require('node:crypto');
const fs = require('node:fs');

const SECURITY_HEADERS = {
  'x-content-type-options': 'nosniff', 'x-frame-options': 'DENY', 'referrer-policy': 'no-referrer', 'cache-control': 'no-store',
  'cross-origin-opener-policy': 'same-origin', 'cross-origin-resource-policy': 'same-origin',
  'permissions-policy': 'camera=(), microphone=(), geolocation=(), usb=(), bluetooth=()',
  'content-security-policy': "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self' data:; base-uri 'none'; form-action 'none'; frame-ancestors 'none'",
};

class HttpError extends Error { constructor(status, message, extra) { super(message); this.status = status; this.extra = extra || {}; } }

// Fixed windows of one minute per key ("what:address"). allow() says whether this call is within `limit` and, when not, how long to wait.
function rateLimiter({ windowMs = 60000, now = () => Date.now() } = {}) {
  const hits = new Map();
  return {
    allow(key, limit) {
      const t = now(), slot = Math.floor(t / windowMs), rec = hits.get(key);
      if (hits.size > 20000) for (const [k, v] of hits) if (v.slot !== slot) hits.delete(k);
      if (!rec || rec.slot !== slot) { hits.set(key, { slot, n: 1 }); return { ok: true, retryAfter: 0 }; }
      rec.n += 1;
      return rec.n <= limit ? { ok: true, retryAfter: 0 } : { ok: false, retryAfter: Math.ceil(((slot + 1) * windowMs - t) / 1000) };
    },
  };
}

// The token of "Authorization: Bearer <token>", compared in constant time so the answer time does not reveal how much of it matched.
function bearerOk(req, token) {
  const header = String(req.headers.authorization || '');
  if (!token || !header.startsWith('Bearer ')) return false;
  const given = Buffer.from(header.slice(7)), want = Buffer.from(token);
  return given.length === want.length && crypto.timingSafeEqual(given, want);
}

function compile(pattern) {   // '/admin/licenses/:id/devices/:machine' -> a matcher that returns the parameters or null
  const names = [];
  const re = new RegExp('^' + pattern.replace(/[.*+?^${}()|[\]\\]/g, '\\$&').replace(/:(\w+)/g, (_, n) => { names.push(n); return '([^/]+)'; }) + '$');
  return path => {
    const m = re.exec(path); if (!m) return null;
    const out = {};
    for (let i = 0; i < names.length; i++) { try { out[names[i]] = decodeURIComponent(m[i + 1]); } catch (_) { return null; } }
    return out;
  };
}

function createApp({ maxBody = 4096, trustProxy = false, onRequest = null } = {}) {
  const routes = [];
  const add = method => (pattern, ...fns) => { routes.push({ method, match: compile(pattern), fns }); };
  const app = { get: add('GET'), post: add('POST'), put: add('PUT'), delete: add('DELETE') };

  // Files the server gives out are named here, once: { '/admin/': [file on disk, content type] }. The request never chooses a path on disk.
  app.static = (map, guard) => {
    for (const [url, [file, type]] of Object.entries(map)) {
      app.get(url, c => {
        if (guard && !guard(c)) return undefined;
        let data; try { data = fs.readFileSync(file); } catch (_) { throw new HttpError(404, 'not found'); }
        return c.send(200, data, { 'content-type': type });
      });
    }
  };

  app.handler = (req, res) => {
    const url = new URL(req.url, 'http://x'), started = Date.now();
    const fwd = trustProxy ? String(req.headers['x-forwarded-for'] || '').split(',').pop().trim() : '';
    const c = {
      req, res, path: url.pathname, query: url.searchParams, params: {}, ip: fwd || (req.socket && req.socket.remoteAddress) || '', state: {},
      send(status, data, headers = {}) { if (res.headersSent) return true; res.writeHead(status, { ...SECURITY_HEADERS, ...headers }); res.end(req.method === 'HEAD' ? undefined : data); return true; },
      json(status, obj, headers = {}) { return c.send(status, JSON.stringify(obj), { 'content-type': 'application/json', ...headers }); },
      body(limit = maxBody) {   // the JSON body, read once; too large or not JSON is the caller's error, never a crash (a route may allow more, e.g. a preset pack)
        if (c._body) return c._body;
        c._body = new Promise((resolve, reject) => {
          let size = 0; const chunks = [];
          req.on('data', d => { size += d.length; if (size <= limit) chunks.push(d); });
          req.on('end', () => {
            if (size > limit) return reject(new HttpError(413, 'request too large'));
            try { const v = JSON.parse(Buffer.concat(chunks).toString('utf8') || '{}'); if (!v || typeof v !== 'object' || Array.isArray(v)) throw new Error('not an object'); resolve(v); }
            catch (_) { reject(new HttpError(400, 'body must be JSON')); }
          });
          req.on('error', () => reject(new HttpError(400, 'the request was cut short')));
        });
        return c._body;
      },
    };
    if (onRequest) res.on('finish', () => { try { onRequest(c, res.statusCode, Date.now() - started); } catch (_) { /* a log line must never break an answer */ } });
    (async () => {
      let pathKnown = false;
      for (const r of routes) {
        const params = r.match(c.path); if (!params) continue;
        pathKnown = true;
        if (r.method !== req.method && !(r.method === 'GET' && req.method === 'HEAD')) continue;
        c.params = params;
        for (const fn of r.fns) { if (await fn(c)) return; if (res.headersSent) return; }
      }
      if (!res.headersSent) c.json(pathKnown ? 405 : 404, { ok: false, error: pathKnown ? 'method not allowed' : 'not found' });
    })().catch(e => {
      if (res.headersSent) return;
      if (e instanceof HttpError) return c.json(e.status, { ok: false, error: e.message, ...e.extra }, e.extra.retryAfter ? { 'retry-after': String(e.extra.retryAfter) } : {});
      console.error('request failed:', e && e.message);   // the message only: never a body, a key or a token
      c.json(500, { ok: false, error: 'server error' });
    });
  };
  return app;
}

module.exports = { createApp, rateLimiter, bearerOk, HttpError, SECURITY_HEADERS };
