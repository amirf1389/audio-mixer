// AmF X1 licence portal: three things a customer needs, with no account and no admin data.
//   Activate   product key -> POST /redeem -> the signed licence key (copy, or save as a .amixkey file the console picks up)
//   Check      a full key  -> POST /status -> edition, end date, state
//   Updates    GET /portal/updates (the server reads the signed update list) -> version, notes, files with their checksums
'use strict';
const $ = id => document.getElementById(id);
const say = (el, text, cls) => { el.textContent = text || ''; el.className = 'msg' + (cls ? ' ' + cls : ''); };
const post = async (path, body) => { const r = await fetch(path, { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify(body) }); let j = {}; try { j = await r.json(); } catch (_) {} return { status: r.status, body: j }; };
const day = ms => ms ? new Date(ms).toISOString().slice(0, 10) : 'no end';

// tabs
document.querySelectorAll('.tab').forEach(b => b.addEventListener('click', () => {
  document.querySelectorAll('.tab').forEach(x => x.classList.toggle('on', x === b));
  for (const t of ['activate', 'check', 'updates']) $('tab-' + t).hidden = t !== b.dataset.tab;
  if (b.dataset.tab === 'updates') loadUpdates();
}));

// the product key groups itself in fives while it is typed
$('pk').addEventListener('input', e => { const v = e.target.value.toUpperCase().replace(/[^0-9A-Z]/g, '').slice(0, 25); e.target.value = (v.match(/.{1,5}/g) || []).join('-'); });
$('pk').addEventListener('keydown', e => { if (e.key === 'Enter') $('pkGo').click(); });
$('pkGo').addEventListener('click', async () => {
  const code = $('pk').value.replace(/[^0-9A-Za-z]/g, '');
  if (code.length !== 25) { say($('pkMsg'), 'A product key has 25 characters.', 'err'); return; }
  $('pkGo').disabled = true; say($('pkMsg'), 'Asking…'); $('pkOut').hidden = true;
  try {
    const r = await post('/redeem', { code });
    if (r.status === 200 && r.body.key) { $('pkKey').value = r.body.key; $('pkPlan').textContent = String(r.body.plan || '').toUpperCase(); $('pkOut').hidden = false; say($('pkMsg'), 'Here is your key.', 'ok'); }
    else say($('pkMsg'), r.status === 429 ? 'Too many tries: wait a minute and try again.' : (r.body.error || 'The server did not accept this product key.'), 'err');
  } catch (_) { say($('pkMsg'), 'The server could not be reached.', 'err'); }
  $('pkGo').disabled = false;
});
$('pkCopy').addEventListener('click', async () => { try { await navigator.clipboard.writeText($('pkKey').value); say($('pkMsg'), 'Copied.', 'ok'); } catch (_) { $('pkKey').select(); say($('pkMsg'), 'Press Ctrl+C to copy.'); } });
$('pkSave').addEventListener('click', () => {
  const a = document.createElement('a'); a.href = URL.createObjectURL(new Blob([$('pkKey').value + '\n'], { type: 'text/plain' })); a.download = 'AmF-X1.amixkey';
  document.body.appendChild(a); a.click(); a.remove(); setTimeout(() => URL.revokeObjectURL(a.href), 2000); say($('pkMsg'), 'Saved as AmF-X1.amixkey.', 'ok');
});

