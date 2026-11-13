'use strict';
(() => {
  const $ = id => document.getElementById(id);
  let token = sessionStorage.getItem('amfb') || '', state = null, timer = 0;
  const db = v => v <= 1e-5 ? -100 : 20 * Math.log10(v);
  const height = v => Math.max(0, Math.min(100, (db(v) + 60) / 60 * 100)) + '%';

  async function api(path, body) {
    const r = await fetch(path, { method: body ? 'POST' : 'GET', headers: { ...(token ? { Authorization: 'Bearer ' + token } : {}), ...(body ? { 'Content-Type': 'application/json' } : {}) }, body: body ? JSON.stringify(body) : undefined });
    const j = await r.json().catch(() => ({}));
    if (r.status === 401 && path !== '/api/pair') signedOut('The session ended. Pair again.');
    if (!r.ok) throw Object.assign(new Error(j.error || 'request failed'), { status: r.status });
    return j;
  }
  function signedOut(note) {
    token = ''; sessionStorage.removeItem('amfb'); clearInterval(timer);
    $('desk').hidden = true; $('out').hidden = true; $('pair').hidden = false; $('targetBar').hidden = true;
    $('status').textContent = 'Not paired'; $('pairNote').textContent = note || '';
  }
  function el(tag, cls, text) { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; }

  function build() {
    const desk = $('desk'); desk.replaceChildren();
    state.strips.concat([{ master: true, source: 'MASTER', gainDb: state.masterGainDb }]).forEach((s, i) => {
      const box = el('section', 'strip' + (s.master ? ' master' : '')); box.dataset.i = i;
      const h = el('h2'); h.append(el('span', 'n', s.master ? 'MASTER' : String(i + 1)), document.createElement('br'), document.createTextNode(s.master ? 'stereo bus' : s.source || '—'));
      const meter = el('div', 'meter'); meter.append(el('i'), el('i'));
      const gain = el('input'); gain.type = 'range'; gain.min = -60; gain.max = 12; gain.step = 0.5; gain.value = Math.max(-60, s.gainDb);
      gain.setAttribute('aria-label', (s.master ? 'Master' : 'Strip ' + (i + 1)) + ' level in dB');
      const read = el('div', 'tp', '');
      gain.addEventListener('input', () => { const v = Number(gain.value) <= -60 ? -90 : Number(gain.value); (s.master ? api('/api/master', { gainDb: v }) : api('/api/strip/' + (i + 1), { gainDb: v })).catch(() => {}); });
      box.append(h, meter, read, gain);
      if (!s.master) {
        const comp = el('input'); comp.type = 'range'; comp.min = 0; comp.max = 10; comp.step = 1; comp.value = s.compLevel || 0; comp.setAttribute('aria-label', 'Strip ' + (i + 1) + ' compression level');
        comp.addEventListener('input', () => api('/api/strip/' + (i + 1), { compLevel: Number(comp.value) }).catch(() => {}));
        const sug = el('button', 'sug', 'listening…'); sug.type = 'button'; sug.disabled = true; sug.setAttribute('aria-label', 'Apply the suggested compression level to strip ' + (i + 1));
        sug.addEventListener('click', () => { const lv = Number(sug.dataset.level); if (lv > 0) { comp.value = lv; api('/api/strip/' + (i + 1), { compLevel: lv }).catch(() => {}); } });
        box.append(el('div', 'tp gr', 'GR —'), comp, sug);
        const pan = el('input'); pan.type = 'range'; pan.min = -1; pan.max = 1; pan.step = 0.05; pan.value = s.pan; pan.setAttribute('aria-label', 'Strip ' + (i + 1) + ' balance');
        pan.addEventListener('input', () => api('/api/strip/' + (i + 1), { pan: Number(pan.value) }).catch(() => {}));
        const row = el('div', 'row');
        for (const key of ['mute', 'solo']) {
          const b = el('button', key + (s[key] ? ' on' : ''), key === 'mute' ? 'M' : 'S'); b.type = 'button';
          b.setAttribute('aria-label', (key === 'mute' ? 'Mute' : 'Solo') + ' strip ' + (i + 1)); b.setAttribute('aria-pressed', String(!!s[key]));
          b.addEventListener('click', () => { s[key] = !s[key]; b.classList.toggle('on', s[key]); b.setAttribute('aria-pressed', String(s[key])); api('/api/strip/' + (i + 1), { [key]: s[key] }).catch(() => {}); });
          row.append(b);
        }
        box.append(pan, row);
      }
      desk.append(box);
    });
  }
  async function tick() {
    try {
      const { meters } = await api('/api/meters');
      $('status').textContent = 'Connected';
      document.querySelectorAll('.strip').forEach((box, i) => {
        const m = meters[i]; if (!m) return;
        const bars = box.querySelectorAll('.meter i'); bars[0].style.height = height(m[0]); bars[1].style.height = height(m[1]);
        const tp = box.querySelector('.tp'); tp.textContent = m[4] > 1e-5 ? db(m[4]).toFixed(1) + ' dBTP' : '—'; tp.classList.toggle('over', m[4] > 1);
        const gr = box.querySelector('.gr'); if (gr) gr.textContent = m[8] < -0.05 ? 'GR ' + m[8].toFixed(1) + ' dB' : 'GR —';
        const sg = box.querySelector('.sug'); if (sg) { const lv = m[10]; sg.dataset.level = lv; sg.textContent = lv < 0 ? 'listening…' : lv === 0 ? 'no comp needed' : 'suggest ' + lv + ' · apply'; sg.disabled = !(lv > 0); }
        box.classList.toggle('idle', i < 16 && !m[7]);
      });
    } catch (e) { if (e.status === 503) $('status').textContent = 'The mixer engine is not running'; }
  }
  async function enter() {
    try { state = await api('/api/state'); } catch (e) { if (e.status === 503) { $('status').textContent = 'The mixer engine is not running'; setTimeout(enter, 2000); } return; }
    $('pair').hidden = true; $('desk').hidden = false; $('out').hidden = false;
    build(); clearInterval(timer); timer = setInterval(tick, 100);
    $('targetBar').hidden = false;
    for (const b of document.querySelectorAll('#targetBar button')) { b.classList.toggle('on', Number(b.dataset.t) === Math.round(state.compTargetDb || 6));
      b.onclick = () => api('/api/target', { spreadDb: Number(b.dataset.t) }).then(() => { for (const x of document.querySelectorAll('#targetBar button')) x.classList.toggle('on', x === b); }).catch(() => {}); }
  }
  $('pair').addEventListener('submit', async ev => {
    ev.preventDefault(); $('pairNote').textContent = '';
    try { const r = await api('/api/pair', { pin: $('pin').value.trim() }); token = r.token; sessionStorage.setItem('amfb', token); $('pin').value = ''; enter(); }
    catch (e) { $('pairNote').textContent = e.message; }
  });
  $('out').addEventListener('click', () => { api('/api/logout', {}).catch(() => {}); signedOut(''); });
  if (token) enter();
})();
