#!/usr/bin/env node
'use strict';
// A local, form-based way to issue Audio Mixer license keys: the same signing scripts/license.js (vendor), scripts/license-sandbox.js (sandbox)
// and scripts/license-admin.js (admin) already do from the command line, without having to remember the "issue" command's flags. Runs a tiny
// HTTP server on 127.0.0.1 ONLY (never another host, not configurable): open the printed address in a browser, fill in the plan / name / e-mail /
// machine code / expiry, and get the signed key back to copy or send. The private key never leaves this process: it is read from disk per request,
// exactly like the "issue" command, and is never written to a log, a response header, or anywhere but the signed key itself.
//   node scripts/license-generator.js [--kind vendor|sandbox|admin] [--port 0] [--dir D]
//     vendor (default)  scripts/license.js's key          (default ~/.audio-mixer-vendor,  or AUDIO_MIXER_VENDOR_DIR  / --dir)
//     sandbox           scripts/license-sandbox.js's key  (default ~/.audio-mixer-sandbox, or AUDIO_MIXER_SANDBOX_DIR / --dir)
//     admin             scripts/license-admin.js's key    (default ~/.audio-mixer-admin,   or AUDIO_MIXER_ADMIN_DIR   / --dir)
//   --port 0 (default) picks a free port and prints the address; a fixed port reopens the same one every time.
// The key directory must already exist (node scripts/license.js init, or the sandbox / admin equivalent): this tool only issues keys with a key
// pair that is already there, it never creates one.
const http = require('node:http');
const crypto = require('node:crypto');
const lic = require('../bridge/license');
const { originAllowed, hostAllowed } = require('../bridge/server');

const KINDS = {
  vendor: { mod: require('./license.js'), dirFn: 'vendorDir', label: 'VENDOR', needsName: true },
  sandbox: { mod: require('./license-sandbox.js'), dirFn: 'sandboxDir', label: 'SANDBOX', needsName: false },
  admin: { mod: require('./license-admin.js'), dirFn: 'adminDir', label: 'ADMIN', needsName: true },
};

const arg = (a, n, d) => { const i = a.indexOf(n); return i >= 0 ? a[i + 1] : d; };

