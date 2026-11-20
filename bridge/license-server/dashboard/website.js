'use strict';
// WEBSITE page: which release the customer portal offers for download, which of its files, a headline and a note.
// Save keeps a draft; PUBLISH makes it public. Values from the server are written as text, never as HTML.
(() => {
  const $ = id => document.getElementById(id);
  const token = (() => { try { return sessionStorage.getItem('amfx1-licence-token') || ''; } catch (_) { return ''; } })();
  if (!token) { location.replace('./login'); return; }
  let data = null, hidden = new Set(), dirty = false;
  const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; };
  const when = t => t ? new Date(t).toLocaleString() : '—';
  const mb = n => (n / 1048576).toFixed(1) + ' MB';
  function say(text, kind = 'danger') { const a = $('alert'); a.className = 'alert alert-' + kind; a.textContent = text; if (kind !== 'danger') setTimeout(() => a.classList.add('d-none'), 3000); }
  function signOut() { try { sessionStorage.removeItem('amfx1-licence-token'); sessionStorage.removeItem('ota-token'); sessionStorage.removeItem('amfx1-staff'); } catch (_) {} location.replace('./login'); }
  async function api(path, method = 'GET', body) {
    const r = await fetch(path, { method, headers: { authorization: 'Bearer ' + token, ...(body ? { 'content-type': 'application/json' } : {}) }, body: body ? JSON.stringify(body) : undefined, credentials: 'omit' });
    const j = await r.json().catch(() => ({}));
    if (r.status === 401) { signOut(); throw new Error('signed out'); }
    if (!r.ok || j.ok === false) throw new Error(j.error || ('error ' + r.status));
    return j;
  }
  const mark = d => { dirty = d; $('state').textContent = d ? 'Not saved yet.' : (data.updatedAt ? 'Draft saved ' + when(data.updatedAt) + ' by ' + data.updatedBy + '.' : ''); };

  function rows() {
    const tb = $('rows'), v = $('ver').value; tb.replaceChildren();
    const files = data.files.filter(f => f.version === v);
    for (const f of files) {
      const tr = el('tr'), box = el('input', 'form-check-input'); box.type = 'checkbox'; box.checked = !hidden.has(f.name); box.setAttribute('aria-label', 'Show ' + f.name);
      box.addEventListener('change', () => { if (box.checked) hidden.delete(f.name); else hidden.add(f.name); mark(true); });
      const td = el('td'); td.append(box);
      const sha = el('td', 'font-monospace text-break', f.sha256);
      tr.append(td, el('td', '', f.platform === 'windows' ? 'Windows' : 'Android'), el('td', '', f.name), el('td', 'text-nowrap', mb(f.size)), sha); tb.append(tr);
    }
    if (!files.length) { const tr = el('tr'), td = el('td', 'text-secondary', 'No file in the release folder has this version in its name.'); td.colSpan = 5; tr.append(td); tb.append(tr); }
  }
  function paint() {
    const sel = $('ver'); sel.replaceChildren();
    for (const v of data.versions) sel.append(new Option(v.version + (v.date ? ' · ' + v.date : ''), v.version));
    sel.value = data.draft.version; hidden = new Set(data.draft.hidden || []);
    $('headline').value = data.draft.headline || ''; $('note').value = data.draft.note || '';
    $('dir').textContent = data.releasesDir;
    const l = data.live, box = $('live'); box.replaceChildren();
    for (const [k, v] of [['Version', l.version || 'none'], ['Files shown', l.files.length], ['Headline', l.headline || '—'], ['Published', data.publishedAt ? when(data.publishedAt) + ' · ' + data.publishedBy : 'not yet (the newest version is shown)']])
      box.append(el('dt', 'col-4 text-secondary fw-normal', k), el('dd', 'col-8 mb-1', String(v)));
    rows(); mark(false);
  }
  async function load() { data = await api('website/state'); try { const me = await api('auth/me'); $('who').textContent = me.staff ? me.staff.name + ' · ' + me.staff.email : 'admin token'; } catch (_) {} paint(); }
  const draft = () => ({ version: $('ver').value, hidden: [...hidden].filter(n => data.files.some(f => f.name === n)), headline: $('headline').value.trim(), note: $('note').value.trim() });
  async function save() { await api('website/state', 'PUT', draft()); await load(); }

  $('ver').addEventListener('change', () => { rows(); mark(true); });
  for (const id of ['headline', 'note']) $(id).addEventListener('input', () => mark(true));
  $('form').addEventListener('submit', async e => { e.preventDefault(); try { await save(); say('Draft saved. Press PUBLISH to show it on the portal.', 'success'); } catch (err) { say(err.message); } });
  $('publish').addEventListener('click', async () => {
    const d = draft(), shown = data.files.filter(f => f.version === d.version && !hidden.has(f.name)).length;
    if (!confirm('Publish version ' + d.version + ' with ' + shown + ' file(s) on the customer portal?')) return;
    try { if (dirty) await api('website/state', 'PUT', d); await api('website/publish', 'POST'); await load(); say('Published. The portal shows it now.', 'success'); } catch (err) { say(err.message); }
  });
  $('reload').addEventListener('click', () => load().catch(e => say(e.message)));
  $('out').addEventListener('click', async () => { try { await fetch('auth/logout', { method: 'POST', headers: { authorization: 'Bearer ' + token }, credentials: 'omit' }); } catch (_) {} signOut(); });
  // PRESETS: publish a preset pack; the page says whether the apps trust it yet (its sha256 in the signed manifest)
  let chosen = null;
  async function loadPresets() {
    const p = await api('website/presets'), box = $('packLive'); box.replaceChildren();
    const l = p.live, m = p.manifest || {};
    for (const [k, v] of [['Live pack', l ? l.version + (l.builtIn ? ' (the release folder\'s, nothing published)' : '') : 'none'], ['Presets', l ? l.mastering + ' mastering · ' + l.effects + ' effect · ' + l.tone + ' tone' : '—'],
      ['SHA-256', l ? l.sha256 : '—'], ['Published', l && l.publishedAt ? when(l.publishedAt) + ' · ' + l.publishedBy : '—'], ['Signed manifest', m.signed ? m.signed.version + ' · ' + m.signed.sha256 : 'no preset pack signed yet'],
      ['Pending manifest', m.pending ? m.pending.version + ' · ' + m.pending.sha256 : '—'], ['Apps', p.trusted ? 'take this pack (signed)' : 'keep their built-in presets until the owner signs this pack']])
      box.append(el('dt', 'col-3 text-secondary fw-normal', k), el('dd', 'col-9 mb-1 text-break' + (k === 'Apps' ? (p.trusted ? ' text-success' : ' text-warning') : ''), String(v)));
  }
  $('packFile').addEventListener('change', async () => { const f = $('packFile').files[0]; chosen = null; $('packPublish').disabled = true; if (!f) return;
    try { const j = JSON.parse(await f.text()); if (!Array.isArray(j.mastering) || !Array.isArray(j.effects) || !Array.isArray(j.tone)) throw new Error('not a preset pack');
      chosen = j; $('packChosen').textContent = f.name + ': version ' + j.version + ', ' + j.mastering.length + ' mastering, ' + j.effects.length + ' effect, ' + j.tone.length + ' tone presets'; $('packPublish').disabled = false; }
    catch (e) { $('packChosen').textContent = ''; say(f.name + ': ' + e.message); } });
  async function publishPack(body, what) { if (!confirm('Publish ' + what + ' on /portal/presets?')) return;
    try { const r = await api('website/presets', 'POST', body); await loadPresets(); say('Presets published (' + r.live.version + ').' + (r.trusted ? '' : ' Apps take it once the owner signs its SHA-256.'), 'success'); } catch (e) { say(e.message); } }
  $('packPublish').addEventListener('click', () => chosen && publishPack({ pack: chosen }, 'preset pack ' + chosen.version));
  $('packBuiltIn').addEventListener('click', () => publishPack({ fromBuiltIn: true }, 'the release folder\'s preset pack'));
  loadPresets().catch(e => { if (e.message !== 'signed out') say(e.message); });
  load().catch(e => { if (e.message !== 'signed out') say(e.message); });
})();
