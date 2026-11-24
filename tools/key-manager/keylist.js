'use strict';
// AmF X1 Key Manager: the KEY LIST. Every key the Key Manager issues (single or batch) is recorded in <key folder>/issued.jsonl SEALED to the
// vendor's public key with the same ECIES box as the generator's sales log and inbox (scripts/inbox-crypto.js): writing needs no PIN, reading
// needs the private key, i.e. the folder unlocked with the PIN in this process. The paid-edition sales come from the generator's own sealed
// <key folder>/sales.jsonl. Records are opened in memory only, for one response; nothing decrypted is ever written to disk or logged.
// Revocation: AmF X1 keys are signed offline and the console checks a revocation list signed by the licence server (AMXR1, /revocations).
// The Key Manager cannot sign that list (it is not the server's key); it keeps MARKS (<key folder>/revoke-marks.json: serial, reason, time;
// no customer data) and copies the marked serials for the licence server's REVOKE.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const inboxCrypto = require('../../scripts/inbox-crypto');
const vault = require('../../scripts/license-vault');
const lic = require('../../bridge/license');

const ISSUED = 'issued.jsonl', SALES = 'sales.jsonl', MARKS = 'revoke-marks.json';
const MAX_LINES = 20000;
const ID_RE = /^[A-Za-z0-9-]{4,40}$/;

const vendorPub = dir => { try { return JSON.parse(fs.readFileSync(path.join(dir, 'public.jwk.json'), 'utf8')); } catch (_) { return null; } };
const lines = f => { try { return fs.readFileSync(f, 'utf8').split('\n').filter(Boolean).slice(-MAX_LINES).map(l => { try { return JSON.parse(l); } catch (_) { return null; } }).filter(Boolean); } catch (_) { return []; } };
const str = (v, n) => String(v == null ? '' : v).replace(/[\u0000-\u001f]/g, '').slice(0, n);

// the payload of an AMIX1 key this process just signed (read only: the key is not re-verified here, it came straight from the signer)
function keyPayload(key) {
  const parts = String(key || '').split('.');
  if (parts.length !== 3 || parts[0] !== 'AMIX1') return null;
  try { return JSON.parse(Buffer.from(parts[1], 'base64url').toString('utf8')); } catch (_) { return null; }
}

// Called with the JSON body of a successful POST /issue: seals one record per key into issued.jsonl. Returns how many were recorded.
function recordIssued(dir, body, now = Date.now()) {
  const pub = vendorPub(dir); if (!pub || !body || body.ok === false) return 0;
  const keys = body.key ? [body.key] : Array.isArray(body.keys) ? body.keys.map(k => k && k.key) : [];
  const out = [];
  for (const key of keys) {
    const p = keyPayload(key); if (!p || !p.id) continue;
    out.push(JSON.stringify(inboxCrypto.seal({ at: now, kind: 'key', plan: p.plan, id: p.id, name: p.name || '', email: p.email || '', mid: p.mid || '', seats: p.seats || 1, issued: p.issued || now, expires: p.expires || null, key }, pub)));
  }
  if (!out.length) return 0;
  fs.mkdirSync(dir, { recursive: true, mode: 0o700 });
  fs.appendFileSync(path.join(dir, ISSUED), out.join('\n') + '\n', { mode: 0o600 });
  return out.length;
}

// the private key when it can be had without asking: an unlocked PIN session, or an old plain private.pem; otherwise null (LOCKED)
function privateKeyFor(dir, session) {
  try { if (vault.isLocked(dir)) return session && session.isOpen() ? session.key(dir) : null; return vault.hasKey(dir) ? vault.loadKey(dir) : null; } catch (_) { return null; }
}

function readMarks(dir) { try { const j = JSON.parse(fs.readFileSync(path.join(dir, MARKS), 'utf8')); return j && typeof j === 'object' && !Array.isArray(j) ? j : {}; } catch (_) { return {}; } }
function setMark(dir, id, on, reason, now = Date.now()) {
  if (!ID_RE.test(String(id || ''))) throw Object.assign(new Error('not a key serial'), { status: 400 });
  const m = readMarks(dir);
  if (on) m[id] = { at: now, reason: str(reason, 120) }; else delete m[id];
  const f = path.join(dir, MARKS), tmp = f + '.tmp';
  fs.writeFileSync(tmp, JSON.stringify(m, null, 1) + '\n', { mode: 0o600 }); fs.renameSync(tmp, f);
  return m;
}

