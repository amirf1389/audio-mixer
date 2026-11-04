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
//   --port 8799 (default) is the address the app's GENERATOR button opens; when it is taken a free port is used instead. --port 0 always picks a free one.
//   The page fills plan / name / e-mail / machine from its address (?plan=&name=&email=&machine=, what the app's button sends), and after a single key
//   offers ACTIVATE IN AMF X1: http://localhost:8765/#license=<key> (the key stays in the # part, which is never sent to any server).
//   --open            also opens the page in the default browser (the double-click launchers use it)
// VENDOR only: when no key pair exists yet, the page offers to create one (the same as `node scripts/license.js init`): the private key is written
// to the key directory on THIS computer (0600) and the public half into bridge/license-public.json and index.html, which are then committed and
// rebuilt. It never overwrites an existing key pair. The sandbox / admin key pairs are still made with their own "init" commands.
// PIN LOCK (vendor): the key folder is created locked (private.pem.enc, scripts/license-vault.js). The page asks for the PIN once; the unlocked key is
// held in THIS process's memory only, and dropped by LOCK, after 15 minutes without use, or when the generator stops. 5 wrong PINs in a row lock the
// page out for 60 s, and every wrong PIN waits 1 s. A plain private.pem from before keeps working, and the page offers to lock it.
// LOCAL OTA (vendor): the same process is a small update server for the app, on the same 127.0.0.1:8799 address:
//   GET  /update.json        the signed manifest made with PUBLISH UPDATE on the page (kept in <key folder>/ota/update.json; signing needs the unlocked key)
//   GET  /releases/<file>    the installers of that manifest (platforms/<os>/releases or releases/<os>), HEAD and Range supported
//   POST /request-license    the app's SUBMIT TO VENDOR: kept in <key folder>/license-requests.jsonl and listed in the page's INBOX (USE fills the form)
// These three answer any origin (the app runs as a file, on localhost:8765 or elsewhere on this computer) but only on 127.0.0.1 (the Host check stays).
// In the app: SETTINGS -> OTA -> http://127.0.0.1:8799/update.json; then SUBMIT TO VENDOR posts here as well.
// PUBLIC LISTENER (--public [port], default port 8780; the .exe turns it on): a SECOND server on all network interfaces that answers ONLY
// POST /request-license, GET /update.json and GET /releases/<published file>, rate-limited per address. Customers' SUBMIT TO VENDOR reaches
// it at http://<your public IP>:8780 once the router forwards that port to this computer. The key page, unlock, issue and publish
// never answer there: they stay on 127.0.0.1:8799.
// VENDOR ADDRESS: the page asks api.ipify.org for this computer's public IP and PUBLISH VENDOR ADDRESS signs {server: "http://<ip>:8780"}
// with the vendor key into <project>/releases/vendor.json (commit + push it) and <key folder>/ota/vendor.json (served at /vendor.json). The app
// fetches it from GitHub, checks the signature with the public key it carries, and posts SUBMIT TO VENDOR there. A new IP: publish again.
// --root <project folder>: where index.html / bridge/license-public.json / the release folders are (default: this project, or the .exe's folder).
// Several keys at once: "How many" > 1 returns that many keys (random serials) and a CSV to download.
const http = require('node:http');
const crypto = require('node:crypto');
const lic = require('../bridge/license');
const { originAllowed, hostAllowed } = require('../bridge/server');

