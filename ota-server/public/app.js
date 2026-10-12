'use strict';
// Public download page: the primary DOWNLOAD button already works without this script (plain href="/latest", server-side OS detection). This only
// fills in what the server detected, the other systems, the driver and the release notes, from the same JSON the app itself uses (GET /api/latest).
// Server data (labels, release notes, file names) is only ever written as text nodes, no HTML is ever parsed: the same rule as the admin dashboard.
(async function () {
  const $ = id => document.getElementById(id);
  const h = (tag, attrs, ...kids) => {
    const e = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs || {})) { if (k === 'class') e.className = v; else if (v !== false && v != null) e.setAttribute(k, v); }
    for (const c of kids.flat()) if (c != null && c !== false) e.append(c.nodeType ? c : document.createTextNode(String(c)));
    return e;
  };
  const human = n => { if (!(n > 0)) return ''; const u = ['B', 'KB', 'MB', 'GB']; let i = 0, v = n; while (v >= 1024 && i < u.length - 1) { v /= 1024; i++; } return (i ? v.toFixed(1) : v) + ' ' + u[i]; };

  let r;
  try { r = await fetch('/api/latest', { cache: 'no-store' }); } catch (e) { $('status').textContent = 'Could not reach the download server. Try again in a moment.'; return; }
  const j = await r.json().catch(() => null);
  if (!r.ok || !j || !j.ok) {
    $('status').textContent = (j && j.error) || 'No release is published on this server yet.';
    $('dl-primary').hidden = true;
    $('none-card').hidden = false;
    return;
  }

  $('status').textContent = 'The latest release:';
  $('version-line').hidden = false;
  $('version-line').textContent = 'Audio Mixer ' + j.version + (j.released ? ' · ' + j.released : '');

  const primary = $('dl-primary');
  if (j.file) {
    primary.textContent = 'Download — ' + j.file.label + (j.file.size ? ' (' + human(j.file.size) + ')' : '');
    primary.title = j.file.how || '';
    $('detected-note').textContent = j.detected ? 'Detected from your browser (' + j.detected + '): ' + (j.os || 'unknown system') + (j.arch ? ', ' + j.arch : '') + '. Not you? Pick a system below.' : '';
  } else {
    primary.hidden = true;
    $('detected-note').textContent = 'Your system (' + (j.os || 'unknown') + ') could not be matched to a published file automatically: pick one below.';
  }

  const others = j.others || [];   // already excludes the picked file and the driver (bridge/osdetect.js)
  if (others.length) {
    $('others-card').hidden = false;
    $('others-grid').replaceChildren(...others.map(o => h('a', { class: 'btn', href: o.url }, o.label + (o.size ? ' (' + human(o.size) + ')' : ''))));
  }

  if (j.driver) {
    $('driver-card').hidden = false;
    $('driver-note').textContent = j.driver.label + (j.driver.how ? ': ' + j.driver.how : '');
  }

  if (j.notes && j.notes.length) {
    $('notes-card').hidden = false;
    $('notes-list').replaceChildren(...j.notes.map(n => h('li', null, n)));
  }
})();
