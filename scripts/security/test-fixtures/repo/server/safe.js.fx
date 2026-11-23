'use strict';
// a well-behaved server: loopback, bearer token in constant time, body limit, rate limit, contained file route
const http = require('http'), fs = require('fs'), path = require('path'), crypto = require('crypto');
const HOST = '127.0.0.1', MAX_BODY = 4096, ROOT = path.join(__dirname, 'public');
const hits = new Map(); const limiter = { allow: k => { const n = (hits.get(k) || 0) + 1; hits.set(k, n); return n < 100; } };
const tokenOk = req => { const a = Buffer.from(String(req.headers.authorization || '')), b = Buffer.from('Bearer ' + process.env.TOKEN); return a.length === b.length && crypto.timingSafeEqual(a, b); };
http.createServer((req, res) => {
  res.setHeader('X-Content-Type-Options', 'nosniff');
  if (!limiter.allow(req.socket.remoteAddress)) { res.writeHead(429); return res.end(); }
  const url = new URL(req.url, 'http://x');
  if (req.method === 'POST' && url.pathname === '/api/save') {
    if (!tokenOk(req)) { res.writeHead(401); return res.end(); }
    let n = 0; req.on('data', d => { n += d.length; if (n > MAX_BODY) { res.writeHead(413); res.end(); req.destroy(); } });
    return req.on('end', () => res.end('ok'));
  }
  const rel = decodeURIComponent(url.pathname);
  const file = path.resolve(ROOT, '.' + rel);
  if (!file.startsWith(ROOT + path.sep)) { res.writeHead(403); return res.end(); }
  fs.readFile(file, (e, d) => res.end(d));
}).listen(8081, HOST);