const planName = id => (lic.PLANS[id] && lic.PLANS[id].name) || String(id || '');
// All issued keys and edition sales, newest first. Locked folder: only the count and times, nothing personal.
function listAll(dir, session) {
  const issued = lines(path.join(dir, ISSUED)), sales = lines(path.join(dir, SALES));
  const key = privateKeyFor(dir, session);
  if (!key) return { locked: true, count: issued.length + sales.length, dir };
  const marks = readMarks(dir), rows = []; let damaged = 0;
  const open = r => { if (!r || !r.sealed) { damaged++; return null; } try { return inboxCrypto.open(r, key); } catch (_) { damaged++; return null; } };
  for (const r of issued) {
    const x = open(r); if (!x) continue;
    rows.push({ at: x.at, type: 'key', id: str(x.id, 40), plan: str(x.plan, 20), planName: planName(x.plan), name: str(x.name, 80), email: str(x.email, 120), machine: str(x.mid, 40), seats: x.seats || 1, expires: x.expires || null, key: str(x.key, 4096) });
  }
  for (const r of sales) {
    const x = open(r); if (!x) continue;
    rows.push({ at: x.at, type: 'edition', id: str(x.serial, 40), plan: str(x.plan, 20), planName: planName(x.plan), edition: str(x.edition, 40), order: str(x.order, 40), name: str(x.buyer, 80), whatsapp: str(x.whatsapp, 20), expires: x.expires || null });
  }
  for (const row of rows) if (marks[row.id]) row.revoke = { at: marks[row.id].at, reason: str(marks[row.id].reason, 120) };
  rows.sort((a, b) => (b.at || 0) - (a.at || 0));
  return { locked: false, rows, damaged, dir, marked: Object.keys(marks).filter(k => ID_RE.test(k)) };
}

function matches(row, q) {
  const t = String(q || '').trim().toLowerCase(); if (!t) return true;
  return [row.id, row.plan, row.planName, row.name, row.email, row.machine, row.order, row.edition, row.whatsapp, row.type, row.revoke ? 'revoke' : ''].some(v => String(v || '').toLowerCase().includes(t));
}

