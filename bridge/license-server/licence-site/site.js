'use strict';
// AmF X1 licence site: one script for every page (body[data-page]). It talks only to this origin: the public licence routes (/status,
// /deactivate, /redeem, /activate-online, /licence/api/*), the request route (/request-license) and the signed update list (/update.json).
// Keys typed here live in this page's memory only: nothing is written to browser storage or cookies. Text from the server is
// always put in with textContent, never as HTML.
(function () {
  var WHATSAPP = '601120871185';
  var PLAN_NAMES = { basic: 'Basic', pro: 'Pro', studio: 'Studio', master: 'Studio Master', ultimate: 'Ultimate' };
  var MACHINE_RE = /^[A-Z0-9-]{8,64}$/;
  var EMAIL_RE = /^[^\s@<>"',;]{1,64}@[^\s@<>"',;]{1,185}\.[A-Za-z]{2,24}$/;
  var $ = function (id) { return document.getElementById(id); };
  var el = function (tag, cls, text) { var e = document.createElement(tag); if (cls) e.className = cls; if (text !== undefined && text !== null) e.textContent = String(text); return e; };

  // ?machine=XXXX-…&edition=pro (MANAGE LICENCE ONLINE in the console and the Android app): checked, then kept on every link of the site
  var q = new URLSearchParams(location.search);
  var machine = String(q.get('machine') || '').trim().toUpperCase(); if (!MACHINE_RE.test(machine)) machine = '';
  var edition = String(q.get('edition') || '').trim().toLowerCase(); if (!Object.prototype.hasOwnProperty.call(PLAN_NAMES, edition)) edition = '';
  var carry = new URLSearchParams(); if (machine) carry.set('machine', machine); if (edition) carry.set('edition', edition);
  if (carry.toString()) document.querySelectorAll('a[href^="/licence/"]').forEach(function (a) { var u = new URL(a.getAttribute('href'), location.origin); u.search = carry.toString(); a.setAttribute('href', u.pathname + u.search); });
  document.querySelectorAll('input[data-machine]').forEach(function (i) { if (machine && !i.value) i.value = machine; });

  function say(node, text, kind) { node.textContent = text || ''; node.className = 'msg' + (kind ? ' ' + kind : ''); }
  function invalid(input, bad) { input.setAttribute('aria-invalid', bad ? 'true' : 'false'); if (bad) input.focus(); return bad; }
  function post(path, body) {
    return fetch(path, { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify(body), credentials: 'same-origin', cache: 'no-store' })
      .then(function (r) { return r.text().then(function (t) { var j = null; try { j = JSON.parse(t); } catch (_) { /* not JSON */ } return { status: r.status, json: j || {} }; }); })
      .catch(function () { return { status: 0, json: { ok: false, error: 'the server could not be reached: check the connection and try again' } }; });
  }
  function errorOf(r) { if (r.status === 429) return 'Too many tries from this connection: wait a minute and try again.'; return (r.json && r.json.error) ? String(r.json.error).charAt(0).toUpperCase() + String(r.json.error).slice(1) + '.' : 'Something went wrong (' + r.status + ').'; }
  function day(ms) { return ms ? new Date(ms).toISOString().slice(0, 10) : ''; }
  function planName(p) { return PLAN_NAMES[p] || String(p || '').toUpperCase(); }
  function kindOf(text) {
    var t = String(text || '').trim();
    if (/^AMIX1\.[A-Za-z0-9_\-.]+$/.test(t)) return { key: t };
    if (/^AMFXE1\.[A-Za-z0-9_\-.]+$/.test(t)) return { token: t };
    if (t.replace(/[^0-9A-Za-z]/g, '').length === 25 && /^[0-9A-Za-z\s-]+$/.test(t)) return { code: t };
    return null;
  }
  function facts(dl, rows) { dl.textContent = ''; rows.forEach(function (r) { dl.appendChild(el('dt', '', r[0])); var dd = el('dd'); if (r[1] instanceof Node) dd.appendChild(r[1]); else dd.textContent = r[1]; dl.appendChild(dd); }); }
  function stateTag(v) { var s = v.revoked ? 'revoked' : v.expired ? 'expired' : 'active'; return el('span', 'state ' + s, s); }

  var page = document.body.getAttribute('data-page');

  // ── OVERVIEW: the edition this computer runs, when the app said so ──
  if (page === 'index' && edition) {
    var cur = $('currentEdition'); cur.textContent = 'This computer runs ' + planName(edition) + (machine ? ' · machine ' + machine : ''); cur.hidden = false;
    var card = document.querySelector('.card.edition[data-edition="' + (edition === 'studio' || edition === 'ultimate' ? 'master' : edition) + '"]'); if (card) card.setAttribute('aria-current', 'true');
  }

  // ── MY LICENCE ──
  if (page === 'my') {
    var box = $('licResult'), held = null;   // { kind: 'key', key, machine } | { kind: 'serial', serial, email }: in memory only, gone with the tab
    var render = function (v) {
      box.textContent = ''; box.hidden = false;
      box.appendChild(el('h2', '', planName(v.plan) + ' licence'));
      var dl = el('dl', 'facts'); box.appendChild(dl);
      facts(dl, [['Serial', v.id], ['State', stateTag(v)], ['Edition', planName(v.plan)], ['Ends', v.expires ? day(v.expires) : 'Never (perpetual)'], ['Computers', v.devices + ' of ' + v.limit + ' activated']]);
      box.appendChild(el('h3', '', 'Activated computers'));
      var list = el('ul', 'devices'); box.appendChild(list);
      var comps = v.computers || [];
      if (!comps.length) list.appendChild(el('li', '', 'No computer is activated on this licence.'));
      comps.forEach(function (c) {
        var li = el('li'), label = el('code', '', held.kind === 'key' ? c : 'Computer ending ' + c.ends);
        var left = el('span'); left.appendChild(label);
        var code = held.kind === 'key' ? c : null;
        if (code && held.machine && code === held.machine) left.appendChild(el('span', 'you', 'THIS COMPUTER'));
        var b = el('button', 'btn small ghost', 'Deactivate'); b.type = 'button';
        b.setAttribute('aria-label', 'Deactivate the computer ending ' + (code ? code.replace(/-/g, '').slice(-4) : c.ends));
        b.addEventListener('click', function () {
          if (!window.confirm('Free this computer\'s seat? AmF X1 on it keeps running until its activation is checked again; activate it again to use it.')) return;
          b.disabled = true;
          var p = held.kind === 'key' ? post('/deactivate', { key: held.key, machine: code }) : post('/licence/api/deactivate', { serial: held.serial, email: held.email, ref: c.ref });
          p.then(function (r) { if (!r.json.ok) { b.disabled = false; return window.alert(errorOf(r)); } lookup(); });
        });
        li.appendChild(left); li.appendChild(b); list.appendChild(li);
      });
    };
    var lookup = function () {
      var msg = held.kind === 'key' ? $('keyMsg') : $('serialMsg'); say(msg, 'Looking it up…');
      var done = function (r) { if (!r.json.ok) { box.hidden = true; return say(msg, errorOf(r), 'bad'); } say(msg, 'Found.', 'ok'); render(r.json); };
      if (held.kind === 'serial') return post('/licence/api/lookup', { serial: held.serial, email: held.email }).then(done);
      return post('/status', { key: held.key, machine: held.machine || undefined, list: true }).then(function (r) {
        if (r.json.ok) { r.json.computers = (r.json.computers || []).map(String); }
        done(r);
      });
    };
    $('byKey').addEventListener('submit', function (e) {
      e.preventDefault(); var msg = $('keyMsg'), kin = $('keyIn'), min = $('keyMachine'), k = kindOf(kin.value), m = min.value.trim().toUpperCase();
      if (invalid(kin, !k || k.token)) return say(msg, k && k.token ? 'An edition token is not a licence key: use Activate for it.' : 'Paste the AMIX1.… licence key or the 25-character product key.', 'bad');
      if (invalid(min, m && !MACHINE_RE.test(m))) return say(msg, 'The machine code has letters, digits and dashes (XXXX-XXXX-XXXX-XXXX-XXXX).', 'bad');
      if (k.code) {
        say(msg, 'Checking the product key…');
        return post('/redeem', { code: k.code }).then(function (r) { if (!r.json.ok) return say(msg, errorOf(r), 'bad'); held = { kind: 'key', key: r.json.key, machine: m }; lookup(); });
      }
      held = { kind: 'key', key: k.key, machine: m }; lookup();
    });
    $('bySerial').addEventListener('submit', function (e) {
      e.preventDefault(); var msg = $('serialMsg'), sin = $('serialIn'), ein = $('emailIn'), s = sin.value.trim().toUpperCase(), em = ein.value.trim();
      if (invalid(sin, !/^[0-9A-F]{12}$/.test(s))) return say(msg, 'The serial is the 12-character licence id (digits and the letters A to F).', 'bad');
      if (invalid(ein, !EMAIL_RE.test(em) || em.length > 120)) return say(msg, 'Type the e-mail address the licence was issued to.', 'bad');
      held = { kind: 'serial', serial: s, email: em }; lookup();
    });
  }

  // ── ACTIVATE ──
  if (page === 'activate') {
    var lastUrl = null;
    $('actForm').addEventListener('submit', function (e) {
      e.preventDefault(); var msg = $('actMsg'), min = $('actMachine'), kin = $('actKey'), m = min.value.trim().toUpperCase(), k = kindOf(kin.value);
      if (invalid(min, !MACHINE_RE.test(m))) return say(msg, 'Type the machine code from the LICENSE page of AmF X1 (XXXX-XXXX-XXXX-XXXX-XXXX).', 'bad');
      if (invalid(kin, !k)) return say(msg, 'Paste a product key (XXXXX-XXXXX-XXXXX-XXXXX-XXXXX), an AMIX1.… licence key or an AMFXE1.… edition token.', 'bad');
      $('actResult').hidden = true; $('actBtn').disabled = true; say(msg, 'Asking the licence server…');
      var body = { machine: m }; if (k.code) body.code = k.code; if (k.key) body.key = k.key; if (k.token) body.token = k.token;
      post('/activate-online', body).then(function (r) {
        $('actBtn').disabled = false;
        if (!r.json.ok || !r.json.activation) return say(msg, errorOf(r), 'bad');
        var v = r.json; say(msg, 'Activated on ' + m + '.', 'ok');
        facts($('actFacts'), [['Edition', planName(v.plan)], ['Term', String(v.term || '')], ['Serial', v.id], ['Machine code', m], ['Computers', v.devices + ' of ' + v.limit + ' activated']]);
        $('actToken').textContent = v.activation;
        if (lastUrl) URL.revokeObjectURL(lastUrl);
        lastUrl = URL.createObjectURL(new Blob([v.activation + '\n'], { type: 'application/octet-stream' }));
        $('openApp').setAttribute('href', lastUrl);
        $('actResult').hidden = false; $('openApp').focus();
      });
    });
    $('copyAct').addEventListener('click', function () {
      var t = $('actToken').textContent, cm = $('copyMsg');
      if (!navigator.clipboard) return say(cm, 'Select the activation below and copy it.', 'bad');
      navigator.clipboard.writeText(t).then(function () { say(cm, 'Copied.', 'ok'); }, function () { say(cm, 'Select the activation below and copy it.', 'bad'); });
    });
  }

  // ── REQUEST / UPGRADE ──
  if (page === 'request') {
    var plan = $('reqPlan'); if (edition && edition !== 'basic') { var want = { pro: 'master', studio: 'master', master: 'ultimate' }[edition]; if (want) plan.value = want; }
    var wa = function () {   // the same message as the app's UPGRADE button (bridge/edition.js upgradeText)
      var p = plan.value, name = p === 'master' ? 'AmF X1 Studio Master' : 'AmF X1 ' + planName(p), m = $('reqMachine').value.trim().toUpperCase();
      var text = 'Hello AMFB Audio, I would like to buy ' + name + '.\nEdition: ' + planName(p).toUpperCase() + '\nMachine ID: ' + (MACHINE_RE.test(m) ? m : '(not shown)') + '\nVersion: \nPlatform: web';
      $('waLink').setAttribute('href', 'https://wa.me/' + WHATSAPP + '?text=' + encodeURIComponent(text));
    };
    ['change', 'input'].forEach(function (ev) { plan.addEventListener(ev, wa); $('reqMachine').addEventListener(ev, wa); }); wa();
    $('reqForm').addEventListener('submit', function (e) {
      e.preventDefault(); var msg = $('reqMsg'), n = $('reqName'), em = $('reqEmail'), mi = $('reqMachine'), m = mi.value.trim().toUpperCase();
      if (invalid(n, !n.value.trim())) return say(msg, 'Type your name.', 'bad');
      if (invalid(em, !EMAIL_RE.test(em.value.trim()))) return say(msg, 'Type the e-mail address we should send the key to.', 'bad');
      if (invalid(mi, !MACHINE_RE.test(m))) return say(msg, 'Type the machine code from the LICENSE page of AmF X1.', 'bad');
      say(msg, 'Sending…');
      post('/request-license', { plan: plan.value, name: n.value.trim().slice(0, 80), email: em.value.trim(), machineId: m, platform: 'web', arch: '', version: '' }).then(function (r) {
        if (!r.json.ok) return say(msg, errorOf(r), 'bad');
        say(msg, 'Request received for ' + planName(plan.value) + ' on ' + m + '. We reply to ' + em.value.trim() + (r.json.emailed ? ' (a receipt is on its way).' : '.'), 'ok');
      });
    });
  }

  // ── DOWNLOADS: the signed update list, its signature checked here with the vendor public key (ECDSA P-256, SHA-256) ──
  if (page === 'downloads') {
    var dmsg = $('dlMsg');
    var b64u = function (s) { var b = atob(String(s).replace(/-/g, '+').replace(/_/g, '/') + '==='.slice((String(s).length + 3) % 4)); var a = new Uint8Array(b.length); for (var i = 0; i < b.length; i++) a[i] = b.charCodeAt(i); return a; };
    var size = function (n) { return n > 1048576 ? (n / 1048576).toFixed(1) + ' MB' : n > 1024 ? Math.round(n / 1024) + ' KB' : n + ' B'; };
    var safeUrl = function (u) { try { var x = new URL(u, location.origin); return x.protocol === 'https:' || x.origin === location.origin ? x.href : null; } catch (_) { return null; } };
    if (!window.crypto || !crypto.subtle) { say(dmsg, 'This browser cannot check signatures here (an https page is needed). Use the download for this computer below.', 'bad'); }
    else Promise.all([fetch('/update.json', { cache: 'no-store' }).then(function (r) { if (!r.ok) throw new Error('no release is published yet'); return r.json(); }), fetch('/licence/vendor-key.json').then(function (r) { return r.json(); })])
      .then(function (both) {
        var env = both[0], jwk = both[1];
        if (!env || typeof env.payload !== 'string' || typeof env.signature !== 'string') throw new Error('the update list is not signed');
        return crypto.subtle.importKey('jwk', { kty: 'EC', crv: 'P-256', x: jwk.x, y: jwk.y, ext: true }, { name: 'ECDSA', namedCurve: 'P-256' }, false, ['verify'])
          .then(function (key) { return crypto.subtle.verify({ name: 'ECDSA', hash: 'SHA-256' }, key, b64u(env.signature), new TextEncoder().encode(env.payload)); })
          .then(function (ok) { if (!ok) throw new Error('the update list is not signed by AMFB Audio and was ignored'); return JSON.parse(env.payload); });
      })
      .then(function (m) {
        if (!m || m.product !== 'audio-mixer' || typeof m.version !== 'string' || !m.files) throw new Error('the update list is not for AmF X1');
        $('dlTitle').textContent = 'AmF X1 ' + m.version + (m.channel && m.channel !== 'stable' ? ' (' + m.channel + ')' : '');
        $('dlDate').textContent = m.released ? 'Released ' + String(m.released).slice(0, 24) : '';
        var notes = $('dlNotes'); (Array.isArray(m.notes) ? m.notes : []).slice(0, 12).forEach(function (n) { notes.appendChild(el('li', '', n)); });
        var list = $('dlFiles'), n = 0;
        Object.keys(m.files).forEach(function (k) {
          var f = m.files[k] || {}, href = safeUrl(f.url); if (!href) return; n++;
          var li = el('li'), info = el('div'); info.appendChild(el('strong', '', String(f.label || f.name || k))); info.appendChild(el('span', 'sha', 'SHA-256 ' + String(f.sha256 || '')));
          var a = el('a', 'btn small', 'Download' + (f.size ? ' · ' + size(Number(f.size)) : '')); a.setAttribute('href', href); a.setAttribute('aria-label', 'Download ' + String(f.name || k));
          li.appendChild(info); li.appendChild(a); list.appendChild(li);
        });
        say(dmsg, n ? '' : 'The release lists no file to download.', n ? '' : 'bad'); $('dlRelease').hidden = false;
      })
      .catch(function (e) { say(dmsg, 'The latest release could not be shown: ' + e.message + '.', 'bad'); });
  }
})();
