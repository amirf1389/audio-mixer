'use strict';
// SYSTEM page: health and configuration, the portal settings, staff accounts and their sessions, the activity trail and the CRM export.
// Values from the server are written as text (textContent, Option), never as HTML. The staff token comes from this tab's storage.
(() => {
  const $ = id => document.getElementById(id);
  const token = (() => { try { return sessionStorage.getItem('amfx1-licence-token') || ''; } catch (_) { return ''; } })();
  if (!token) { location.replace('./login'); return; }
  let me = null, settings = {}, schema = {}, audit = [];
  const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; };
  const when = t => t ? new Date(t).toLocaleString() : '—';
  const bytes = n => n > 1048576 ? (n / 1048576).toFixed(1) + ' MB' : n > 1024 ? (n / 1024).toFixed(1) + ' KB' : n + ' B';
  const dur = s => { const d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600), m = Math.floor(s % 3600 / 60); return (d ? d + 'd ' : '') + h + 'h ' + m + 'm'; };
  function say(text, kind = 'danger') { const a = $('alert'); a.className = 'alert alert-' + kind; a.textContent = text; if (kind !== 'danger') setTimeout(() => a.classList.add('d-none'), 3000); }
  function signOut() { try { sessionStorage.removeItem('amfx1-licence-token'); sessionStorage.removeItem('ota-token'); sessionStorage.removeItem('amfx1-staff'); } catch (_) {} location.replace('./login'); }
  async function api(path, method = 'GET', body) {
    const r = await fetch(path, { method, headers: { authorization: 'Bearer ' + token, ...(body ? { 'content-type': 'application/json' } : {}) }, body: body ? JSON.stringify(body) : undefined, credentials: 'omit' });
    const j = await r.json().catch(() => ({}));
    if (r.status === 401) { signOut(); throw new Error('signed out'); }
    if (!r.ok || j.ok === false) throw new Error(j.error || ('error ' + r.status));
    return j;
  }
  const dl = (box, pairs) => { box.replaceChildren(); for (const [k, v] of pairs) box.append(el('dt', 'col-5 text-secondary fw-normal', k), el('dd', 'col-7 mb-1', String(v))); };

  async function overview() {
    const s = await api('system/info');
    $('who').textContent = s.you.kind === 'staff' ? s.you.name + ' · ' + s.you.email : 'admin token'; me = s.you;
    const cards = [['Licences', s.data.licences, 'fa-key'], ['Active', s.data.active, 'fa-circle-check'], ['Computers', s.data.devices, 'fa-desktop'],
                   ['Customers', s.data.db.customers, 'fa-address-book'], ['Staff', s.data.db.staff, 'fa-users'], ['Activity chain', s.data.auditChain.ok ? 'intact' : 'BROKEN at ' + s.data.auditChain.brokenAt, s.data.auditChain.ok ? 'fa-link' : 'fa-link-slash']];
    const box = $('cards'); box.replaceChildren();
    for (const [k, v, ic] of cards) { const c = el('div', 'col-6 col-md-4 col-xl-2'), card = el('div', 'bg-body border rounded p-2 h-100'); const i = el('i', 'fa-solid ' + ic + ' text-warning me-1'); i.setAttribute('aria-hidden', 'true');
      const t = el('div', 'small text-secondary text-uppercase'); t.append(i, document.createTextNode(k)); card.append(t, el('div', 'fs-5 fw-bold' + (String(v).startsWith('BROKEN') ? ' text-danger' : ''), String(v))); c.append(card); box.append(c); }
    dl($('srv'), [['Node', s.server.node], ['System', s.server.platform], ['Running for', dur(s.server.uptimeS)], ['Started', when(s.server.upSince)], ['Memory', s.server.memoryMB + ' MB'],
      ['Licence file', bytes(s.data.licenceFileBytes)], ['Activity file', bytes(s.data.auditFileBytes)], ['Staff database', bytes(s.data.db.bytes) + ' · ' + s.data.db.integrity], ['Notes', s.data.db.notes],
      ['Last backup', s.data.db.lastBackup ? new Date(s.data.db.lastBackup.at).toLocaleString() + ' · ' + s.data.db.lastBackup.by : 'never']]);
    dl($('cfg'), [['Shared sign-in with OTA', s.config.sharedSignIn ? 'on' : 'off (PORTAL_DB / PORTAL_JWT_SECRET not set)'], ['Customer data key', s.config.crmKey], ['Licence data key', s.config.dataKey],
      ['Behind a proxy', s.config.trustProxy ? 'yes (X-Forwarded-For trusted)' : 'no'], ['Update feed', s.config.updateFeed], ['Computers per key', s.config.maxDevices], ['Plans', s.config.plans.join(', ')]]);
  }

  const LABELS = { orgName: 'Company name', supportEmail: 'Support e-mail', defaultPlan: 'Default plan for new keys', defaultDays: 'Default days for new keys (0 = no end)', defaultSeats: 'Default computers per new key',
                   expiringWarnDays: 'Warn this many days before a key ends', portalNotice: 'Notice on the customer portal', maintenance: 'Maintenance mode (shown on the customer portal)' };
  function paintSettings() {
    const box = $('setFields'); box.replaceChildren();
    for (const [k, d] of Object.entries(schema)) {
      const col = el('div', d.kind === 'text' && d.max > 100 ? 'col-12' : 'col-md-6'), id = 'set_' + k;
      if (d.kind === 'bool') {
        const w = el('div', 'form-check form-switch mt-4'), inp = el('input', 'form-check-input'); inp.type = 'checkbox'; inp.id = id; inp.checked = !!settings[k]; inp.setAttribute('role', 'switch');
        const lab = el('label', 'form-check-label', LABELS[k] || k); lab.htmlFor = id; w.append(inp, lab); col.append(w);
      } else {
        const lab = el('label', 'form-label small', LABELS[k] || k); lab.htmlFor = id; let inp;
        if (d.kind === 'choice') { inp = el('select', 'form-select form-select-sm'); for (const ch of d.choices) inp.append(new Option(ch, ch, false, settings[k] === ch)); }
        else if (d.kind === 'text' && d.max > 100) { inp = el('textarea', 'form-control form-control-sm'); inp.rows = 2; inp.maxLength = d.max; inp.value = settings[k] || ''; }
        else { inp = el('input', 'form-control form-control-sm'); inp.type = d.kind === 'int' ? 'number' : d.kind === 'email' ? 'email' : 'text'; if (d.kind === 'int') { inp.min = d.min; inp.max = d.max; inp.step = 1; } if (d.max) inp.maxLength = d.max; inp.value = settings[k] ?? ''; }
        inp.id = id; col.append(lab, inp);
      }
      box.append(col);
    }
    $('setNote').textContent = '';
  }
  async function loadSettings() { const r = await api('settings'); settings = r.settings; schema = r.schema; paintSettings(); }
  $('setForm').addEventListener('submit', async ev => {
    ev.preventDefault(); const body = {};
    for (const [k, d] of Object.entries(schema)) { const inp = $('set_' + k); const v = d.kind === 'bool' ? inp.checked : d.kind === 'int' ? Number(inp.value) : inp.value; if (JSON.stringify(v) !== JSON.stringify(settings[k])) body[k] = v; }
    if (!Object.keys(body).length) { $('setNote').textContent = 'Nothing changed.'; return; }
    try { const r = await api('settings', 'PUT', body); settings = r.settings; paintSettings(); say('Saved: ' + r.changed.join(', '), 'success'); } catch (e) { say(e.message); }
  });
  $('setReset').addEventListener('click', paintSettings);

  async function loadStaff() {
    const { staff } = await api('staff'); const tb = $('staffRows'); tb.replaceChildren();
    for (const s of staff) {
      const tr = el('tr'), act = el('td', 'text-end text-nowrap'), self = me && me.kind === 'staff' && me.id === s.id;
      const b1 = el('button', 'btn btn-sm btn-outline-secondary me-1', 'End sessions'); b1.type = 'button'; b1.title = 'Signs this account out everywhere';
      b1.addEventListener('click', async () => { if (!confirm('Sign ' + s.name + ' out everywhere?')) return; try { await api('staff/' + encodeURIComponent(s.id) + '/end-sessions', 'POST', {}); if (self) signOut(); else say(s.name + ' is signed out everywhere.', 'success'); } catch (e) { say(e.message); } });
      const b2 = el('button', 'btn btn-sm ' + (s.disabled ? 'btn-outline-success' : 'btn-outline-danger'), s.disabled ? 'Enable' : 'Disable'); b2.type = 'button'; b2.disabled = self && !s.disabled;
      b2.addEventListener('click', async () => { try { await api('staff/' + encodeURIComponent(s.id) + (s.disabled ? '/enable' : '/disable'), 'POST', {}); loadStaff(); } catch (e) { say(e.message); } });
      act.append(b1, b2);
      const name = el('td', '', s.name); if (self) name.append(el('span', 'badge text-bg-warning ms-1', 'you'));
      tr.append(name, el('td', '', s.email), el('td', 'small text-secondary', when(s.last_login)), el('td', '', s.disabled ? 'disabled' : 'active'), act); tb.append(tr);
    }
  }
  $('staffForm').addEventListener('submit', async ev => { ev.preventDefault();
    try { await api('staff', 'POST', { name: $('sName').value, email: $('sEmail').value, password: $('sPw').value }); $('sName').value = $('sEmail').value = $('sPw').value = ''; say('Account added.', 'success'); loadStaff(); } catch (e) { say(e.message); } });
  $('pwForm').addEventListener('submit', async ev => { ev.preventDefault();
    try { await api('auth/password', 'POST', { current: $('pCur').value, password: $('pNew').value }); signOut(); } catch (e) { say(e.message); } finally { $('pCur').value = $('pNew').value = ''; } });

  function paintAudit() {
    const f = $('aFilter').value.trim().toLowerCase(), tb = $('auditRows'); tb.replaceChildren();
    const rows = audit.filter(a => !f || [a.action, a.id, a.ip, a.detail].some(v => String(v || '').toLowerCase().includes(f)));
    if (!rows.length) { const tr = el('tr'), td = el('td', 'text-secondary text-center py-3', 'Nothing to show.'); td.colSpan = 5; tr.append(td); tb.append(tr); return; }
    for (const a of rows) { const tr = el('tr'), act = el('td'); act.append(el('span', 'badge ' + (/denied|refused|broken/.test(a.action) ? 'text-bg-danger' : /delete|revoke|disable/.test(a.action) ? 'text-bg-warning' : 'text-bg-secondary'), a.action));
      tr.append(el('td', 'text-nowrap', when(a.t)), act, el('td', 'font-monospace', a.id || ''), el('td', 'font-monospace', a.ip || ''), el('td', '', a.detail || '')); tb.append(tr); }
  }
  async function loadAudit() { audit = (await api('audit?limit=' + encodeURIComponent($('aLimit').value))).entries || []; paintAudit(); }
  $('aFilter').addEventListener('input', paintAudit);
  $('aLimit').addEventListener('change', () => loadAudit().catch(e => say(e.message)));
  $('aVerify').addEventListener('click', async () => { try { const { chain } = await api('audit/verify'); chain.ok ? say('The activity chain is intact: ' + chain.entries + ' entries, none changed or removed.', 'success') : say('The activity chain is broken at entry ' + chain.brokenAt + ': the file was edited.'); } catch (e) { say(e.message); } });
  $('csv').addEventListener('click', async ev => {   // the token goes in a header, so the file is fetched here and handed to the browser as a download
    ev.preventDefault();
    try { const r = await fetch('crm/export.csv', { headers: { authorization: 'Bearer ' + token }, credentials: 'omit' }); if (!r.ok) throw new Error('export refused (' + r.status + ')');
      const url = URL.createObjectURL(await r.blob()), a = el('a'); a.href = url; a.download = 'amfx1-customers.csv'; document.body.append(a); a.click(); a.remove(); setTimeout(() => URL.revokeObjectURL(url), 1000); } catch (e) { say(e.message); }
  });
  // BACKUP: a copy of the staff / CRM database (the CRM inside stays encrypted; keep the file somewhere safe)
  $('backup').addEventListener('click', async () => {
    try { const r = await fetch('system/backup', { headers: { authorization: 'Bearer ' + token }, credentials: 'omit', cache: 'no-store' }); if (!r.ok) throw new Error('backup refused (' + r.status + ')');
      const name = (/filename="([^"]+)"/.exec(r.headers.get('content-disposition') || '') || [])[1] || 'amfx1-portal.db';
      const url = URL.createObjectURL(await r.blob()), a = el('a'); a.href = url; a.download = name; document.body.append(a); a.click(); a.remove(); setTimeout(() => URL.revokeObjectURL(url), 1000);
      say('Backup downloaded: ' + name + '. Keep it safe: it holds the staff accounts and the (encrypted) customers.', 'success'); overview().catch(() => {});
    } catch (e) { say(e.message); }
  });
  $('refresh').addEventListener('click', () => overview().catch(e => say(e.message)));
  $('staffTabBtn').addEventListener('shown.bs.tab', () => loadStaff().catch(e => say(e.message)));
  $('auditTabBtn').addEventListener('shown.bs.tab', () => loadAudit().catch(e => say(e.message)));
  $('out').addEventListener('click', async () => { try { await fetch('auth/logout', { method: 'POST', headers: { authorization: 'Bearer ' + token }, credentials: 'omit' }); } catch (_) {} signOut(); });

  (async () => { try { await overview(); await loadSettings(); } catch (e) { if (e.message !== 'signed out') say(e.message); } })();
})();
