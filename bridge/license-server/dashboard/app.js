/* Licence management page of the AmF X1 licence server.
   - Talks only to the admin API of the server it was loaded from, with the admin token typed in (kept in this tab's memory; "keep until this
     tab is closed" uses sessionStorage, never localStorage).
   - Everything from the server is written as text nodes: no HTML from the server is ever parsed. No external script, style or font.
   - A key is shown once, right after it is issued; the server does not keep it. */
(function () {
  'use strict';
  const $ = id => document.getElementById(id);
  let token = '', licences = [], maxDevices = 4, busy = false;

  const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; };
  const say = (node, text, kind) => { node.textContent = text || ''; node.className = 'msg' + (kind ? ' ' + kind : ''); };
  const day = t => t ? new Date(t).toISOString().slice(0, 10) : '—';
  const when = t => new Date(t).toISOString().replace('T', ' ').slice(0, 19);

  async function api(method, path, body) {
    let res;
    try { res = await fetch(path, { method, headers: { authorization: 'Bearer ' + token, ...(body ? { 'content-type': 'application/json' } : {}) }, body: body ? JSON.stringify(body) : undefined, cache: 'no-store' }); }
    catch (_) { throw new Error('The server did not answer. Check that it is running and try again.'); }
    let data = {}; try { data = await res.json(); } catch (_) { /* an empty or non-JSON answer */ }
    if (res.status === 401) { signOut('The token was not accepted.'); throw new Error('The token was not accepted.'); }
    if (res.status === 429) throw new Error('Too many requests. Wait ' + (res.headers.get('retry-after') || 'a few') + ' seconds and try again.');
    if (!res.ok || data.ok === false) throw new Error(data.error || ('The server answered ' + res.status + '.'));
    return data;
  }
  // one action at a time: the button that started it is disabled until the answer is in, so a double click cannot issue or revoke twice
  async function guard(button, fn) {
    if (busy) return; busy = true; const label = button.textContent; button.disabled = true;
    try { await fn(); } finally { busy = false; button.disabled = false; button.textContent = label; }
  }

  function signOut(message) {
    token = ''; try { sessionStorage.removeItem('amfx1-licence-token'); } catch (_) { /* storage may be off */ }
    licences = []; $('rows').replaceChildren(); $('auditRows').replaceChildren(); $('stats').replaceChildren(); hideKey();
    $('app').hidden = true; $('login').hidden = false; $('signOut').hidden = true; $('who').textContent = ''; $('token').value = '';
    say($('loginMsg'), message || '', message ? 'err' : ''); $('token').focus();
  }
  async function signIn(value, remember) {
    token = value.trim();
    if (token.length < 32) { token = ''; say($('loginMsg'), 'The admin token is at least 32 characters long.', 'err'); return; }
    say($('loginMsg'), 'Checking …');
    try { await loadAll(); } catch (e) { if (token) say($('loginMsg'), e.message, 'err'); token = ''; return; }
    if (remember) { try { sessionStorage.setItem('amfx1-licence-token', token); } catch (_) { /* storage may be off */ } }
    say($('loginMsg'), ''); $('login').hidden = true; $('app').hidden = false; $('signOut').hidden = false; $('who').textContent = location.host; $('iName').focus();
  }

  async function loadAll() { await Promise.all([loadLicences(), loadStats(), loadAudit()]); }
  async function loadStats() {
    const s = await api('GET', '/admin/stats'), box = $('stats'); box.replaceChildren();
    for (const [n, label] of [[s.total, 'keys on record'], [s.active, 'active'], [s.expiringSoon, 'expire within 30 days'], [s.expired, 'expired'], [s.revoked, 'revoked'], [s.devices, 'computers activated']]) {
      const c = el('div', 'stat'); c.append(el('b', '', String(n)), el('span', '', label)); box.append(c);
    }
  }
  async function loadLicences() {
    const r = await api('GET', '/admin/licenses'); licences = r.licenses || []; maxDevices = r.maxDevices || 4;
    const plan = $('iPlan'), had = plan.value; plan.replaceChildren(); for (const p of r.plans || []) { const o = el('option', '', p); o.value = p; plan.append(o); } if (had) plan.value = had;
    $('iSeats').max = String(maxDevices); render();
  }
  async function loadAudit() {
    const r = await api('GET', '/admin/audit?limit=100'), body = $('auditRows'); body.replaceChildren();
    for (const e of r.entries || []) { const tr = el('tr'); tr.append(el('td', '', when(e.t)), el('td', '', e.action), el('td', '', e.id || '—'), el('td', '', e.ip || '—'), el('td', '', e.detail || '')); body.append(tr); }
    if (!(r.entries || []).length) { const tr = el('tr'), td = el('td', 'muted', 'Nothing yet.'); td.colSpan = 5; tr.append(td); body.append(tr); }
  }

  function shown() {
    const q = $('filter').value.trim().toLowerCase(), st = $('stateFilter').value;
    return licences.filter(l => (!st || l.state === st) && (!q || [l.name, l.email, l.id, l.note, l.machine, ...l.devices].some(v => String(v || '').toLowerCase().includes(q))));
  }
  function render() {
    const body = $('rows'), list = shown(); body.replaceChildren();
    say($('listMsg'), list.length === licences.length ? licences.length + ' key(s).' : list.length + ' of ' + licences.length + ' key(s) shown.');
    if (!list.length) { const tr = el('tr'), td = el('td', 'muted', licences.length ? 'No key matches the search.' : 'No key has been issued yet. Use the form above.'); td.colSpan = 7; tr.append(td); body.append(tr); return; }
    for (const l of list) {
      const tr = el('tr');
      const who = el('td'); who.append(el('div', '', l.name || (l.readable ? '(no name on record)' : '(details unreadable: another data key)')));
      if (l.email) who.append(el('div', 'sub', l.email)); who.append(el('div', 'sub', 'id ' + l.id + (l.machine ? ' · locked to ' + l.machine : ''))); if (l.note) who.append(el('div', 'sub', l.note));
      const devs = el('td'); devs.append(el('div', 'sub', l.devices.length + ' of ' + (l.seats || maxDevices)));
      for (const m of l.devices) { const row = el('div', 'dev'); row.append(el('span', '', m)); const b = el('button', 'ghost small', 'Remove'); b.type = 'button'; b.title = 'Free this computer\'s seat'; b.addEventListener('click', () => removeDevice(b, l, m)); row.append(b); devs.append(row); }
      const state = el('td'); state.append(el('span', 'tag ' + l.state, l.state)); if (l.state === 'revoked') state.append(el('div', 'sub', day(l.revoked) + (l.reason ? ' · ' + l.reason : '')));
      const acts = el('td', 'acts');
      if (l.state === 'revoked') { const b = el('button', 'ghost small', 'Restore'); b.type = 'button'; b.addEventListener('click', () => restore(b, l)); acts.append(b); }
      else { const b = el('button', 'danger small', 'Revoke'); b.type = 'button'; b.addEventListener('click', () => revoke(b, l)); acts.append(b); }
      tr.append(who, el('td', '', l.plan || '—'), el('td', '', day(l.issued)), el('td', '', l.expires ? day(l.expires) : 'never'), devs, state, acts); body.append(tr);
    }
  }

  const revoke = (b, l) => guard(b, async () => {
    const reason = window.prompt('Revoke the key of "' + (l.name || l.id) + '"?\nIt can no longer be activated on a computer. Reason (optional):', '');
    if (reason === null) return;
    b.textContent = 'Revoking …';
    try { await api('POST', '/admin/licenses/' + encodeURIComponent(l.id) + '/revoke', { reason }); await loadAll(); say($('listMsg'), 'Revoked ' + l.id + '.', 'ok'); } catch (e) { say($('listMsg'), e.message, 'err'); }
  });
  const restore = (b, l) => guard(b, async () => {
    if (!window.confirm('Restore the key of "' + (l.name || l.id) + '"? It will work again.')) return;
    b.textContent = 'Restoring …';
    try { await api('POST', '/admin/licenses/' + encodeURIComponent(l.id) + '/restore', {}); await loadAll(); say($('listMsg'), 'Restored ' + l.id + '.', 'ok'); } catch (e) { say($('listMsg'), e.message, 'err'); }
  });
  const removeDevice = (b, l, m) => guard(b, async () => {
    if (!window.confirm('Remove computer ' + m + ' from this key? Its seat becomes free.')) return;
    try { await api('DELETE', '/admin/licenses/' + encodeURIComponent(l.id) + '/devices/' + encodeURIComponent(m)); await loadAll(); say($('listMsg'), 'Removed ' + m + '.', 'ok'); } catch (e) { say($('listMsg'), e.message, 'err'); }
  });

  function hideKey() { $('keyText').value = ''; $('newKey').hidden = true; say($('copyMsg'), ''); }
  async function copyKey() {
    const t = $('keyText');
    try { await navigator.clipboard.writeText(t.value); say($('copyMsg'), 'Copied.', 'ok'); }
    catch (_) { t.focus(); t.select(); say($('copyMsg'), 'Copying is blocked here: the key is selected, press Ctrl+C.', 'err'); }
  }
  function exportCsv() {
    const q = v => '"' + String(v === null || v === undefined ? '' : v).replace(/"/g, '""').replace(/^([=+\-@])/, "'$1") + '"';   // a cell must never start a formula
    const lines = [['id', 'name', 'email', 'plan', 'issued', 'expires', 'seats', 'computers', 'state', 'note'].join(',')];
    for (const l of shown()) lines.push([l.id, l.name, l.email, l.plan, day(l.issued), l.expires ? day(l.expires) : 'never', l.seats, l.devices.join(' '), l.state, l.note].map(q).join(','));
    const a = el('a'); a.href = URL.createObjectURL(new Blob([lines.join('\r\n') + '\r\n'], { type: 'text/csv' })); a.download = 'licences-' + day(Date.now()) + '.csv';
    document.body.append(a); a.click(); a.remove(); setTimeout(() => URL.revokeObjectURL(a.href), 1000);
    say($('listMsg'), 'Exported ' + (lines.length - 1) + ' key(s). The file holds customer names and e-mails: keep it safe.', 'ok');
  }

  $('loginForm').addEventListener('submit', e => { e.preventDefault(); guard($('signIn'), () => signIn($('token').value, $('remember').checked)); });
  $('signOut').addEventListener('click', () => signOut(''));
  $('issueForm').addEventListener('submit', e => {
    e.preventDefault();
    guard($('issueBtn'), async () => {
      const body = { name: $('iName').value.trim(), plan: $('iPlan').value, days: Number($('iDays').value), seats: Number($('iSeats').value) };
      if ($('iEmail').value.trim()) body.email = $('iEmail').value.trim(); if ($('iMachine').value.trim()) body.machine = $('iMachine').value.trim(); if ($('iNote').value.trim()) body.note = $('iNote').value.trim();
      if (!body.name) { say($('issueMsg'), 'Type the customer name.', 'err'); return; }
      $('issueBtn').textContent = 'Issuing …'; say($('issueMsg'), '');
      try {
        const r = await api('POST', '/admin/licenses', body);
        $('keyText').value = r.key; $('newKey').hidden = false; say($('issueMsg'), 'Issued ' + r.id + '.', 'ok'); $('issueForm').reset(); $('keyText').focus(); $('keyText').select();
        await loadAll();
      } catch (err) { say($('issueMsg'), err.message, 'err'); }
    });
  });
  $('copyKey').addEventListener('click', copyKey);
  $('hideKey').addEventListener('click', hideKey);
  $('filter').addEventListener('input', render);
  $('stateFilter').addEventListener('change', render);
  $('refresh').addEventListener('click', () => guard($('refresh'), async () => { try { await loadAll(); } catch (e) { say($('listMsg'), e.message, 'err'); } }));
  $('exportCsv').addEventListener('click', exportCsv);
  $('auditRefresh').addEventListener('click', () => guard($('auditRefresh'), async () => { try { await loadAudit(); say($('auditMsg'), 'Up to date.', 'ok'); } catch (e) { say($('auditMsg'), e.message, 'err'); } }));
  $('auditVerify').addEventListener('click', () => guard($('auditVerify'), async () => {
    try { const r = await api('GET', '/admin/audit/verify'); say($('auditMsg'), r.chain.ok ? 'Intact: ' + r.chain.entries + ' entries, none removed or changed.' : 'BROKEN at entry ' + r.chain.brokenAt + ': the log was edited.', r.chain.ok ? 'ok' : 'err'); } catch (e) { say($('auditMsg'), e.message, 'err'); }
  }));

  let saved = ''; try { saved = sessionStorage.getItem('amfx1-licence-token') || ''; } catch (_) { /* storage may be off */ }
  if (saved) { $('remember').checked = true; signIn(saved, true); } else $('token').focus();
})();
