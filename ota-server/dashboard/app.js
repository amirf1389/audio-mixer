/* Audio Mixer OTA server: vendor dashboard.
   - Talks to the admin API of the same server with the admin token you type in (kept in this tab's memory only; "remember in this tab" uses sessionStorage).
   - Publishing is signed IN THIS BROWSER with your vendor private key (private.pem is read from your disk, imported as a non-extractable WebCrypto key
     and never sent anywhere); the server only receives the signed manifest, which it verifies again before it publishes.
   - No external scripts or styles; everything from the server is written as text nodes (no HTML is ever parsed), under a strict Content-Security-Policy.
   The helpers at the top are pure and are also loaded by the tests in Node. */
(function (root) {
  'use strict';
  const subtle = () => (root.crypto && root.crypto.subtle) || require('node:crypto').webcrypto.subtle;
  const enc = s => new TextEncoder().encode(s);
  const hex = buf => Array.from(new Uint8Array(buf), b => b.toString(16).padStart(2, '0')).join('');
  const b64u = buf => { let s = ''; new Uint8Array(buf).forEach(b => { s += String.fromCharCode(b); }); return btoa(s).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, ''); };
  const unb64u = t => { const s = atob(String(t).replace(/-/g, '+').replace(/_/g, '/')), a = new Uint8Array(s.length); for (let i = 0; i < s.length; i++) a[i] = s.charCodeAt(i); return a; };
  const sha256Hex = async buf => hex(await subtle().digest('SHA-256', buf));
  const EC = { name: 'ECDSA', namedCurve: 'P-256' };

  // "-----BEGIN PRIVATE KEY-----" (PKCS#8, what scripts/license.js init writes) -> non-extractable signing key
  async function importPrivate(pem) {
    const m = /-----BEGIN PRIVATE KEY-----([\s\S]+?)-----END PRIVATE KEY-----/.exec(String(pem));
    if (!m) throw new Error('This is not a PKCS#8 private key (private.pem from "node scripts/license.js init").');
    return subtle().importKey('pkcs8', unb64u(m[1].replace(/\s+/g, '').replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '')), EC, false, ['sign']);
  }
  // same envelope as bridge/update.js signManifest: the signature covers the exact payload text (ECDSA P-256 / SHA-256, r||s)
  async function signManifest(manifest, key) {
    const payload = JSON.stringify(manifest);
    return { payload, signature: b64u(await subtle().sign({ name: 'ECDSA', hash: 'SHA-256' }, key, enc(payload))) };
  }
  async function verifyEnvelope(env, jwk) {
    try {
      const k = await subtle().importKey('jwk', { kty: jwk.kty, crv: jwk.crv, x: jwk.x, y: jwk.y, ext: true }, EC, false, ['verify']);
      return await subtle().verify({ name: 'ECDSA', hash: 'SHA-256' }, k, unb64u(env.signature), enc(env.payload));
    } catch (_) { return false; }
  }

  const PLATFORMS = [
    { key: 'win-x64-exe', label: 'Windows 64-bit setup (.exe)', re: /^Audio Mixer-(\d+(?:\.\d+){1,3})\.exe$/ },
    { key: 'win-x64-msi', label: 'Windows 64-bit package (.msi)', re: /^AudioMixer-(\d+(?:\.\d+){1,3})-x64\.msi$/ },
    { key: 'win-x86-msi', label: 'Windows 32-bit package (.msi)', re: /^AudioMixer-(\d+(?:\.\d+){1,3})-x86\.msi$/ },
    { key: 'linux-deb', label: 'Debian / Ubuntu (.deb)', re: /^audio-mixer_(\d+(?:\.\d+){1,3})_all\.deb$/ },
    { key: 'linux-tar', label: 'Linux archive (.tar.gz)', re: /^AudioMixer-(\d+(?:\.\d+){1,3})-linux\.tar\.gz$/ },
    { key: 'macos-dmg', label: 'macOS disk image (.dmg)', re: /^AudioMixer-(\d+(?:\.\d+){1,3})-macos\.dmg$/ },
    { key: 'macos', label: 'macOS archive (.tar.gz)', re: /^AudioMixer-(\d+(?:\.\d+){1,3})-macos\.tar\.gz$/ },
    { key: 'android-apk', label: 'Android app (.apk)', re: /^AudioMixer-(\d+(?:\.\d+){1,3})-android\.apk$/ },
    { key: 'ios-project', label: 'iOS Xcode project (.tar.gz)', re: /^AudioMixer-(\d+(?:\.\d+){1,3})-ios-xcode-project\.tar\.gz$/ },
  ];
  const cmpVersion = (a, b) => { const p = v => String(v).split('.').map(n => parseInt(n, 10) || 0); const x = p(a), y = p(b); for (let i = 0; i < Math.max(x.length, y.length); i++) { const d = (x[i] || 0) - (y[i] || 0); if (d) return d < 0 ? -1 : 1; } return 0; };
  // which uploaded files belong to which platform, for the newest version found in the file names
  function guess(files) {
    let version = null; const found = {};
    for (const f of files) for (const p of PLATFORMS) { const m = p.re.exec(f.name); if (m) { (found[m[1]] = found[m[1]] || {})[p.key] = f.name; if (!version || cmpVersion(m[1], version) > 0) version = m[1]; } }
    return { version, map: version ? found[version] : {} };
  }
  // same shape as scripts/make-update.js: files { key: { name, url, size, sha256 } }
  function buildManifest({ version, channel, notes, map, files, base, released }) {
    const byName = Object.fromEntries(files.map(f => [f.name, f])), out = {};
    for (const [key, name] of Object.entries(map)) { const f = byName[name]; if (name && f) out[key] = { name, url: base.replace(/\/+$/, '') + '/releases/' + encodeURIComponent(name), size: f.size, sha256: f.sha256 }; }
    return { product: 'audio-mixer', version, channel, released, notes: notes.map(s => s.trim()).filter(Boolean), files: out };
  }
  // audit log: readable names and a one-line summary of the details of an entry
  const ACTIONS = { 'file.upload': 'Upload file', 'file.delete': 'Delete file', 'manifest.publish': 'Publish', 'manifest.rollback': 'Roll back', 'manifest.unpublish': 'Unpublish', 'files.verify': 'Verify files', 'auth.denied': 'Sign-in refused', 'admin.rate_limited': 'Rate limited', 'session.open': 'Dashboard opened', 'audit.export': 'Audit exported' };
  const fmtDetail = d => {
    if (d == null || typeof d !== 'object') return d == null ? '' : String(d);
    const one = v => (Array.isArray(v) ? v.map(one).join(', ') : v && typeof v === 'object' ? JSON.stringify(v) : String(v));
    const t = Object.entries(d).filter(([, v]) => v !== null && v !== false && !(Array.isArray(v) && !v.length)).map(([k, v]) => (v === true ? k : k + ' ' + one(v))).join(' • ');
    return t.length > 220 ? t.slice(0, 219) + '…' : t;
  };
  const pure = { ACTIONS, fmtDetail, sha256Hex, importPrivate, signManifest, verifyEnvelope, guess, buildManifest, cmpVersion, b64u, unb64u, PLATFORMS };
  if (typeof module !== 'undefined' && module.exports) { module.exports = pure; return; }
  if (typeof document === 'undefined') return;

  // ───────────────────────── UI ─────────────────────────
  const $app = document.getElementById('app');
  const S = { token: '', config: null, view: 'overview', files: [], stats: null, manifests: {}, histChannel: '', msg: null };
  const h = (tag, attrs, ...kids) => {
    const e = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs || {})) { if (k === 'class') e.className = v; else if (k === 'style') e.style.cssText = v; else if (k.startsWith('on')) e.addEventListener(k.slice(2), v); else if (v === true) e.setAttribute(k, ''); else if (v !== false && v != null) e.setAttribute(k, v); }
    for (const c of kids.flat()) if (c != null && c !== false) e.append(c.nodeType ? c : document.createTextNode(String(c)));
    return e;
  };
  const mb = n => (n / 1048576).toFixed(n < 10485760 ? 2 : 1) + ' MB';
  const short = s => String(s).slice(0, 12) + '…';
  const pill = (text, kind) => h('span', { class: 'pill ' + (kind || '') }, text);
  const say = (text, bad) => { S.msg = text ? { text, bad: !!bad } : null; paintMsg(); };
  const msgBox = h('div', { class: 'card', hidden: true, role: 'status' });
  function paintMsg() { msgBox.hidden = !S.msg; msgBox.textContent = S.msg ? S.msg.text : ''; msgBox.className = 'card ' + (S.msg && S.msg.bad ? 'err' : 'okc'); }

  async function api(method, path, { body, headers, raw } = {}) {
    const r = await fetch(path, { method, headers: { Authorization: 'Bearer ' + S.token, ...(headers || {}) }, body });
    if (r.status === 401) { S.token = ''; try { sessionStorage.removeItem('ota-token'); } catch (_) { /* storage blocked */ } render(); throw new Error('The admin token was refused.'); }
    const j = raw ? r : await r.json().catch(() => ({}));
    if (!raw && (!r.ok || j.ok === false)) throw new Error((j.error || 'HTTP ' + r.status) + (j.problems ? ': ' + j.problems.join('; ') : ''));
    return j;
  }
  async function loadAll() {
    S.config = await api('GET', '/admin/config');
    S.files = (await api('GET', '/admin/files')).files;
    S.stats = (await api('GET', '/admin/stats')).days;
    S.manifests = {};
    for (const ch of Object.keys(S.config.channels)) {
      const r = await fetch((ch === 'stable' ? '' : '/' + ch) + '/update.json', { cache: 'no-store' });
      if (r.ok) { const env = await r.json(); S.manifests[ch] = { env, manifest: JSON.parse(env.payload), valid: await verifyEnvelope(env, S.config.jwk) }; }
    }
    if (!S.histChannel || !S.config.channels[S.histChannel]) S.histChannel = Object.keys(S.config.channels)[0] || 'stable';
  }
  const refresh = async () => { try { await loadAll(); render(); } catch (e) { say(e.message, true); } };
  const base = () => S.config.publicUrl || location.origin;

  // ── views ──
  function login() {
    const t = h('input', { type: 'password', id: 'tok', autocomplete: 'current-password', 'aria-label': 'Admin token', placeholder: 'OTA_ADMIN_TOKEN' }), keep = h('input', { type: 'checkbox', id: 'keep' });
    const go = async () => { S.token = t.value.trim(); if (!S.token) return; try { await loadAll(); if (keep.checked) { try { sessionStorage.setItem('ota-token', S.token); } catch (_) { /* storage blocked */ } } say(null); render(); } catch (e) { S.token = ''; say(e.message, true); } };
    t.addEventListener('keydown', e => { if (e.key === 'Enter') go(); });
    return h('div', { class: 'login card' }, h('h1', null, 'Audio Mixer ', h('b', null, 'OTA')), h('p', { class: 'dim' }, 'Vendor dashboard. Enter the admin token of this server (OTA_ADMIN_TOKEN).'), t,
      h('label', { for: 'keep', style: 'display:flex;gap:8px;align-items:center' }, keep, 'remember in this tab'), h('button', { class: 'btn primary', onclick: go }, 'Sign in'), msgBox);
  }
  function overview() {
    const chs = Object.keys(S.config.channels);
    return h('div', { class: 'grid', style: 'grid-template-columns:1fr' }, chs.length ? chs.map(ch => {
      const m = S.manifests[ch];
      if (!m) return h('div', { class: 'card' }, h('h2', null, ch), 'Not readable.');
      const mf = m.manifest;
      return h('div', { class: 'card' },
        h('div', { class: 'row' }, h('h2', null, ch + ' channel'), h('span', null, pill('v' + mf.version, 'warn'), ' ', pill(m.valid ? 'signature valid' : 'SIGNATURE INVALID', m.valid ? 'ok' : 'bad'), ' ', h('span', { class: 'dim mono' }, mf.released || ''))),
        mf.notes && mf.notes.length ? h('ul', null, mf.notes.map(n => h('li', null, n))) : null,
        h('div', { class: 'tbl' }, h('table', null, h('thead', null, h('tr', null, ['Platform', 'File', 'Size', 'SHA-256'].map(x => h('th', null, x)))),
          h('tbody', null, Object.entries(mf.files).map(([k, f]) => h('tr', null, h('td', null, (PLATFORMS.find(p => p.key === k) || { label: k }).label), h('td', null, h('a', { href: '/releases/' + encodeURIComponent(f.name) }, f.name)), h('td', { class: 'num' }, mb(f.size)), h('td', { class: 'mono', title: f.sha256 }, short(f.sha256))))))),
        h('p', { class: 'dim mono', style: 'word-break:break-all' }, 'Apps: BRIDGE_UPDATE_URL=' + base() + (ch === 'stable' ? '' : '/' + ch) + '/update.json  BRIDGE_UPDATE_HOSTS=' + new URL(base()).hostname),
        h('div', { class: 'row' }, h('a', { class: 'btn', href: (ch === 'stable' ? '' : '/' + ch) + '/update.json', target: '_blank', rel: 'noopener' }, 'Open manifest'),
          h('button', { class: 'btn danger', onclick: async () => { if (!confirm('Unpublish the ' + ch + ' channel? The apps will get "no update" (404) until you publish again. The history stays.')) return; try { await api('DELETE', '/admin/manifest/' + ch); say('Unpublished ' + ch); await refresh(); } catch (e) { say(e.message, true); } } }, 'Unpublish')));
    }) : h('div', { class: 'card' }, h('h2', null, 'Nothing published yet'), h('p', null, 'Upload the release files under Files, then publish them under Publish (signed with your vendor key).')));
  }
  function files() {
    const list = h('div', { class: 'grid', style: 'grid-template-columns:1fr' });
    const pick = h('input', { type: 'file', multiple: true, 'aria-label': 'Release files' });
    const drop = h('div', { class: 'drop' }, 'Drop release files here (the .exe, .msi, .deb, .tar.gz of one version) or ', pick);
    const rows = h('div');
    const upload = async file => {
      const row = h('div', { class: 'card' }), bar = h('i'), status = h('span', { class: 'dim' }, 'hashing …');
      row.append(h('div', { class: 'row' }, h('b', { class: 'mono' }, file.name), status), h('div', { class: 'bar' }, bar)); rows.prepend(row);
      try {
        if (file.size > S.config.maxFile) throw new Error('larger than the limit (' + mb(S.config.maxFile) + ')');
        const sum = await sha256Hex(await file.arrayBuffer()); status.textContent = 'uploading …';
        await new Promise((res, rej) => {
          const x = new XMLHttpRequest(); x.open('PUT', '/admin/files/' + encodeURIComponent(file.name)); x.setRequestHeader('Authorization', 'Bearer ' + S.token); x.setRequestHeader('X-SHA256', sum);
          x.upload.onprogress = e => { if (e.lengthComputable) bar.style.width = (100 * e.loaded / e.total) + '%'; };
          x.onload = () => { let j = {}; try { j = JSON.parse(x.responseText); } catch (_) { /* not JSON */ } x.status < 300 ? res(j) : rej(new Error(j.error || 'HTTP ' + x.status)); }; x.onerror = () => rej(new Error('network error'));
          x.send(file);
        });
        bar.style.width = '100%'; status.textContent = 'stored, SHA-256 checked'; status.className = 'okc';
      } catch (e) { status.textContent = e.message; status.className = 'err'; }
    };
    const take = async fl => { for (const f of Array.from(fl)) await upload(f); await refresh(); };
    pick.addEventListener('change', () => take(pick.files));
    drop.addEventListener('dragover', e => { e.preventDefault(); drop.classList.add('over'); }); drop.addEventListener('dragleave', () => drop.classList.remove('over'));
    drop.addEventListener('drop', e => { e.preventDefault(); drop.classList.remove('over'); take(e.dataTransfer.files); });
    list.append(h('div', { class: 'card' }, h('h2', null, 'Upload'), drop, rows),
      h('div', { class: 'card' }, h('h2', null, 'Files on the server (' + S.files.length + ')'), h('div', { class: 'tbl' }, h('table', null, h('thead', null, h('tr', null, ['File', 'Size', 'SHA-256', 'Listed by', ''].map(x => h('th', null, x)))),
        h('tbody', null, S.files.map(f => h('tr', null, h('td', null, h('a', { href: '/releases/' + encodeURIComponent(f.name) }, f.name)), h('td', { class: 'num' }, mb(f.size)), h('td', { class: 'mono', title: f.sha256 }, short(f.sha256)),
          h('td', null, f.referencedBy.length ? f.referencedBy.map(c => pill(c, 'ok')) : pill('not listed', 'warn')),
          h('td', null, h('button', { class: 'btn danger', disabled: f.referencedBy.length > 0, title: f.referencedBy.length ? 'A published manifest lists this file' : '', onclick: async () => { if (!confirm('Delete ' + f.name + '?')) return; try { await api('DELETE', '/admin/files/' + encodeURIComponent(f.name)); say('Deleted ' + f.name); await refresh(); } catch (e) { say(e.message, true); } } }, 'Delete')))))))));
    return list;
  }
  function publish() {
    const g = guess(S.files), channel = h('input', { type: 'text', value: 'stable', list: 'chs', 'aria-label': 'Channel' }), version = h('input', { type: 'text', value: g.version || '', 'aria-label': 'Version' });
    const notes = h('textarea', { 'aria-label': 'Notes', placeholder: 'One line per note shown to the user' }), key = h('input', { type: 'file', accept: '.pem,text/plain', 'aria-label': 'Vendor private key' });
    const force = h('input', { type: 'checkbox' }), preview = h('pre', null, 'Choose the files to see the manifest.'), result = h('div');
    const maps = PLATFORMS.map(p => { const s = h('select', { 'aria-label': p.label }, h('option', { value: '' }, '— not offered —'), S.files.filter(f => p.re.test(f.name)).map(f => h('option', { value: f.name }, f.name))); s.value = g.map[p.key] || ''; return [p, s]; });
    const manifest = () => buildManifest({ version: version.value.trim(), channel: channel.value.trim(), notes: notes.value.split('\n'), map: Object.fromEntries(maps.map(([p, s]) => [p.key, s.value]).filter(x => x[1])), files: S.files, base: base(), released: new Date().toISOString().slice(0, 10) });
    const upd = () => { preview.textContent = JSON.stringify(manifest(), null, 2); };
    [channel, version, notes, ...maps.map(x => x[1])].forEach(e => { e.addEventListener('input', upd); e.addEventListener('change', upd); }); upd();
    const go = h('button', { class: 'btn primary', onclick: async () => {
      result.textContent = ''; const m = manifest();
      try {
        if (!/^\d+(\.\d+){1,3}$/.test(m.version)) throw new Error('The version must look like 1.4.2.0.'); if (!/^[a-z0-9][a-z0-9-]{0,19}$/.test(m.channel)) throw new Error('The channel is lower-case letters, digits and "-".');
        if (!Object.keys(m.files).length) throw new Error('Choose at least one file.'); if (!key.files[0]) throw new Error('Choose your vendor private key (private.pem): it signs the manifest here in the browser and is not uploaded.');
        if (!confirm('Publish version ' + m.version + ' on the ' + m.channel + ' channel? Apps that check for updates will see it.')) return;
        const env = await signManifest(m, await importPrivate(await key.files[0].text())), r = await api('PUT', '/admin/manifest/' + encodeURIComponent(m.channel) + (force.checked ? '?force=1' : ''), { body: JSON.stringify(env), headers: { 'Content-Type': 'application/json' } });
        say(r.unchanged ? 'Already published.' : 'Published ' + r.version + ' on ' + m.channel + (r.previous ? ' (was ' + r.previous + ')' : '')); await refresh();
      } catch (e) { result.className = 'err'; result.textContent = e.message; }
    } }, 'Sign and publish');
    return h('div', { class: 'card', style: 'display:grid;gap:12px' }, h('h2', null, 'Publish a version'),
      h('datalist', { id: 'chs' }, h('option', { value: 'stable' }), h('option', { value: 'beta' }), Object.keys(S.config.channels).map(c => h('option', { value: c }))),
      h('div', { class: 'grid' }, h('label', null, 'Channel', channel), h('label', null, 'Version', version)), h('label', null, 'Notes', notes),
      h('div', { class: 'grid' }, maps.map(([p, s]) => h('label', null, p.label, s))),
      h('label', null, 'Vendor private key (private.pem, stays in this browser)', key), h('label', { style: 'display:flex;gap:8px;align-items:center' }, force, 'replace a version that is already published (same or older)'),
      h('h2', null, 'Manifest preview'), preview, go, result);
  }
  function historyView() {
    const box = h('div', { class: 'tbl' }), sel = h('select', { 'aria-label': 'Channel' }, Object.keys(S.config.channels).map(c => h('option', { value: c }, c)));
    sel.value = S.histChannel;
    const load = async () => {
      S.histChannel = sel.value; box.textContent = 'Loading …';
      try {
        const v = (await api('GET', '/admin/history/' + S.histChannel)).versions;
        box.replaceChildren(v.length ? h('table', null, h('thead', null, h('tr', null, ['Version', 'Released', 'Notes', 'Files', ''].map(x => h('th', null, x)))), h('tbody', null, v.map(x => h('tr', null, h('td', { class: 'mono' }, x.version, ' ', x.current ? pill('published', 'ok') : null), h('td', null, x.released || ''), h('td', null, x.notes.join(' • ')), h('td', null, x.files.length),
          h('td', null, x.current ? null : h('button', { class: 'btn', onclick: async () => { if (!confirm('Publish version ' + x.version + ' on the ' + S.histChannel + ' channel again (roll back)? Its files must still be on the server.')) return; try { const env = await api('GET', '/admin/history/' + S.histChannel + '/' + x.version, { raw: true }); await api('PUT', '/admin/manifest/' + S.histChannel + '?force=1', { body: await env.text(), headers: { 'Content-Type': 'application/json' } }); say('Rolled back to ' + x.version + ' on ' + S.histChannel); await refresh(); } catch (e) { say(e.message, true); } } }, 'Roll back to this'))))))
          : h('p', { class: 'dim' }, 'No version has been published on this channel.'));
      } catch (e) { box.textContent = e.message; }
    };
    sel.addEventListener('change', load); load();
    return h('div', { class: 'card', style: 'display:grid;gap:10px' }, h('h2', null, 'History and roll back'), h('label', null, 'Channel', sel), box, h('p', { class: 'dim' }, 'A roll back publishes an older, already signed manifest again; nothing is signed here.'));
  }
  function statsView() {
    const days = Object.keys(S.stats).sort().slice(-30), sum = o => Object.values(o).reduce((a, b) => a + b, 0), W = 720, H = 180, bw = W / Math.max(days.length, 1);
    const checks = days.map(d => sum(S.stats[d].manifest)), dls = days.map(d => sum(S.stats[d].download)), max = Math.max(1, ...checks, ...dls);
    const ns = 'http://www.w3.org/2000/svg', el = (t, a) => { const e = document.createElementNS(ns, t); for (const [k, v] of Object.entries(a || {})) e.setAttribute(k, v); return e; };
    const svg = el('svg', { viewBox: `0 0 ${W} ${H + 20}`, width: '100%', role: 'img', 'aria-label': 'Update checks and downloads per day' });
    days.forEach((d, i) => {
      const x = i * bw, c = checks[i] / max * H, g = dls[i] / max * H;
      svg.append(el('rect', { class: 'bc', x: x + 2, y: H - c, width: Math.max(1, bw / 2 - 3), height: c }), el('rect', { class: 'bd', x: x + bw / 2, y: H - g, width: Math.max(1, bw / 2 - 3), height: g }));
      if (i % Math.ceil(days.length / 8) === 0) { const t = el('text', { x: x + 2, y: H + 14 }); t.textContent = d.slice(5); svg.append(t); }
    });
    const per = {}; Object.values(S.stats).forEach(d => Object.entries(d.download).forEach(([n, c]) => { per[n] = (per[n] || 0) + c; }));
    const det = {}; Object.values(S.stats).forEach(d => Object.entries(d.detect || {}).forEach(([n, c]) => { det[n] = (det[n] || 0) + c; }));
    return h('div', { class: 'card', style: 'display:grid;gap:10px' }, h('h2', null, 'Last ' + days.length + ' days'), days.length ? svg : h('p', { class: 'dim' }, 'No requests counted yet.'),
      h('div', null, pill('update checks ' + checks.reduce((a, b) => a + b, 0)), ' ', pill('downloads ' + dls.reduce((a, b) => a + b, 0), 'warn'), ' ', h('span', { class: 'dim' }, 'cyan = update checks, amber = downloads (a download is counted when it starts from byte 0)')),
      Object.keys(det).length ? h('div', null, h('span', { class: 'dim' }, 'Smart links (/latest, /api/latest) by detected system: '), Object.entries(det).sort((a, b) => b[1] - a[1]).map(([n, c]) => [pill(n + ' ' + c, n === 'unknown' ? 'warn' : ''), ' '])) : null,
      Object.keys(per).length ? h('div', { class: 'tbl' }, h('table', null, h('thead', null, h('tr', null, h('th', null, 'File'), h('th', null, 'Downloads'))), h('tbody', null, Object.entries(per).sort((a, b) => b[1] - a[1]).map(([n, c]) => h('tr', null, h('td', null, n), h('td', { class: 'num' }, c)))))) : null);
  }
  function health() {
    const out = h('div'), btn = h('button', { class: 'btn primary', onclick: async () => {
      btn.disabled = true; out.textContent = 'Hashing every file on the disk … this takes a moment for large files.';
      try { const r = await api('GET', '/admin/verify'); out.replaceChildren(h('p', { class: r.allGood ? 'okc' : 'err' }, r.allGood ? 'All ' + r.files.length + ' files match the SHA-256 recorded at upload.' : 'Damaged or missing files found:'), h('div', { class: 'tbl' }, h('table', null, h('tbody', null, r.files.map(f => h('tr', null, h('td', null, f.name), h('td', null, pill(f.ok ? 'ok' : String(f.actual).length > 12 ? 'DIFFERS' : f.actual, f.ok ? 'ok' : 'bad'))))))));
      } catch (e) { out.textContent = e.message; out.className = 'err'; } btn.disabled = false;
    } }, 'Verify files on disk');
    return h('div', { class: 'card', style: 'display:grid;gap:10px' }, h('h2', null, 'Server health'), h('p', null, 'Public URL: ', h('b', { class: 'mono' }, S.config.publicUrl || '(not set: OTA_PUBLIC_URL)')), h('p', null, 'Channels: ', Object.entries(S.config.channels).map(([c, v]) => pill(c + ' ' + v, 'ok')), Object.keys(S.config.channels).length ? null : 'none published'),
      h('p', { class: 'dim' }, 'The server holds only the public key. Your private key is used in this browser to sign manifests and is never uploaded.'), btn, out);
  }

  function auditView() {
    const act = h('select', { 'aria-label': 'Action' }, [['', 'All actions'], ['file.', 'Files'], ['manifest.', 'Publish / roll back / unpublish'], ['files.verify', 'File checks'], ['auth.', 'Refused sign-ins'], ['admin.', 'Rate limits'], ['session.', 'Dashboard opened'], ['audit.', 'Audit exports']].map(([v, l]) => h('option', { value: v }, l)));
    const res = h('select', { 'aria-label': 'Result' }, [['', 'Any result'], ['ok', 'ok'], ['rejected', 'rejected by the server'], ['denied', 'denied'], ['error', 'error']].map(([v, l]) => h('option', { value: v }, l)));
    const find = h('input', { type: 'text', placeholder: 'Search the rows shown', 'aria-label': 'Search' });
    const body = h('tbody'), info = h('p', { class: 'dim' }), more = h('button', { class: 'btn', hidden: true }, 'Load older entries'), chain = h('span');
    let rows = [], last = 0;
    const paint = () => {
      const q = find.value.trim().toLowerCase();
      body.replaceChildren(...rows.filter(e => !q || JSON.stringify(e).toLowerCase().includes(q)).map(e => h('tr', null,
        h('td', { class: 'mono', title: e.t + ' (UTC)' }, new Date(e.t).toLocaleString()), h('td', { class: 'mono', title: e.actor.ua || '' }, (e.actor.fp ? 'token ' + e.actor.fp : 'no valid token'), h('br'), h('span', { class: 'dim' }, e.actor.ip)),
        h('td', null, ACTIONS[e.action] || e.action), h('td', { class: 'mono' }, e.target), h('td', { class: 'dim' }, fmtDetail(e.detail)),
        h('td', null, pill(e.result + (e.status ? ' ' + e.status : ''), e.result === 'ok' ? 'ok' : e.result === 'rejected' ? 'warn' : 'bad')))));
    };
    const load = async older => {
      try {
        const r = await api('GET', '/admin/audit?limit=100' + (older && last ? '&before=' + last : '') + '&action=' + encodeURIComponent(act.value) + '&result=' + encodeURIComponent(res.value));
        rows = older ? rows.concat(r.entries) : r.entries; last = rows.length ? rows[rows.length - 1].seq : 0;
        info.textContent = r.total + ' matching entries (' + r.chain + ' in the log), newest first.'; more.hidden = !r.hasMore; paint();
      } catch (e) { info.textContent = e.message; info.className = 'err'; }
    };
    more.addEventListener('click', () => load(true)); act.addEventListener('change', () => load(false)); res.addEventListener('change', () => load(false)); find.addEventListener('input', paint);
    const verifyBtn = h('button', { class: 'btn', onclick: async () => { try { const r = await api('GET', '/admin/audit/verify'); chain.replaceChildren(r.chainOk ? pill('chain intact: ' + r.entries + ' entries', 'ok') : pill('CHAIN BROKEN: ' + r.reason, 'bad')); } catch (e) { chain.textContent = e.message; } } }, 'Verify the log');
    const exportBtn = h('button', { class: 'btn', onclick: async () => { try { const r = await api('GET', '/admin/audit/export', { raw: true }); if (!r.ok) throw new Error('HTTP ' + r.status); const a = h('a', { href: URL.createObjectURL(await r.blob()), download: 'audit.jsonl' }); document.body.append(a); a.click(); a.remove(); } catch (e) { say(e.message, true); } } }, 'Download (JSON lines)');
    load(false);
    return h('div', { class: 'card', style: 'display:grid;gap:10px' }, h('h2', null, 'Audit log'),
      h('p', { class: 'dim' }, 'Every upload, delete, publish, roll back, unpublish and file check, refused sign-ins and dashboard openings: when, from where, with which token (first 8 characters of its SHA-256, never the token) and what the server answered. Append-only and hash-chained: a changed or removed line is found by "Verify the log".'),
      h('div', { class: 'grid' }, h('label', null, 'Action', act), h('label', null, 'Result', res), h('label', null, 'Search', find)), h('div', { class: 'row' }, info, h('span', null, chain, ' ', verifyBtn, ' ', exportBtn)),
      h('div', { class: 'tbl' }, h('table', null, h('thead', null, h('tr', null, ['When', 'Who', 'Action', 'Target', 'Details', 'Result'].map(x => h('th', null, x)))), body)), more);
  }

  const VIEWS = { overview: ['Overview', overview], files: ['Files', files], publish: ['Publish', publish], history: ['History', historyView], audit: ['Audit', auditView], stats: ['Stats', statsView], health: ['Health', health] };
  function render() {
    if (!S.token) { $app.replaceChildren(login()); return; }
    const nav = h('nav', null, Object.entries(VIEWS).map(([k, [label]]) => h('button', { 'aria-current': k === S.view ? 'page' : null, onclick: () => { S.view = k; say(null); render(); } }, label)));
    const head = h('header', null, h('h1', null, 'Audio Mixer ', h('b', null, 'OTA'), ' server'), h('span', null, h('button', { class: 'btn', onclick: refresh }, 'Refresh'), ' ', h('button', { class: 'btn', onclick: () => { S.token = ''; try { sessionStorage.removeItem('ota-token'); } catch (_) { /* storage blocked */ } render(); } }, 'Sign out')));
    $app.replaceChildren(head, nav, h('main', null, msgBox, VIEWS[S.view][1]()));
    paintMsg();
  }
  let saved = ''; try { saved = sessionStorage.getItem('ota-token') || ''; } catch (_) { /* storage blocked */ }
  if (saved) { S.token = saved; loadAll().then(render, () => { S.token = ''; render(); }); } else render();
})(typeof globalThis !== 'undefined' ? globalThis : this);
