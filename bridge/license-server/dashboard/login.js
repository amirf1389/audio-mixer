'use strict';
// Staff sign-in: trades e-mail + password for an 8-hour token, keeps it in this tab only (sessionStorage), and opens the chosen page.
// The licence and OTA dashboards read the same tab storage, so no token has to be typed or copied.
(() => {
  const $ = id => document.getElementById(id);
  fetch('crm', { method: 'HEAD', credentials: 'omit' }).then(r => { if (!r.ok) for (const o of [...$('go').options]) if (o.value === './crm') o.remove(); }, () => {});   // the OTA server has no CRM page
  $('f').addEventListener('submit', async ev => {
    ev.preventDefault(); $('msg').textContent = '';
    try {
      const r = await fetch('auth/login', { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ email: $('email').value.trim(), password: $('pw').value }), credentials: 'omit' });
      const j = await r.json().catch(() => ({}));
      if (!r.ok || !j.token) { $('msg').textContent = r.status === 429 ? 'Too many tries: wait a minute.' : (j.error || 'Sign-in failed.'); return; }
      try { sessionStorage.setItem('amfx1-licence-token', j.token); sessionStorage.setItem('ota-token', j.token); sessionStorage.setItem('amfx1-staff', JSON.stringify(j.staff || {})); } catch (_) { $('msg').textContent = 'This browser blocks tab storage: allow it for this site.'; return; }
      $('pw').value = '';
      location.assign($('go').value === './crm' ? './crm' : './');
    } catch (_) { $('msg').textContent = 'The server did not answer.'; }
  });
})();