function page(kind) {
  const k = KINDS[kind];
  const plans = Object.values(lic.PLANS).map(p => `<option value="${p.id}">${p.name} (${p.channels} ch)</option>`).join('');
  return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Audio Mixer — License Key Generator (${k.label})</title>
<link rel="icon" href="data:,">
<style>
:root{--bg:#0b0e14;--panel:#121722;--panel2:#171d2b;--line:#252d3f;--text:#e6e9f0;--dim:#8b95ab;--amber:#fbbf24;--cyan:#22d3ee;--green:#34d399;--red:#f43f5e;--mono:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}
@media (prefers-color-scheme:light){:root{--bg:#f3f5f9;--panel:#fff;--panel2:#f6f8fc;--line:#d8deea;--text:#161b27;--dim:#5b6579;--amber:#b45309;--cyan:#0e7490;--green:#047857;--red:#be123c}}
*{box-sizing:border-box}
html,body{margin:0;background:var(--bg);color:var(--text);font:14px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
button,input,select{font:inherit;color:inherit}
header{display:flex;flex-wrap:wrap;gap:10px 16px;align-items:center;justify-content:space-between;padding:12px 16px;border-bottom:1px solid var(--line);background:var(--panel)}
h1{margin:0;font:800 15px var(--mono);letter-spacing:.14em;text-transform:uppercase}
h1 b{color:var(--amber)}
h2{margin:0 0 10px;font:700 12px var(--mono);letter-spacing:.12em;text-transform:uppercase;color:var(--cyan)}
main{max-width:560px;margin:0 auto;padding:16px;display:grid;gap:14px}
.card{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:18px}
.field{display:grid;gap:4px;margin-bottom:10px}
label{font:700 10px var(--mono);letter-spacing:.08em;text-transform:uppercase;color:var(--dim)}
input,select{background:var(--panel2);border:1px solid var(--line);border-radius:7px;padding:7px 9px}
.row2{display:grid;gap:10px;grid-template-columns:1fr 1fr}
.btn{display:inline-block;border:1px solid var(--line);background:var(--amber);color:#111;padding:10px 18px;border-radius:7px;cursor:pointer;font:800 12px var(--mono);letter-spacing:.06em;text-transform:uppercase;width:100%}
.btn:disabled{opacity:.5;cursor:not-allowed}
.mono{font-family:var(--mono)}
.dim{color:var(--dim)}
textarea,#key-out{width:100%;background:var(--panel2);border:1px solid var(--line);border-radius:7px;padding:9px;font-family:var(--mono);font-size:12px;word-break:break-all;white-space:pre-wrap}
.pill{display:inline-block;padding:1px 8px;border-radius:99px;font:700 10px var(--mono);letter-spacing:.06em;border:1px solid var(--amber);color:var(--amber)}
.err{color:var(--red)}
[hidden]{display:none!important}
</style>
</head>
<body>
<header><h1>AUDIO <b>MIXER</b></h1><span class="pill">${k.label} KEY</span></header>
<main>
  <section class="card">
    <h2>GENERATE A LICENSE KEY</h2>
    <form id="f">
      <div class="field"><label for="plan">Plan</label><select id="plan">${plans}</select></div>
      <div class="field"><label for="name">Name${k.needsName ? '' : ' (optional)'}</label><input id="name" type="text" maxlength="80" autocomplete="off"></div>
      <div class="field"><label for="email">E-mail (optional)</label><input id="email" type="text" maxlength="120" autocomplete="off"></div>
      <div class="field"><label for="machine">Machine code (optional — bind the key to one computer)</label><input id="machine" type="text" maxlength="40" autocomplete="off" placeholder="XXXX-XXXX-XXXX-XXXX-XXXX"></div>
      <div class="row2">
        <div class="field"><label for="days">Expires in (days, 0 = never)</label><input id="days" type="number" min="0" step="1" value="${kind === 'sandbox' || kind === 'admin' ? 30 : 0}"></div>
        <div class="field"><label for="seats">Seats</label><input id="seats" type="number" min="1" step="1" value="1"></div>
      </div>
      <button class="btn" id="go" type="submit">GENERATE KEY</button>
    </form>
    <p class="err" id="err" hidden></p>
  </section>
  <section class="card" id="result" hidden>
    <h2>SIGNED KEY</h2>
    <div id="key-out" class="mono"></div>
    <p class="dim" id="summary"></p>
    <button class="btn" id="copy" type="button">COPY</button>
  </section>
</main>
<script>
'use strict';
// Every field here is sent to POST /issue on this SAME local server and comes back as the signed key text; the key and the summary are
// written with textContent only, no HTML is ever parsed.
(function () {
  var $ = function (id) { return document.getElementById(id); };
  $('f').addEventListener('submit', function (ev) {
    ev.preventDefault();
    $('err').hidden = true; $('go').disabled = true; $('go').textContent = 'GENERATING…';
    var body = { plan: $('plan').value, name: $('name').value, email: $('email').value, machine: $('machine').value, days: Number($('days').value) || 0, seats: Number($('seats').value) || 1 };
    fetch('/issue', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) })
      .then(function (r) { return r.json().then(function (j) { return { ok: r.ok, j: j }; }); })
      .then(function (res) {
        if (!res.ok || !res.j || !res.j.ok) { $('err').textContent = (res.j && res.j.error) || 'Could not generate the key.'; $('err').hidden = false; return; }
        $('key-out').textContent = res.j.key;
        $('summary').textContent = res.j.summary;
        $('result').hidden = false;
      })
      .catch(function () { $('err').textContent = 'Could not reach this server.'; $('err').hidden = false; })
      .then(function () { $('go').disabled = false; $('go').textContent = 'GENERATE KEY'; });
  });
  $('copy').addEventListener('click', function () {
    navigator.clipboard.writeText($('key-out').textContent).then(function () { $('copy').textContent = 'COPIED'; setTimeout(function () { $('copy').textContent = 'COPY'; }, 1200); });
  });
})();
</script>
</body>
</html>`;
}

// SHA-256 CSP hashes of the page's own inline <script> / <style>, the same lock the OTA server's public download page uses instead of
// 'unsafe-inline': computed once per kind (the page text is a fixed template, not read from a file, so there is nothing to re-check for changes).
const pages = {};
function pageFor(kind) {
  if (pages[kind]) return pages[kind];
  const body = page(kind);
  const hash = t => "'sha256-" + crypto.createHash('sha256').update(t, 'utf8').digest('base64') + "'";
  const inline = tag => { const m = new RegExp('<' + tag + '(?:\\s[^>]*)?>([\\s\\S]*?)</' + tag + '>').exec(body); return hash(m[1]); };
  const csp = `default-src 'none'; script-src ${inline('script')}; style-src ${inline('style')}; connect-src 'self'; img-src data:; base-uri 'none'; form-action 'none'; frame-ancestors 'none'`;
  return (pages[kind] = { body, csp });
}

function clean(v, n) { return String(v || '').replace(/[\u0000-\u001f<>]/g, '').trim().slice(0, n); }

function issueKey(kind, dirArgv, body) {
  const k = KINDS[kind];
  const plan = body.plan, name = clean(body.name, 80), email = clean(body.email, 120), machine = clean(body.machine, 40);
  const days = Number(body.days) || 0, seats = Math.max(1, Number(body.seats) || 1);
  if (!lic.PLANS[plan]) { const e = new Error('plan must be one of: ' + Object.keys(lic.PLANS).join(', ')); e.status = 400; throw e; }
  if (k.needsName && !name) { const e = new Error('name is required for a ' + k.label.toLowerCase() + ' key'); e.status = 400; throw e; }
  const dir = k.mod[k.dirFn](dirArgv);
  const r = k.mod.issue({ dir, plan, name, email: email || undefined, machine: machine || undefined, days, seats, now: Date.now() });
  const p = r.payload;
  const summary = `${k.label} ${lic.PLANS[p.plan].name} for ${p.name || '(no name)'}, ${p.expires ? 'expires ' + new Date(p.expires).toISOString().slice(0, 10) : 'no expiry'}${p.mid ? ', bound to ' + p.mid : ', not bound to a computer'}`;
  return { key: r.key, summary };
}

function createServer({ kind = 'vendor', dirArgv = [] } = {}) {
  if (!KINDS[kind]) throw new Error('kind must be one of: ' + Object.keys(KINDS).join(', '));
  const send = (res, code, body, headers = {}) => { const t = typeof body === 'string' ? body : JSON.stringify(body); res.writeHead(code, { 'Content-Type': typeof body === 'string' ? 'text/html; charset=utf-8' : 'application/json; charset=utf-8', 'X-Content-Type-Options': 'nosniff', 'Cache-Control': 'no-store', 'X-Frame-Options': 'DENY', 'Referrer-Policy': 'no-referrer', ...headers }); res.end(t); };
  const fail = (res, code, error) => send(res, code, { ok: false, error });

  return http.createServer(async (req, res) => {
    try {
      if (!hostAllowed(req.headers.host)) return fail(res, 403, 'host not allowed');
      if (!originAllowed(req.headers.origin)) return fail(res, 403, 'origin not allowed');
      const url = new URL(req.url, 'http://x');
      if (req.method === 'GET' && url.pathname === '/') { const p = pageFor(kind); return send(res, 200, p.body, { 'Content-Security-Policy': p.csp }); }
      if (req.method === 'POST' && url.pathname === '/issue') {
        const chunks = []; let n = 0;
        for await (const c of req) { n += c.length; if (n > 8192) return fail(res, 413, 'request too large'); chunks.push(c); }
        let body; try { body = JSON.parse(Buffer.concat(chunks).toString('utf8')); } catch (e) { return fail(res, 400, 'the body is not JSON'); }
        try { const r = issueKey(kind, dirArgv, body || {}); return send(res, 200, { ok: true, ...r }); } catch (e) { return fail(res, e.status || 500, e.message); }
      }
      return fail(res, 404, 'not found');
    } catch (e) { fail(res, 500, e.message); }
  });
}

function main(argv) {
  const kind = arg(argv, '--kind', 'vendor');
  if (!KINDS[kind]) { console.error('--kind must be one of: ' + Object.keys(KINDS).join(', ')); return 2; }
  const port = Number(arg(argv, '--port', 0)) || 0;
  const server = createServer({ kind, dirArgv: argv });
  server.listen(port, '127.0.0.1', () => {
    console.log(`Audio Mixer license key generator (${KINDS[kind].label}) on http://127.0.0.1:${server.address().port}/ — open it in a browser on this computer. Ctrl+C to stop.`);
  });
  return 0;
}

if (require.main === module) process.exit(main(process.argv.slice(2)));
module.exports = { KINDS, page, pageFor, issueKey, createServer, main };
