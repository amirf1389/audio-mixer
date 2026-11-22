'use strict';
// planted unsafe server for the audit's unit tests
const http = require('http'), fs = require('fs'), path = require('path');
const ROOT = __dirname;
http.createServer((req, res) => {
  const url = new URL(req.url, 'http://x');
  res.setHeader('Access-Control-Allow-Origin', req.headers.origin || '*');
  const cors = { 'Access-Control-Allow-Origin': origin };
  if (req.method === 'POST' && url.pathname === '/api/delete') { let b = ''; req.on('data', d => { b += d; }); req.on('end', () => res.end('ok')); return; }
  const rel = decodeURIComponent(url.pathname);
  fs.readFile(path.join(ROOT, rel), (e, d) => res.end(d));
}).listen(8080);