const vault = require('./license-vault');
const fs = require('node:fs');
const path = require('node:path');
const layout = require('./layout');
const IDLE_MS = 15 * 60 * 1000, MAX_TRIES = 5, LOCKOUT_MS = 60 * 1000;
const KINDS = {
  vendor: { mod: require('./license.js'), dirFn: 'vendorDir', label: 'VENDOR', needsName: false, canInit: true },   // no name: "Customer <serial>"
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
<header><h1>AUDIO <b>MIXER</b></h1><span><span class="pill">${k.label} KEY</span> <button type="button" id="lockbtn" class="pill" hidden>LOCK</button></span></header>
<main>
  <section class="card" id="setup" hidden>
    <h2>NO SIGNING KEY ON THIS COMPUTER YET</h2>
    <p class="dim">Keys are signed with your private key. None was found in <span class="mono" id="keydir"></span>. Create one here: it stays on this computer only, encrypted with a PIN you choose.</p>
    <div class="row2"><div class="field"><label for="pin1">New PIN (6+ characters; longer is stronger)</label><input id="pin1" type="password" autocomplete="new-password"></div><div class="field"><label for="pin2">Same PIN again</label><input id="pin2" type="password" autocomplete="new-password"></div></div>
    <button class="btn" id="init" type="button">CREATE MY KEY PAIR</button>
    <div id="init-done" hidden><p><b>Done.</b> Back up this file now (USB drive or password manager, not a cloud folder and not the repository) and remember the PIN: without both, no more keys can be made for your builds. The file is useless without the PIN.</p><div id="privpath" class="mono"></div><p class="dim">The public half was written into bridge/license-public.json and index.html. Commit and push those two files, then rebuild the installers so the app trusts your keys.</p></div>
  </section>
  <section class="card" id="unlock-card" hidden>
    <h2>KEY FOLDER LOCKED</h2>
    <form id="uf"><div class="field"><label for="upin">PIN</label><input id="upin" type="password" autocomplete="current-password"></div><button class="btn" id="unlock" type="submit">UNLOCK</button></form>
    <p class="dim">The key stays unlocked in this generator only, until LOCK, 15 minutes without use, or closing the generator.</p>
  </section>
  <section class="card" id="plain-card" hidden>
    <h2>YOUR KEY FILE IS NOT PIN-LOCKED</h2>
    <p class="dim">private.pem is stored as plain text. Lock it with a PIN: it is encrypted and the plain file is removed.</p>
    <div class="row2"><div class="field"><label for="lpin1">New PIN</label><input id="lpin1" type="password" autocomplete="new-password"></div><div class="field"><label for="lpin2">Same PIN again</label><input id="lpin2" type="password" autocomplete="new-password"></div></div>
    <button class="btn" id="lockplain" type="button">LOCK WITH PIN</button>
  </section>
  <section class="card" id="gen">
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
      <div class="field"><label for="count">How many keys (1 = one key; more = a batch with random serials and a CSV)</label><input id="count" type="number" min="1" max="500" step="1" value="1"></div>
      <button class="btn" id="go" type="submit">GENERATE KEY</button>
    </form>
    <p class="err" id="err" hidden></p>
  </section>
  <section class="card" id="inbox-card" hidden>
    <h2>INBOX: KEY REQUESTS FROM THE APP</h2>
    <div id="inbox" class="mono"></div>
  </section>
  <section class="card" id="ota-card" hidden>
    <h2>LOCAL UPDATE SERVER (OTA)</h2>
    <p class="dim">In the app: SETTINGS &rarr; OTA &rarr; <span class="mono" id="ota-url"></span></p>
    <p id="ota-state" class="mono"></p>
    <div class="field"><label for="vaddr">Your public address for customers' key requests (router: forward TCP 8780 to this computer)</label><input id="vaddr" type="text" placeholder="looking up your public IP…"></div>
    <button class="btn" id="vpub" type="button">PUBLISH VENDOR ADDRESS</button>
    <p class="dim" id="vstate"></p>
    <label class="dim"><input type="checkbox" id="ota-repo"> also write releases/update.json in the project (for GitHub)</label>
    <button class="btn" id="publish" type="button">PUBLISH UPDATE</button>
  </section>
  <section class="card" id="result" hidden>
    <h2>SIGNED KEY</h2>
    <div id="key-out" class="mono"></div>
    <p class="dim" id="summary"></p>
    <div class="row2"><button class="btn" id="copy" type="button">COPY</button><button class="btn" id="csv" type="button" hidden>DOWNLOAD CSV</button></div>
    <p><a id="mailkey" class="btn" href="#" hidden>E-MAIL KEY TO CUSTOMER</a></p>
    <p><a id="activate" class="btn" href="#" target="_blank" rel="noopener noreferrer" hidden>ACTIVATE IN AMF X1 (THIS COMPUTER)</a></p>
  </section>
</main>
<script>
'use strict';
// Every field here is sent to POST /issue on this SAME local server and comes back as the signed key text; the key and the summary are
// written with textContent only, no HTML is ever parsed.
(function () {
  var $ = function (id) { return document.getElementById(id); };
  var csvText = '';
  // filled in from the address: the app's GENERATOR button sends ?plan=&name=&email=&machine=
  try { var q = new URLSearchParams(location.search); ['plan', 'name', 'email', 'machine'].forEach(function (k) { var v = q.get(k); if (v && $(k)) { if (k !== 'plan' || $(k).querySelector('option[value="' + v.replace(/[^a-z]/g, '') + '"]')) $(k).value = v.slice(0, 120); } }); } catch (e) { /* old browser */ }
  var post = function (url, body) { return fetch(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body || {}) }).then(function (r) { return r.json(); }); };
  var showErr = function (t) { $('err').textContent = t; $('err').hidden = false; };
  var refresh = function () {
    return fetch('/status').then(function (r) { return r.json(); }).then(function (j) {
      if (!j || !j.ok) return;
      $('err').hidden = true;
      var needInit = !j.hasKey, needUnlock = j.hasKey && j.locked && !j.unlocked;
      $('keydir').textContent = j.dir; $('setup').hidden = !needInit; if (!j.canInit) $('init').hidden = true;
      $('unlock-card').hidden = !needUnlock; $('plain-card').hidden = !(j.hasKey && !j.locked && j.canInit);
      $('gen').hidden = needInit && j.canInit || needUnlock; $('lockbtn').hidden = !(j.locked && j.unlocked);
      if (needUnlock) $('upin').focus();
    }).catch(function () { /* the form still works */ });
  };
  refresh();
  $('init').addEventListener('click', function () {
    if ($('pin1').value !== $('pin2').value) return showErr('The two PINs differ.');
    $('init').disabled = true;
    post('/init', { pin: $('pin1').value }).then(function (j) {
      $('pin1').value = $('pin2').value = '';
      if (!j || !j.ok) { showErr((j && j.error) || 'Could not create the key pair.'); $('init').disabled = false; return; }
      $('privpath').style.whiteSpace = 'pre-wrap'; $('privpath').textContent = j.priv + (j.embedded ? '' : '\\n\\nNo AmF X1 project here: put this public key into bridge/license-public.json (and index.html, between LICENSE_PUBLIC_JWK) of your project, or run the generator from the project with --root:\\n' + j.publicKey); $('init-done').hidden = false; $('init').hidden = true; $('gen').hidden = false; $('lockbtn').hidden = false;
    }).catch(function () { showErr('Could not reach this server.'); $('init').disabled = false; });
  });
  $('uf').addEventListener('submit', function (ev) {
    ev.preventDefault(); $('unlock').disabled = true; $('unlock').textContent = 'CHECKING…';
    post('/unlock', { pin: $('upin').value }).then(function (j) {
      $('upin').value = '';
      if (!j || !j.ok) showErr((j && j.error) || 'Could not unlock.'); else refresh();
    }).catch(function () { showErr('Could not reach this server.'); }).then(function () { $('unlock').disabled = false; $('unlock').textContent = 'UNLOCK'; });
  });
  $('lockplain').addEventListener('click', function () {
    if ($('lpin1').value !== $('lpin2').value) return showErr('The two PINs differ.');
    post('/lock-file', { pin: $('lpin1').value }).then(function (j) { $('lpin1').value = $('lpin2').value = ''; if (!j || !j.ok) showErr((j && j.error) || 'Could not lock.'); else refresh(); });
  });
  var fill = function (r) { if (r.plan && $('plan').querySelector('option[value="' + String(r.plan).replace(/[^a-z]/g, '') + '"]')) $('plan').value = r.plan; $('name').value = r.name || ''; $('email').value = r.email || ''; $('machine').value = r.machineId || ''; $('count').value = 1; window.scrollTo(0, 0); };
  var loadOta = function () {
    fetch('/ota/status').then(function (r) { return r.json(); }).then(function (j) {
      if (!j || !j.ok) return;
      $('ota-card').hidden = false; $('ota-url').textContent = location.origin + '/update.json';
      $('ota-state').textContent = (j.published ? 'Published: ' + j.published.version + ' (' + j.published.files + ' files, ' + j.published.at + ')' : 'Nothing published yet.') + ' Project version: ' + j.version + ', ' + j.found + ' installer(s) found.';
      var box = $('inbox'); box.textContent = ''; $('inbox-card').hidden = !j.requests.length;
      j.requests.forEach(function (r) {
        var row = document.createElement('div'); row.style.cssText = 'display:flex;gap:8px;align-items:center;justify-content:space-between;border-bottom:1px solid var(--line);padding:6px 0';
        var t = document.createElement('span'); t.textContent = (r.at ? new Date(r.at).toISOString().slice(0, 16).replace('T', ' ') + '  ' : '') + String(r.plan || '').toUpperCase() + '  ' + (r.name || '(no name)') + '  ' + (r.email || '') + '  ' + (r.machineId || '');
        var b = document.createElement('button'); b.type = 'button'; b.className = 'pill'; b.textContent = 'USE'; b.addEventListener('click', function () { fill(r); });
        row.appendChild(t); row.appendChild(b); box.appendChild(row);
      });
    }).catch(function () { /* not the vendor kind */ });
  };
  loadOta();
  fetch('/vendor/ip').then(function (r) { return r.json(); }).then(function (j) { if (j && j.ip && !$('vaddr').value) $('vaddr').value = 'http://' + (j.ip.indexOf(':') >= 0 ? '[' + j.ip + ']' : j.ip) + ':8780'; else if (!$('vaddr').value) $('vaddr').placeholder = 'http://<your public IP>:8780'; }).catch(function () { /* offline */ });
  $('vpub').addEventListener('click', function () {
    post('/vendor/publish', { server: $('vaddr').value.trim() }).then(function (j) {
      if (!j || !j.ok) { showErr((j && j.error) || 'Could not publish.'); if (j && j.locked) refresh(); return; }
      $('err').hidden = true; $('vstate').textContent = 'Published ' + j.server + (j.repoFile ? '. Commit and push ' + j.repoFile + ' so every copy of the app finds it.' : '. (No project folder here: start the generator with --root <project> to write releases/vendor.json.)');
    });
  });
  $('publish').addEventListener('click', function () {
    $('publish').disabled = true;
    post('/ota/publish', { repo: $('ota-repo').checked }).then(function (j) { if (!j || !j.ok) { showErr((j && j.error) || 'Could not publish.'); if (j && j.locked) refresh(); } else { $('err').hidden = true; loadOta(); } }).catch(function () { showErr('Could not reach this server.'); }).then(function () { $('publish').disabled = false; });
  });
  $('lockbtn').addEventListener('click', function () { post('/lock', {}).then(function () { $('result').hidden = true; refresh(); }); });
  $('f').addEventListener('submit', function (ev) {
    ev.preventDefault();
    $('err').hidden = true; $('go').disabled = true; $('go').textContent = 'GENERATING…';
    var body = { plan: $('plan').value, name: $('name').value, email: $('email').value, machine: $('machine').value, days: Number($('days').value) || 0, seats: Number($('seats').value) || 1, count: Number($('count').value) || 1 };
    fetch('/issue', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) })
      .then(function (r) { return r.json().then(function (j) { return { ok: r.ok, j: j }; }); })
      .then(function (res) {
        if (!res.ok || !res.j || !res.j.ok) { $('err').textContent = (res.j && res.j.error) || 'Could not generate the key.'; $('err').hidden = false; if (res.j && res.j.locked) refresh(); return; }
        $('key-out').textContent = res.j.keys ? res.j.keys.map(function (x) { return x.id + '  ' + x.key; }).join('\\n') : res.j.key;
        $('summary').textContent = res.j.summary;
        csvText = res.j.csv || ''; $('csv').hidden = !csvText;
        $('activate').hidden = !!res.j.keys; if (!res.j.keys) $('activate').href = 'http://localhost:8765/#license=' + encodeURIComponent(res.j.key);
        // send the key to the customer: an e-mail draft to the address on the form, with the key and how to activate it
        var to = $('email').value.trim();
        $('mailkey').hidden = !!res.j.keys || !/^[^@\\s]+@[^@\\s]+\\.[^@\\s]+$/.test(to);
        if (!$('mailkey').hidden) {
          var who = $('name').value.trim(), plan = $('plan').options[$('plan').selectedIndex].text;
          var body = 'Hello' + (who ? ' ' + who : '') + ',\\n\\nhere is your AmF X1 license key (' + plan + '):\\n\\n' + res.j.key + '\\n\\nTo activate: open AmF X1, go to SYSTEM > LICENSE, paste the key into the LICENSE KEY box and press ACTIVATE.\\n' + ($('machine').value.trim() ? 'The key works on the computer with machine code ' + $('machine').value.trim().toUpperCase() + '.\\n' : '') + '\\nAMFB Audio\\n';
          $('mailkey').href = 'mailto:' + encodeURIComponent(to) + '?subject=' + encodeURIComponent('Your AmF X1 license key') + '&body=' + encodeURIComponent(body);
        }
        $('result').hidden = false;
      })
      .catch(function () { $('err').textContent = 'Could not reach this server.'; $('err').hidden = false; })
      .then(function () { $('go').disabled = false; $('go').textContent = 'GENERATE KEY'; });
  });
  $('copy').addEventListener('click', function () {
    navigator.clipboard.writeText(csvText || $('key-out').textContent).then(function () { $('copy').textContent = 'COPIED'; setTimeout(function () { $('copy').textContent = 'COPY'; }, 1200); });
  });
  $('csv').addEventListener('click', function () {
    var a = document.createElement('a'); a.href = URL.createObjectURL(new Blob([csvText], { type: 'text/csv' })); a.download = 'amfx1-license-keys-' + new Date().toISOString().slice(0, 10) + '.csv';
    document.body.appendChild(a); a.click(); setTimeout(function () { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
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

function issueKey(kind, dirArgv, body, session) {
  const k = KINDS[kind];
  const plan = body.plan, name = clean(body.name, 80), email = clean(body.email, 120), machine = clean(body.machine, 40);
  const days = Number(body.days) || 0, seats = Math.max(1, Number(body.seats) || 1);
  if (!lic.PLANS[plan]) { const e = new Error('plan must be one of: ' + Object.keys(lic.PLANS).join(', ')); e.status = 400; throw e; }
  if (k.needsName && !name) { const e = new Error('name is required for a ' + k.label.toLowerCase() + ' key'); e.status = 400; throw e; }
  const dir = k.mod[k.dirFn](dirArgv);
  const privateKey = session ? session.key(dir) : undefined;
  const r = k.mod.issue({ dir, privateKey, plan, name, email: email || undefined, machine: machine || undefined, days, seats, now: Date.now() });
  const p = r.payload;
  const summary = `${k.label} ${lic.PLANS[p.plan].name} for ${p.name || '(no name)'}, ${p.expires ? 'expires ' + new Date(p.expires).toISOString().slice(0, 10) : 'no expiry'}${p.mid ? ', bound to ' + p.mid : ', not bound to a computer'}`;
  const count = Math.floor(Number(body.count) || 1);
  if (count > 1) {
    if (count > 500) { const e = new Error('at most 500 keys at a time'); e.status = 400; throw e; }
    const keys = [{ id: p.id, key: r.key, name: p.name }];
    for (let i = 1; i < count; i++) {   // a batch: random serials, names only when one was given (then numbered)
      const x = k.mod.issue({ dir, privateKey, plan, name: name ? name + ' #' + (i + 1) : '', email: email || undefined, machine: machine || undefined, days, seats, now: Date.now() });
      keys.push({ id: x.payload.id, key: x.key, name: x.payload.name });
    }
    if (name) keys[0].name = name + ' #1';
    const q = v => '"' + String(v).replace(/"/g, '""') + '"';
    const csv = 'id,plan,name,expires,key\n' + keys.map(x => [x.id, plan, q(x.name || ''), p.expires ? new Date(p.expires).toISOString().slice(0, 10) : '', x.key].join(',')).join('\n') + '\n';
    return { keys, csv, summary: `${count} ${k.label} ${lic.PLANS[plan].name} keys, ${p.expires ? 'expire ' + new Date(p.expires).toISOString().slice(0, 10) : 'no expiry'}${p.mid ? ', bound to ' + p.mid : ', not bound to a computer'}` };
  }
  return { key: r.key, summary };
}

function keyStatus(kind, dirArgv, session) {
  const k = KINDS[kind], dir = k.mod[k.dirFn](dirArgv);
  const hasKey = k.canInit ? vault.hasKey(dir) : require('node:fs').existsSync(require('node:path').join(dir, 'private.pem'));
  return { hasKey, locked: !!k.canInit && vault.isLocked(dir), unlocked: !!(session && session.isOpen()), dir, canInit: !!k.canInit };
}
// The unlocked key of a PIN-locked folder, in memory only. Plain folders do not need it (key(dir) then returns undefined and issue() reads the file).
function makeSession(now = () => Date.now()) {
  let key = null, used = 0, fails = 0, until = 0;
  const s = {
    isOpen() { if (key && now() - used > IDLE_MS) key = null; return !!key; },
    close() { key = null; },
    unlock(dir, pin) {
      if (now() < until) { const e = new Error('too many wrong PINs: wait ' + Math.ceil((until - now()) / 1000) + ' s'); e.status = 429; throw e; }
      try { key = vault.loadKey(dir, String(pin || '')); used = now(); fails = 0; }
      catch (e) { if (++fails >= MAX_TRIES) { fails = 0; until = now() + LOCKOUT_MS; } e.status = e.status || 401; throw e; }
    },
    key(dir) {
      if (!vault.isLocked(dir)) return undefined;
      if (!s.isOpen()) { const e = new Error('the key folder is locked: unlock it with your PIN'); e.status = 401; e.locked = true; throw e; }
      used = now(); return key;
    },
  };
  return s;
}
function initKind(kind, dirArgv, root, pin, session) {
  const k = KINDS[kind];
  if (!k.canInit) { const e = new Error('create this key pair with its own "init" command'); e.status = 400; throw e; }
  const dir = k.mod[k.dirFn](dirArgv);
  vault.checkPin(pin);   // a new vendor key pair is always PIN-locked from the page
  let r; try { r = k.mod.initKeys(dir, { pin }); } catch (e) { e.status = e.status || 409; throw e; }   // never overwrites an existing key
  if (session) session.unlock(dir, pin);
  const jwk = k.mod.loadPublic(dir);
  // in a project: the public key is written into the app (index.html, bridge/license-public.json); without one (the .exe alone) it is shown to copy
  const where = root || layout.ROOT;
  if (fs.existsSync(path.join(where, 'index.html')) && fs.existsSync(path.join(where, 'bridge'))) { k.mod.embedPublic(jwk, where); return { priv: r.priv, embedded: true }; }
  return { priv: r.priv, embedded: false, publicKey: JSON.stringify(jwk) };
}

// ---- local OTA -----------------------------------------------------------------------------------------------------------------------------------
const otaDir = dir => path.join(dir, 'ota');
function readJsonl(f, limit) { try { return fs.readFileSync(f, 'utf8').split('\n').filter(Boolean).slice(-limit).reverse().map(l => { try { return JSON.parse(l); } catch (_) { return null; } }).filter(Boolean); } catch (_) { return []; } }
function otaStatus(dir, root, releases) {
  const r = root || layout.ROOT; let version = '?'; try { version = JSON.parse(fs.readFileSync(path.join(r, 'package.json'), 'utf8')).version; } catch (_) { /* none */ }
  let published = null; try { const env = JSON.parse(fs.readFileSync(path.join(otaDir(dir), 'update.json'), 'utf8')), m = JSON.parse(env.payload); published = { version: m.version, files: Object.keys(m.files).length, at: m.released }; } catch (_) { /* none yet */ }
  let found = 0; try { found = Object.keys(require('./make-update').build({ version, base: 'x', releases }).files).length; } catch (_) { /* none */ }
  return { version, published, found, requests: readJsonl(path.join(dir, 'license-requests.jsonl'), 50) };
}
// this computer's public address as the internet sees it (a small public API; nothing is sent but the request itself)
async function publicIp(fetchImpl = globalThis.fetch) {
  for (const u of ['https://api.ipify.org?format=json', 'https://api64.ipify.org?format=json']) {
    try { const c = new AbortController(), t = setTimeout(() => c.abort(), 5000); const r = await fetchImpl(u, { signal: c.signal }); clearTimeout(t); const j = await r.json(); if (j && /^[0-9a-f.:]+$/i.test(j.ip || '')) return j.ip; } catch (_) { /* next */ }
  }
  return null;
}
function vendorPublish(dir, root, privateKey, server) {
  if (!/^https?:\/\/[\w.:\[\]-]+$/.test(server)) { const e = new Error('not an address: ' + server); e.status = 400; throw e; }
  const { signManifest } = require('../bridge/update');
  const env = signManifest({ product: 'audio-mixer', kind: 'vendor', server, at: new Date().toISOString(), version: 'vendor', files: {} }, privateKey);
  fs.mkdirSync(otaDir(dir), { recursive: true, mode: 0o700 });
  fs.writeFileSync(path.join(otaDir(dir), 'vendor.json'), JSON.stringify(env, null, 2) + '\n');
  const r = root || layout.ROOT, wrote = fs.existsSync(path.join(r, 'releases'));
  if (wrote) fs.writeFileSync(path.join(r, 'releases', 'vendor.json'), JSON.stringify(env, null, 2) + '\n');
  return { server, repoFile: wrote ? path.join(r, 'releases', 'vendor.json') : null };
}
function otaPublish(dir, root, releases, privateKey, base, alsoRepo) {
  const r = root || layout.ROOT, version = JSON.parse(fs.readFileSync(path.join(r, 'package.json'), 'utf8')).version;
  const { signManifest } = require('../bridge/update');
  const local = require('./make-update').build({ version, base: base + '/releases', flatUrls: true, releases });
  fs.mkdirSync(otaDir(dir), { recursive: true, mode: 0o700 });
  fs.writeFileSync(path.join(otaDir(dir), 'update.json'), JSON.stringify(signManifest(local, privateKey), null, 2) + '\n');
  if (alsoRepo) {   // the same release for the app's default source (GitHub): links to the files where the repository keeps them
    const gh = require('./make-update').build({ version, base: 'https://raw.githubusercontent.com/amirf1389/audio-mixer/main', releases });
    fs.writeFileSync(path.join(r, 'releases', 'update.json'), JSON.stringify(signManifest(gh, privateKey), null, 2) + '\n');
  }
  return { version, files: Object.keys(local.files).length };
}
// the file a published manifest names: from the flat releases folder (tests) or the project's release folders
function otaFile(dir, releases, name) {
  if (!/^[\w .+()-]+$/.test(name) || name.includes('..')) return null;
  let env; try { env = JSON.parse(fs.readFileSync(path.join(otaDir(dir), 'update.json'), 'utf8')); } catch (_) { return null; }
  const m = JSON.parse(env.payload);
  if (!Object.values(m.files).some(f => f.name === name)) return null;   // only what was published, never any other file
  const f = releases ? path.join(releases, name) : layout.findRelease(name);
  return f && fs.existsSync(f) ? f : null;
}

// per-address request budget for the public listener: 20 requests a minute (requests are tiny; this only stops floods)
function rateLimiter(perMin = 20, now = () => Date.now()) {
  const seen = new Map();
  return ip => { const t = now(), w = (seen.get(ip) || []).filter(x => t - x < 60000); w.push(t); seen.set(ip, w); if (seen.size > 5000) seen.clear(); return w.length <= perMin; };
}

function createServer({ kind = 'vendor', dirArgv = [], root, session = makeSession(), releases, publicOnly = false, limit = rateLimiter() } = {}) {
  if (!KINDS[kind]) throw new Error('kind must be one of: ' + Object.keys(KINDS).join(', '));
  const send = (res, code, body, headers = {}) => { const t = typeof body === 'string' ? body : JSON.stringify(body); res.writeHead(code, { 'Content-Type': typeof body === 'string' ? 'text/html; charset=utf-8' : 'application/json; charset=utf-8', 'X-Content-Type-Options': 'nosniff', 'Cache-Control': 'no-store', 'X-Frame-Options': 'DENY', 'Referrer-Policy': 'no-referrer', ...headers }); res.end(t); };
  const fail = (res, code, error) => send(res, code, { ok: false, error });

  return http.createServer(async (req, res) => {
    try {
      const url = new URL(req.url, 'http://x');
      if (publicOnly) {   // the internet-facing listener: only the three public routes, nothing about keys
        if (!(url.pathname === '/update.json' || url.pathname === '/vendor.json' || url.pathname.startsWith('/releases/') || url.pathname === '/request-license')) return fail(res, 404, 'not found');
        if (!limit(req.socket.remoteAddress || '?')) return fail(res, 429, 'too many requests');
      } else if (!hostAllowed(req.headers.host)) return fail(res, 403, 'host not allowed');
      // the public part of the local update server: any origin (the app is a file, localhost:8765, ...), still only this computer (Host check above)
      const pub = KINDS[kind].canInit && (publicOnly || url.pathname === '/vendor.json' || url.pathname === '/update.json' || url.pathname.startsWith('/releases/') || url.pathname === '/request-license');
      if (pub) {
        const cors = { 'Access-Control-Allow-Origin': '*', 'Access-Control-Allow-Methods': 'GET, HEAD, POST, OPTIONS', 'Access-Control-Allow-Headers': 'Content-Type' };
        const dir = KINDS[kind].mod[KINDS[kind].dirFn](dirArgv);
        if (req.method === 'OPTIONS') { res.writeHead(204, cors); return res.end(); }
        if (url.pathname === '/vendor.json' && req.method === 'GET') {
          const f = path.join(otaDir(dir), 'vendor.json'); if (!fs.existsSync(f)) return send(res, 404, { ok: false, error: 'no vendor address published' }, cors);
          return send(res, 200, fs.readFileSync(f, 'utf8'), { ...cors, 'Content-Type': 'application/json; charset=utf-8' });
        }
        if (url.pathname === '/update.json' && (req.method === 'GET' || req.method === 'HEAD')) {
          const f = path.join(otaDir(dir), 'update.json'); if (!fs.existsSync(f)) return send(res, 404, { ok: false, error: 'nothing published yet' }, cors);
          return send(res, 200, fs.readFileSync(f, 'utf8'), { ...cors, 'Content-Type': 'application/json; charset=utf-8' });
        }
        if (url.pathname.startsWith('/releases/') && (req.method === 'GET' || req.method === 'HEAD')) {
          const f = otaFile(dir, releases, decodeURIComponent(url.pathname.slice(10))); if (!f) return send(res, 404, { ok: false, error: 'not found' }, cors);
          const size = fs.statSync(f).size, m = /^bytes=(\d+)-(\d*)$/.exec(req.headers.range || '');
          const start = m ? Number(m[1]) : 0, end = m && m[2] ? Math.min(Number(m[2]), size - 1) : size - 1;
          if (m && start >= size) { res.writeHead(416, { ...cors, 'Content-Range': 'bytes */' + size }); return res.end(); }
          res.writeHead(m ? 206 : 200, { ...cors, 'Content-Type': 'application/octet-stream', 'Content-Length': end - start + 1, 'Accept-Ranges': 'bytes', 'Content-Disposition': 'attachment; filename="' + path.basename(f).replace(/"/g, '') + '"', ...(m ? { 'Content-Range': `bytes ${start}-${end}/${size}` } : {}) });
          if (req.method === 'HEAD') return res.end();
          return fs.createReadStream(f, { start, end }).pipe(res);
        }
        if (url.pathname === '/request-license' && req.method === 'POST') {
          const chunks = []; let n = 0; for await (const c of req) { n += c.length; if (n > 8192) return send(res, 413, { ok: false, error: 'request too large' }, cors); chunks.push(c); }
          let b; try { b = JSON.parse(Buffer.concat(chunks).toString('utf8')); } catch (_) { return send(res, 400, { ok: false, error: 'the body is not JSON' }, cors); }
          if (!b || b.kind !== 'license-request' || !lic.PLANS[b.plan]) return send(res, 400, { ok: false, error: 'not a license request' }, cors);
          const rec = { at: Date.now(), plan: b.plan, name: clean(b.name, 80), email: clean(b.email, 120), machineId: clean(b.machineId, 40), platform: clean(b.platform, 40), version: clean(b.version, 20) };
          if (readJsonl(path.join(dir, 'license-requests.jsonl'), 1000).length >= 1000) return send(res, 429, { ok: false, error: 'the inbox is full' }, cors);
          fs.mkdirSync(dir, { recursive: true, mode: 0o700 }); fs.appendFileSync(path.join(dir, 'license-requests.jsonl'), JSON.stringify(rec) + '\n', { mode: 0o600 });
          return send(res, 200, { ok: true, emailed: false }, cors);
        }
        return send(res, 405, { ok: false, error: 'method not allowed' }, cors);
      }
      if (!originAllowed(req.headers.origin)) return fail(res, 403, 'origin not allowed');
      if (req.method === 'GET' && url.pathname === '/') { const p = pageFor(kind); return send(res, 200, p.body, { 'Content-Security-Policy': p.csp }); }
      if (req.method === 'GET' && url.pathname === '/ota/status' && KINDS[kind].canInit) return send(res, 200, { ok: true, ...otaStatus(KINDS[kind].mod[KINDS[kind].dirFn](dirArgv), root, releases) });
      if (req.method === 'GET' && url.pathname === '/vendor/ip' && KINDS[kind].canInit) return send(res, 200, { ok: true, ip: await publicIp() });
      if (req.method === 'POST' && url.pathname === '/vendor/publish' && KINDS[kind].canInit) {
        const dir = KINDS[kind].mod[KINDS[kind].dirFn](dirArgv); let b = {}; try { const chunks = []; for await (const c of req) chunks.push(c); b = JSON.parse(Buffer.concat(chunks).toString('utf8') || '{}'); } catch (_) { /* defaults */ }
        try { const key = session.key(dir) || vault.loadKey(dir); return send(res, 200, { ok: true, ...vendorPublish(dir, root, key, String(b.server || '')) }); }
        catch (e) { return e.locked ? send(res, 401, { ok: false, error: e.message, locked: true }) : fail(res, e.status || 500, e.message); }
      }
      if (req.method === 'POST' && url.pathname === '/ota/publish' && KINDS[kind].canInit) {
        const dir = KINDS[kind].mod[KINDS[kind].dirFn](dirArgv); let b = {}; try { const chunks = []; for await (const c of req) chunks.push(c); b = JSON.parse(Buffer.concat(chunks).toString('utf8') || '{}'); } catch (_) { /* defaults */ }
        try { const key = session.key(dir) || vault.loadKey(dir); return send(res, 200, { ok: true, ...otaPublish(dir, root, releases, key, 'http://127.0.0.1:' + req.socket.localPort, !!b.repo) }); }
        catch (e) { return e.locked ? send(res, 401, { ok: false, error: e.message, locked: true }) : fail(res, e.status || 500, e.message); }
      }
      if (req.method === 'GET' && url.pathname === '/status') return send(res, 200, { ok: true, ...keyStatus(kind, dirArgv, session) });
      const readJson = async () => { const chunks = []; let n = 0; for await (const c of req) { n += c.length; if (n > 8192) return null; chunks.push(c); } try { return JSON.parse(Buffer.concat(chunks).toString('utf8') || '{}'); } catch (_) { return null; } };
      const dirNow = () => KINDS[kind].mod[KINDS[kind].dirFn](dirArgv);
      if (req.method === 'POST' && ['/init', '/unlock', '/lock', '/lock-file'].includes(url.pathname)) {
        const b = await readJson(); if (!b) return fail(res, 400, 'the body is not JSON');
        try {
          if (url.pathname === '/init') return send(res, 200, { ok: true, ...initKind(kind, dirArgv, root, b.pin, session) });
          if (!KINDS[kind].canInit) return fail(res, 400, 'not for this key kind');
          if (url.pathname === '/unlock') {
            try { session.unlock(dirNow(), b.pin); } catch (e) { if (e.code === 'WRONG_PIN') await new Promise(r => setTimeout(r, 1000)); throw e; }   // a wrong PIN costs a second
            return send(res, 200, { ok: true });
          }
          if (url.pathname === '/lock') { session.close(); return send(res, 200, { ok: true }); }
          vault.lock(dirNow(), b.pin); session.unlock(dirNow(), b.pin); return send(res, 200, { ok: true });   // /lock-file: encrypt a plain private.pem
        } catch (e) { return fail(res, e.status || 500, e.message); }
      }
      if (req.method === 'POST' && url.pathname === '/issue') {
        const chunks = []; let n = 0;
        for await (const c of req) { n += c.length; if (n > 8192) return fail(res, 413, 'request too large'); chunks.push(c); }
        let body; try { body = JSON.parse(Buffer.concat(chunks).toString('utf8')); } catch (e) { return fail(res, 400, 'the body is not JSON'); }
        try { const r = issueKey(kind, dirArgv, body || {}, KINDS[kind].canInit ? session : null); return send(res, 200, { ok: true, ...r }); } catch (e) { return e.locked ? send(res, 401, { ok: false, error: e.message, locked: true }) : fail(res, e.status || 500, e.message); }
      }
      return fail(res, 404, 'not found');
    } catch (e) { fail(res, 500, e.message); }
  });
}

function main(argv) {
  const kind = arg(argv, '--kind', 'vendor');
  if (!KINDS[kind]) { console.error('--kind must be one of: ' + Object.keys(KINDS).join(', ')); return 2; }
  const want = arg(argv, '--port', '8799'), port = want === '0' ? 0 : Number(want) || 8799;
  let root = arg(argv, '--root'); if (!root) root = fs.existsSync(path.join(layout.ROOT, 'package.json')) ? layout.ROOT : (process.execPath && /generator/i.test(path.basename(process.execPath)) ? path.dirname(process.execPath) : process.cwd());
  const server = createServer({ kind, dirArgv: argv, root });
  if (argv.includes('--public') && KINDS[kind].canInit) {
    const pv = argv[argv.indexOf('--public') + 1], pport = /^\d+$/.test(pv || '') ? Number(pv) : 8780;
    const ps = createServer({ kind, dirArgv: argv, root, publicOnly: true });
    ps.on('error', e => console.error(`Public listener on port ${pport} could not start: ${e.message}`));
    ps.listen(pport, '0.0.0.0', () => console.log(`Public requests and updates on port ${pport} (all networks). Forward TCP ${pport} on your router to this computer; customers then use http://<your public IP>:${pport}`));
  }
  server.once('error', e => { if (e.code === 'EADDRINUSE' && port) { console.error(`Port ${port} is in use: using a free port instead (the app's GENERATOR button will not find this one).`); server.listen(0, '127.0.0.1'); } else { console.error('Error: ' + e.message); process.exit(1); } });
  server.on('listening', () => {
    const url = `http://127.0.0.1:${server.address().port}/`;
    console.log(`Audio Mixer license key generator (${KINDS[kind].label}) on ${url} — open it in a browser on this computer. Ctrl+C to stop.`);
    if (argv.includes('--open') && !server.opened) { server.opened = true;
      const { spawn } = require('node:child_process');
      const [cmd, args] = process.platform === 'win32' ? ['cmd', ['/c', 'start', '', url]] : process.platform === 'darwin' ? ['open', [url]] : ['xdg-open', [url]];
      try { spawn(cmd, args, { stdio: 'ignore', detached: true }).unref(); } catch (_) { /* open it by hand */ }
    }
  });
  server.listen(port, '127.0.0.1');
  return 0;
}

if (require.main === module) {
  const code = main(process.argv.slice(2));
  if (code !== 0) process.exit(code);   // 0 means the server is listening (asynchronously): exiting now would kill it before it ever serves a request
}
module.exports = { publicIp, vendorPublish, rateLimiter, otaStatus, otaPublish, KINDS, page, pageFor, issueKey, keyStatus, initKind, makeSession, createServer, main };
