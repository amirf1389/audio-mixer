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

// updates
let updatesLoaded = false;
async function loadUpdates() {
  if (updatesLoaded) return; updatesLoaded = true;
  try {
    const r = await fetch('/portal/updates'), j = await r.json();
    if (!j.ok) { say($('upMsg'), j.error || 'No update list is published yet.', 'err'); updatesLoaded = false; return; }
    $('upVer').textContent = 'AmF X1 ' + j.version; $('upDate').textContent = (j.released || '') + (j.channel ? ' · ' + j.channel : '') + (j.signed ? ' · the list carries a signature' : ' · the list is not signed');
    const ul = $('upNotes'); ul.textContent = ''; for (const n of (j.notes || []).slice(0, 12)) { const li = document.createElement('li'); li.textContent = n; ul.appendChild(li); }
    const tb = $('upFiles'); tb.textContent = '';
    for (const f of j.files || []) {
      const tr = document.createElement('tr'), s = document.createElement('td'), n = document.createElement('td'), h = document.createElement('td'); h.className = 'mono';
      s.textContent = f.platform; h.textContent = f.sha256 || '';
      if (/^https:\/\//.test(f.url || '')) { const a = document.createElement('a'); a.href = f.url; a.rel = 'noopener'; a.textContent = f.name || f.url.split('/').pop(); n.appendChild(a); } else n.textContent = f.name || '';
      tr.append(s, n, h); tb.appendChild(tr);
    }
    $('upMsg').hidden = true; $('upOut').hidden = false;
  } catch (_) { say($('upMsg'), 'The update list could not be read.', 'err'); updatesLoaded = false; }
}