// CSV for a spreadsheet: every cell quoted, and a cell that starts with = + - @ (or a tab / CR) is prefixed with ' so that a spreadsheet
// never runs a customer-typed name as a formula
function toCsv(rows) {
  const cell = v => { let s = String(v == null ? '' : v); if (/^[=+\-@\t\r]/.test(s)) s = "'" + s; return '"' + s.replace(/"/g, '""') + '"'; };
  const day = t => (t ? new Date(t).toISOString().slice(0, 10) : '');
  const head = ['date', 'type', 'serial', 'plan', 'edition', 'name', 'email', 'machine', 'seats', 'expires', 'order', 'whatsapp', 'revoke', 'revoke_reason', 'key'];
  return head.join(',') + '\n' + rows.map(r => [day(r.at), r.type, r.id, r.planName, r.edition || '', r.name, r.email || '', r.machine || '', r.seats || '', day(r.expires), r.order || '', r.whatsapp || '', r.revoke ? 'yes' : '', r.revoke ? r.revoke.reason : '', r.key || ''].map(cell).join(',')).join('\n') + '\n';
}

// ---- the KEY LIST page (same look and the same CSP-hash lock as the generator page: no inline code runs that is not hashed here) ----------
function page() {
  return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>AmF X1 Key Manager — Key list</title>
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
main{max-width:1100px;margin:0 auto;padding:16px;display:grid;gap:12px}
.bar{display:flex;flex-wrap:wrap;gap:8px;align-items:center}
input,select{background:var(--panel2);border:1px solid var(--line);border-radius:7px;padding:7px 9px}
#q{flex:1 1 220px;min-width:0}
.btn{border:1px solid var(--line);background:var(--amber);color:#111;padding:8px 14px;border-radius:7px;cursor:pointer;font:800 11px var(--mono);letter-spacing:.06em;text-transform:uppercase}
.btn2{border:1px solid var(--line);background:var(--panel2);padding:4px 9px;border-radius:6px;cursor:pointer;font:700 10px var(--mono);letter-spacing:.04em;text-transform:uppercase}
.pill{display:inline-block;padding:1px 8px;border-radius:99px;font:700 10px var(--mono);letter-spacing:.06em;border:1px solid var(--amber);color:var(--amber);text-decoration:none}
.card{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:14px;overflow-x:auto}
table{border-collapse:collapse;width:100%;font-size:12px}
th,td{text-align:left;padding:6px 8px;border-bottom:1px solid var(--line);vertical-align:top}
th{font:700 10px var(--mono);letter-spacing:.08em;text-transform:uppercase;color:var(--dim)}
td.mono{font-family:var(--mono)}
tr.rv td{color:var(--red)}
.dim{color:var(--dim)}
.err{color:var(--red)}
[hidden]{display:none!important}
</style>
</head>
<body>
<header><h1>AMF X1 <b>KEY MANAGER</b></h1><span><a class="pill" href="/">GENERATOR</a> <span class="pill">KEY LIST</span></span></header>
<main>
  <section class="card" id="locked" hidden><p><b>The key folder is locked.</b> <span id="lcount"></span> Unlock it with your PIN on the <a href="/">generator page</a>, then come back: the list is decrypted in this program's memory only.</p></section>
  <div class="bar" id="tools" hidden>
    <input id="q" type="search" placeholder="Search serial, name, e-mail, machine, plan, order…" autocomplete="off" aria-label="Search">
    <select id="type" aria-label="Type"><option value="">All</option><option value="key">Licence keys</option><option value="edition">Edition sales</option><option value="revoke">Marked for revocation</option></select>
    <button class="btn" id="csv" type="button">EXPORT CSV</button>
    <button class="btn" id="copyrv" type="button">COPY MARKED SERIALS</button>
    <button class="btn2" id="reload" type="button">REFRESH</button>
  </div>
  <p class="dim" id="info"></p>
  <section class="card" id="list" hidden>
    <table><thead><tr><th>Date</th><th>Type</th><th>Serial</th><th>Plan</th><th>Name / buyer</th><th>E-mail / WhatsApp</th><th>Machine</th><th>Expires</th><th></th></tr></thead><tbody id="rows"></tbody></table>
  </section>
  <p class="dim">Revocation: keys are signed offline, so a key stops working only when the licence server's signed revocation list names it. MARK here, then COPY MARKED SERIALS and revoke them on the licence server.</p>
  <p class="err" id="err" hidden></p>
</main>
<script>
'use strict';
(function () {
  var $ = function (id) { return document.getElementById(id); };
  var H = { 'Content-Type': 'application/json', 'X-Gen-Action': '1' };
  var all = [];
  var day = function (t) { return t ? new Date(t).toISOString().slice(0, 10) : ''; };
  var err = function (t) { $('err').textContent = t; $('err').hidden = !t; };
  var filterNow = function () {
    var q = $('q').value.trim().toLowerCase(), ty = $('type').value;
    return all.filter(function (r) {
      if (ty === 'revoke' ? !r.revoke : ty && r.type !== ty) return false;
      if (!q) return true;
      return [r.id, r.plan, r.planName, r.name, r.email, r.machine, r.order, r.edition, r.whatsapp, r.type].some(function (v) { return String(v || '').toLowerCase().indexOf(q) >= 0; });
    });
  };
  var cellText = function (tr, text, mono) { var td = document.createElement('td'); td.textContent = text; if (mono) td.className = 'mono'; tr.appendChild(td); return td; };
  var draw = function () {
    var rows = filterNow(), tb = $('rows'); tb.textContent = '';
    rows.forEach(function (r) {
      var tr = document.createElement('tr'); if (r.revoke) tr.className = 'rv';
      cellText(tr, day(r.at)); cellText(tr, r.type === 'key' ? 'KEY' : 'EDITION'); cellText(tr, r.id, true);
      cellText(tr, r.planName + (r.edition ? ' (' + r.edition + ')' : '') + (r.order ? ' #' + r.order : ''));
      cellText(tr, r.name || ''); cellText(tr, r.email || r.whatsapp || ''); cellText(tr, r.machine || '', true);
      cellText(tr, r.expires ? day(r.expires) : 'never');
      var td = cellText(tr, '');
      if (r.key) { var c = document.createElement('button'); c.className = 'btn2'; c.type = 'button'; c.textContent = 'COPY KEY'; c.addEventListener('click', function () { navigator.clipboard.writeText(r.key).then(function () { c.textContent = 'COPIED'; }); }); td.appendChild(c); td.appendChild(document.createTextNode(' ')); }
      var m = document.createElement('button'); m.className = 'btn2'; m.type = 'button'; m.textContent = r.revoke ? 'UNMARK' : 'MARK REVOKE';
      if (r.revoke && r.revoke.reason) m.title = r.revoke.reason;
      m.addEventListener('click', function () {
        var on = !r.revoke, reason = '';
        if (on) { reason = window.prompt('Reason for revoking ' + r.id + ' (refund, chargeback, leaked, …)', ''); if (reason === null) return; }
        fetch('/km/api/revoke', { method: 'POST', headers: H, body: JSON.stringify({ id: r.id, on: on, reason: reason }) }).then(function (x) { return x.json(); }).then(function (j) { if (!j.ok) return err(j.error || 'failed'); load(); }).catch(function () { err('Could not reach the Key Manager.'); });
      });
      td.appendChild(m);
      tb.appendChild(tr);
    });
    $('info').textContent = rows.length + ' of ' + all.length + ' records' + (all.length ? '' : ' (keys issued from this Key Manager and edition sales appear here)');
  };
  var load = function () {
    err('');
    fetch('/km/api/keys', { headers: { 'X-Gen-Action': '1' } }).then(function (x) { return x.json(); }).then(function (j) {
      if (!j.ok) return err(j.error || 'failed');
      $('locked').hidden = !j.locked; $('tools').hidden = !!j.locked; $('list').hidden = !!j.locked;
      if (j.locked) { $('lcount').textContent = j.count + ' sealed record(s).'; $('info').textContent = ''; return; }
      all = j.rows || []; draw();
      if (j.damaged) err(j.damaged + ' record(s) could not be opened (damaged, or sealed to another key).');
    }).catch(function () { err('Could not reach the Key Manager.'); });
  };
  $('q').addEventListener('input', draw); $('type').addEventListener('change', draw); $('reload').addEventListener('click', load);
  $('csv').addEventListener('click', function () {
    var p = new URLSearchParams({ q: $('q').value, type: $('type').value });
    fetch('/km/api/keys.csv?' + p.toString(), { headers: { 'X-Gen-Action': '1' } }).then(function (x) { if (!x.ok) throw new Error(); return x.text(); }).then(function (t) {
      var a = document.createElement('a'); a.href = URL.createObjectURL(new Blob([t], { type: 'text/csv' })); a.download = 'amfx1-keys-' + new Date().toISOString().slice(0, 10) + '.csv';
      document.body.appendChild(a); a.click(); setTimeout(function () { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
    }).catch(function () { err('Export failed (is the folder still unlocked?).'); });
  });
  $('copyrv').addEventListener('click', function () {
    var ids = all.filter(function (r) { return r.revoke; }).map(function (r) { return r.id; });
    if (!ids.length) return err('No key is marked for revocation.');
    navigator.clipboard.writeText(ids.join('\\n')).then(function () { $('copyrv').textContent = 'COPIED ' + ids.length; setTimeout(function () { $('copyrv').textContent = 'COPY MARKED SERIALS'; }, 1500); });
  });
  load();
})();
</script>
</body>
</html>`;
}

const hash = t => "'sha256-" + crypto.createHash('sha256').update(t, 'utf8').digest('base64') + "'";
const inlineOf = (body, tag) => { const m = new RegExp('<' + tag + '(?:\\s[^>]*)?>([\\s\\S]*?)</' + tag + '>').exec(body); return m ? m[1] : ''; };
let cached = null;
function pageWithCsp() {
  if (cached) return cached;
  const body = page();
  const csp = `default-src 'none'; script-src ${hash(inlineOf(body, 'script'))}; style-src ${hash(inlineOf(body, 'style'))}; connect-src 'self'; img-src data:; base-uri 'none'; form-action 'none'; frame-ancestors 'none'`;
  return (cached = { body, csp });
}

module.exports = { recordIssued, listAll, toCsv, matches, setMark, readMarks, keyPayload, privateKeyFor, pageWithCsp, hash, ISSUED, SALES, MARKS };