// check a key
$('ckGo').addEventListener('click', async () => {
  const key = $('ckKey').value.trim(), machine = $('ckMid').value.trim().toUpperCase();
  if (!key.startsWith('AMIX1')) { say($('ckMsg'), 'A licence key starts with AMIX1.', 'err'); return; }
  $('ckGo').disabled = true; say($('ckMsg'), 'Checking…'); $('ckOut').hidden = true;
  try {
    const r = await post('/status', machine ? { key, machine } : { key }), b = r.body, tb = $('ckOut').querySelector('tbody'); tb.textContent = '';
    if (r.status !== 200 || b.ok === false) say($('ckMsg'), b.error || 'This key is not genuine.', 'err');
    else {
      const state = b.revoked ? 'revoked' : b.expired ? 'ended' : 'valid';
      const rows = [['Edition', String(b.plan || '').toUpperCase()], ['Licence id', b.id || ''], ['Valid until', day(b.expires)], ['State', state]];
      if (b.activated !== undefined) rows.push(['This computer', b.activated ? 'activated' : 'not activated yet']);
      if (b.devices !== undefined && b.limit !== undefined) rows.push(['Computers in use', b.devices + ' of ' + b.limit]);
      for (const [k, v] of rows) { const tr = document.createElement('tr'), a = document.createElement('th'), c = document.createElement('td'); a.textContent = k; c.textContent = String(v); tr.append(a, c); tb.appendChild(tr); }
      $('ckOut').hidden = false; say($('ckMsg'), state === 'valid' ? 'Genuine and valid.' : 'Genuine, but ' + state + '.', state === 'valid' ? 'ok' : 'err');
    }
  } catch (_) { say($('ckMsg'), 'The server could not be reached.', 'err'); }
  $('ckGo').disabled = false;
});

