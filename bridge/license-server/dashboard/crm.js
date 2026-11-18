'use strict';
// CRM page: customers and their notes, and the staff accounts. Talks to /admin/crm/* and /admin/staff with the staff token from this tab.
// Every value from the server is put on the page as text (textContent), never as HTML.
(() => {
  const $ = id => document.getElementById(id);
  const token = (() => { try { return sessionStorage.getItem('amfx1-licence-token') || ''; } catch (_) { return ''; } })();
  if (!token) { location.replace('./login'); return; }
  let statuses = [], current = null;
  const modal = new bootstrap.Modal($('custModal'));
  const day = t => t ? new Date(t).toLocaleString() : '—';
  const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; };
  function say(text, kind = 'danger') { const a = $('alert'); a.className = 'alert alert-' + kind; a.textContent = text; if (kind !== 'danger') setTimeout(() => a.classList.add('d-none'), 3000); }
  async function api(path, method = 'GET', body) {
    const r = await fetch(path, { method, headers: { authorization: 'Bearer ' + token, ...(body ? { 'content-type': 'application/json' } : {}) }, body: body ? JSON.stringify(body) : undefined, credentials: 'omit' });
    const j = await r.json().catch(() => ({}));
    if (r.status === 401) { try { sessionStorage.removeItem('amfx1-licence-token'); } catch (_) {} location.replace('./login'); throw new Error('signed out'); }
    if (!r.ok || j.ok === false) throw new Error(j.error || ('error ' + r.status));
    return j;
  }
  const badge = s => ({ lead: 'secondary', trial: 'info', customer: 'success', reseller: 'warning', lost: 'dark' })[s] || 'secondary';

  async function loadStats() {
    const { stats } = await api('crm/stats'); const box = $('stats'); box.replaceChildren();
    for (const [k, v] of Object.entries(stats)) { const c = el('div', 'col-6 col-md-2'); const card = el('div', 'bg-body border rounded p-2 text-center'); card.append(el('div', 'small text-secondary text-uppercase', k), el('div', 'fs-4 fw-bold', String(v))); c.append(card); box.append(c); }
  }
  async function loadList() {
    const q = encodeURIComponent($('q').value.trim()), s = encodeURIComponent($('fStatus').value);
    const { customers } = await api(`crm/customers?q=${q}&status=${s}`); const tb = $('rows'); tb.replaceChildren();
    if (!customers.length) { const tr = el('tr'); const td = el('td', 'text-secondary text-center py-3', 'No customers yet.'); td.colSpan = 6; tr.append(td); tb.append(tr); return; }
    for (const c of customers) {
      const tr = el('tr'); tr.style.cursor = 'pointer'; tr.tabIndex = 0;
      const st = el('td'); st.append(el('span', 'badge text-bg-' + badge(c.status), c.status));
      tr.append(el('td', 'fw-semibold', c.name), el('td', '', c.company || '—'), el('td', '', c.email || '—'), el('td', '', c.phone || '—'), st, el('td', 'small text-secondary', day(c.updated)));
      const open = () => openCustomer(c.id); tr.addEventListener('click', open); tr.addEventListener('keydown', e => { if (e.key === 'Enter') open(); });
      tb.append(tr);
    }
  }
  function fill(c) {
    $('cName').value = c.name || ''; $('cCompany').value = c.company || ''; $('cEmail').value = c.email || ''; $('cPhone').value = c.phone || '';
    $('cCountry').value = c.country || ''; $('cTags').value = c.tags || ''; $('cStatus').value = c.status || 'lead'; $('cFollow').value = c.followUp || '';
    $('keyBox').classList.toggle('d-none', !c.id); $('kOut').classList.add('d-none'); $('kKey').textContent = ''; paintKeys(c.licences || []);
    $('custTitle').textContent = c.id ? c.name : 'New customer'; $('delBtn').classList.toggle('d-none', !c.id); $('noteBox').classList.toggle('d-none', !c.id);
    const ul = $('notes'); ul.replaceChildren();
    for (const n of c.notes || []) { const li = el('li', 'list-group-item px-0'); li.append(el('div', '', n.text), el('div', 'text-secondary', day(n.at) + ' · ' + n.by)); ul.append(li); }
  }
  // the customer's keys (listed under the customer's e-mail) with their state
  function paintKeys(list) {
    const tb = $('keyRows'); tb.replaceChildren(); $('keyNone').classList.toggle('d-none', list.length > 0); tb.closest('table').classList.toggle('d-none', !list.length);
    const tone = { active: 'success', expired: 'secondary', revoked: 'danger' };
    for (const k of list) { const st = el('td'); st.append(el('span', 'badge text-bg-' + (tone[k.state] || 'secondary'), k.state + (k.trial ? ' · trial' : '')));
      const tr = el('tr'); tr.append(el('td', 'font-monospace', k.id), el('td', '', String(k.plan || '').toUpperCase()), el('td', '', k.expires ? new Date(k.expires).toLocaleDateString() : 'no end'), el('td', '', (k.devices || 0) + ' / ' + (k.seats || 1)), st); tb.append(tr); }
  }
  // the issue form starts from Settings (plan, days, computers)
  let defaults = null;
  async function loadDefaults() { if (defaults) return defaults; try { defaults = (await api('settings')).settings || {}; } catch (_) { defaults = {}; } return defaults; }
  async function prefillKey() { const d = await loadDefaults(); if (d.defaultPlan) $('kPlan').value = d.defaultPlan; $('kDays').value = Number.isInteger(d.defaultDays) ? d.defaultDays : 365; $('kSeats').value = Number.isInteger(d.defaultSeats) ? d.defaultSeats : 1; }
  $('kIssue').addEventListener('click', async () => {
    if (!current || !current.id) return;
    if (!current.email) { say('Save an e-mail for this customer first: the key is listed under it.'); return; }
    if (!confirm('Issue a ' + $('kPlan').value.toUpperCase() + ' key for ' + current.name + '?')) return;
    try { const r = await api('crm/customers/' + encodeURIComponent(current.id) + '/licence', 'POST', { plan: $('kPlan').value, days: Number($('kDays').value), seats: Number($('kSeats').value) });
      const keep = { id: r.id, key: r.key, pk: r.productKey }; await openCustomer(current.id);
      $('kId').textContent = keep.id; $('kKey').textContent = keep.key; $('kPk').textContent = keep.pk ? 'Product key: ' + keep.pk : ''; $('kOut').classList.remove('d-none'); say('Key ' + keep.id + ' issued.', 'success');
    } catch (e) { say(e.message); }
  });
  // FOLLOW-UPS: due today and overdue, by this browser's own day
  const localDay = () => { const d = new Date(); return d.getFullYear() + '-' + String(d.getMonth() + 1).padStart(2, '0') + '-' + String(d.getDate()).padStart(2, '0'); };
  async function loadDue() {
    const r = await api('crm/due?today=' + localDay());
    const fill = (ul, list, late) => { ul.replaceChildren(); if (!list.length) { ul.append(el('li', 'list-group-item px-0 text-secondary', 'None.')); return; }
      for (const c of list) { const li = el('li', 'list-group-item list-group-item-action px-0'); li.style.cursor = 'pointer'; li.tabIndex = 0;
        li.append(el('span', 'fw-semibold', c.name), el('span', 'text-secondary', (c.company ? ' · ' + c.company : '') + (late ? ' · since ' + c.followUp : '')));
        const open = () => openCustomer(c.id); li.addEventListener('click', open); li.addEventListener('keydown', e => { if (e.key === 'Enter') open(); }); ul.append(li); } };
    fill($('dueToday'), r.dueToday, false); fill($('dueLate'), r.overdue, true);
    const n = r.dueToday.length + r.overdue.length; $('dueCount').textContent = String(n); $('dueSec').classList.toggle('d-none', n === 0);
  }
  async function openCustomer(id) { const { customer } = await api('crm/customers/' + encodeURIComponent(id)); current = customer; fill(customer); await prefillKey(); modal.show(); }
  $('newBtn').addEventListener('click', () => { current = {}; fill({}); modal.show(); });
  $('saveBtn').addEventListener('click', async () => {
    const body = { name: $('cName').value, company: $('cCompany').value, email: $('cEmail').value, phone: $('cPhone').value, country: $('cCountry').value, tags: $('cTags').value, status: $('cStatus').value, followUp: $('cFollow').value };
    try { if (current && current.id) await api('crm/customers/' + encodeURIComponent(current.id), 'PUT', body); else await api('crm/customers', 'POST', body); modal.hide(); say('Saved.', 'success'); await Promise.all([loadList(), loadStats(), loadDue()]); } catch (e) { say(e.message); }
  });
  $('delBtn').addEventListener('click', async () => {
    if (!current || !current.id || !confirm('Delete ' + current.name + ' and all notes?')) return;
    try { await api('crm/customers/' + encodeURIComponent(current.id), 'DELETE'); modal.hide(); say('Deleted.', 'success'); await Promise.all([loadList(), loadStats(), loadDue()]); } catch (e) { say(e.message); }
  });
  $('noteAdd').addEventListener('click', async () => {
    const t = $('noteText').value.trim(); if (!t || !current || !current.id) return;
    try { await api('crm/customers/' + encodeURIComponent(current.id) + '/notes', 'POST', { text: t }); $('noteText').value = ''; await openCustomer(current.id); loadList(); } catch (e) { say(e.message); }
  });
  let timer = 0; $('q').addEventListener('input', () => { clearTimeout(timer); timer = setTimeout(() => loadList().catch(e => say(e.message)), 250); });
  $('fStatus').addEventListener('change', () => loadList().catch(e => say(e.message)));

  async function loadStaff() {
    const { staff } = await api('staff'); const tb = $('staffRows'); tb.replaceChildren();
    for (const s of staff) {
      const tr = el('tr'); const btn = el('button', 'btn btn-sm ' + (s.disabled ? 'btn-outline-success' : 'btn-outline-danger'), s.disabled ? 'Enable' : 'Disable'); btn.type = 'button';
      btn.addEventListener('click', async () => { try { await api('staff/' + encodeURIComponent(s.id) + (s.disabled ? '/enable' : '/disable'), 'POST', {}); loadStaff(); } catch (e) { say(e.message); } });
      const act = el('td', 'text-end'); act.append(btn);
      tr.append(el('td', '', s.name), el('td', '', s.email), el('td', 'small text-secondary', day(s.last_login)), el('td', '', s.disabled ? 'disabled' : 'active'), act); tb.append(tr);
    }
  }
  $('staffForm').addEventListener('submit', async ev => {
    ev.preventDefault();
    try { await api('staff', 'POST', { name: $('sName').value, email: $('sEmail').value, password: $('sPw').value }); $('sName').value = $('sEmail').value = $('sPw').value = ''; say('Account added.', 'success'); loadStaff(); } catch (e) { say(e.message); }
  });
  const show = staff => { $('custSec').classList.toggle('d-none', staff); $('staffSec').classList.toggle('d-none', !staff); $('staffTab').classList.toggle('active', staff); if (staff) loadStaff().catch(e => say(e.message)); };
  $('staffTab').addEventListener('click', e => { e.preventDefault(); show(true); });
  $('out').addEventListener('click', async () => { try { await fetch('auth/logout', { method: 'POST', headers: { authorization: 'Bearer ' + token }, credentials: 'omit' }); } catch (_) { /* signed out here either way */ } try { sessionStorage.removeItem('amfx1-licence-token'); sessionStorage.removeItem('ota-token'); sessionStorage.removeItem('amfx1-staff'); } catch (_) {} location.replace('./login'); });

  (async () => {
    try {
      const me = await api('auth/me'); $('who').textContent = me.staff ? me.staff.name + ' · ' + me.staff.email : 'admin token';
      statuses = (await api('crm/stats')).statuses || ['lead', 'trial', 'customer', 'reseller', 'lost'];
      for (const s of statuses) { $('fStatus').append(new Option(s, s)); $('cStatus').append(new Option(s, s)); }
      await Promise.all([loadStats(), loadList(), loadDue()]);
      if (location.hash === '#staff') show(true);
    } catch (e) { if (e.message !== 'signed out') say(e.message); }
  })();
})();