// updates: the newest release, a version check, downloads by system with size and checksum, and what is new release by release
let updatesLoaded = false, latest = null;
const cmpVersion = (a, b) => { const x = String(a).split('.').map(n => parseInt(n, 10) || 0), y = String(b).split('.').map(n => parseInt(n, 10) || 0); for (let i = 0; i < Math.max(x.length, y.length); i++) { const d = (x[i] || 0) - (y[i] || 0); if (d) return d < 0 ? -1 : 1; } return 0; };
const sizeOf = n => n ? (n / 1048576).toFixed(n < 10485760 ? 2 : 1) + ' MB' : '';
const SYSTEMS = [['win', 'Windows'], ['mac', 'macOS'], ['linux', 'Linux'], ['android', 'Android']];
const systemOf = p => { const k = String(p).toLowerCase(); for (const [id, name] of SYSTEMS) if (k.includes(id) || (id === 'mac' && k.includes('darwin'))) return name; return 'Other'; };
function paintFiles() {
  const want = $('upSys').value, tb = $('upFiles'); tb.textContent = '';
  for (const f of (latest.files || []).filter(f => !want || systemOf(f.platform) === want)) {
    const tr = document.createElement('tr'), cells = [0, 1, 2, 3, 4].map(() => document.createElement('td'));
    cells[0].textContent = systemOf(f.platform) + ' · ' + f.platform; cells[2].textContent = sizeOf(f.size); cells[3].className = 'mono'; cells[3].textContent = f.sha256 || '';
    if (/^https:\/\//.test(f.url || '')) { const a = document.createElement('a'); a.href = f.url; a.rel = 'noopener'; a.textContent = f.name || f.url.split('/').pop(); cells[1].appendChild(a); } else cells[1].textContent = f.name || '';
    if (f.sha256) { const b = document.createElement('button'); b.className = 'small'; b.textContent = 'Copy'; b.addEventListener('click', async () => { try { await navigator.clipboard.writeText(f.sha256); b.textContent = 'Copied'; } catch (_) { b.textContent = 'Select it'; } setTimeout(() => { b.textContent = 'Copy'; }, 1500); }); cells[4].appendChild(b); }
    tr.append(...cells); tb.appendChild(tr);
  }
  if (!tb.children.length) { const tr = document.createElement('tr'), td = document.createElement('td'); td.colSpan = 5; td.className = 'muted'; td.textContent = 'No file for this system in this release.'; tr.appendChild(td); tb.appendChild(tr); }
}
$('upSys').addEventListener('change', paintFiles);
$('myVerGo').addEventListener('click', () => {
  const v = $('myVer').value.trim(); if (!/^\d+(\.\d+){1,3}$/.test(v)) { say($('myVerMsg'), 'Type a version like 1.24.0.0.', 'err'); return; }
  const c = cmpVersion(v, latest.version);
  say($('myVerMsg'), c < 0 ? 'An update is waiting: ' + latest.version + ' is newer than ' + v + '. In AmF X1: ENGINE CONTROL → CHECK NOW.' : c === 0 ? 'You have the newest version.' : 'Your copy (' + v + ') is newer than the published one (' + latest.version + ').', c < 0 ? 'err' : 'ok');
});
$('myVer').addEventListener('keydown', e => { if (e.key === 'Enter') $('myVerGo').click(); });
async function loadUpdates() {
  if (updatesLoaded) return; updatesLoaded = true;
  try {
    const r = await fetch('/portal/updates'), j = await r.json();
    if (!j.ok) { say($('upMsg'), j.error || 'No update list is published yet.', 'err'); updatesLoaded = false; return; }
    latest = j;
    $('upVer').textContent = 'AmF X1 ' + j.version; $('upDate').textContent = (j.released || '') + (j.channel ? ' · ' + j.channel : '') + (j.signed ? ' · the list carries a signature' : ' · the list is not signed');
    const ul = $('upNotes'); ul.textContent = ''; for (const n of (j.notes || []).slice(0, 12)) { const li = document.createElement('li'); li.textContent = n; ul.appendChild(li); }
    if (!ul.children.length) { const li = document.createElement('li'); li.className = 'muted'; li.textContent = 'No notes were published with this release.'; ul.appendChild(li); }
    const sys = $('upSys'), present = [...new Set((j.files || []).map(f => systemOf(f.platform)))]; sys.textContent = '';
    for (const [v, t] of [['', 'every system (' + (j.files || []).length + ' files)']].concat(present.map(p => [p, p]))) { const o = document.createElement('option'); o.value = v; o.textContent = t; sys.appendChild(o); }
    const ua = navigator.userAgent, mine = /Windows/.test(ua) ? 'Windows' : /Android/.test(ua) ? 'Android' : /Mac OS X/.test(ua) ? 'macOS' : /Linux/.test(ua) ? 'Linux' : ''; if (present.includes(mine)) sys.value = mine;   // start on the visitor's own system
    paintFiles();
    const exe = (j.files || []).find(f => /\.exe$/i.test(f.name || '') && systemOf(f.platform) === 'Windows'); if (exe) $('upCmd').textContent = 'Get-FileHash ".\\' + exe.name + '" -Algorithm SHA256';
    const box = $('upNew'); box.textContent = '';
    for (const f of j.features || []) { if (!f.items || !f.items.length) continue; const d = document.createElement('details'), sum = document.createElement('summary'), b = document.createElement('b'), ul2 = document.createElement('ul'); d.className = 'rel';
      b.textContent = f.version; sum.append(b, document.createTextNode((f.title || '') + (f.date ? ' · ' + f.date : ''))); for (const it of f.items) { const li = document.createElement('li'); li.textContent = it; ul2.appendChild(li); } d.append(sum, ul2); if (!box.children.length) d.open = true; box.appendChild(d); }
    $('upNewBox').hidden = !box.children.length;
    $('upMsg').hidden = true; $('upOut').hidden = false;
  } catch (_) { say($('upMsg'), 'The update list could not be read.', 'err'); updatesLoaded = false; }
}

// the vendor's notice and maintenance message (SYSTEM → Settings), shown as plain text
fetch('/portal/notice').then(r => r.json()).then(j => {
  if (!j || !j.ok) return; const box = document.getElementById('notice'); const parts = [];
  if (j.maintenance) parts.push('Maintenance: some steps may not work right now. Please try again a little later.');
  if (j.notice) parts.push(j.notice);
  if (j.supportEmail) parts.push('Help: ' + j.supportEmail);
  if (parts.length) { box.textContent = parts.join('  ·  '); box.hidden = false; }
}).catch(() => {});
