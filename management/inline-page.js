'use strict';
// INLINE PAGES (management/vendor/index.html, management/customer/index.html): one HTML file each, its script and its Tailwind component
// styles written inside it, the Tailwind Play CDN build (one pinned version, a copy in bridge/license-server/dashboard/vendor/ served by
// the same server as tailwind.js: no script is ever fetched from a third party at run time) for the utility classes. The Content-Security-
// Policy lets no other script run: each inline <script> is allowed by its SHA-256 hash (worked out here from the file itself), tailwind.js
// by 'self'. Tailwind's Play CDN writes its styles into the page at run time, hence 'unsafe-inline' for styles only (never for scripts).
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const TAILWIND = "'self'";
const TAILWIND_FILE = path.join(__dirname, '..', 'bridge', 'license-server', 'dashboard', 'vendor', 'tailwindcss-play-3.4.16.js');

function scriptHashes(html) {
  const out = []; const re = /<script(?![^>]*\bsrc=)[^>]*>([\s\S]*?)<\/script>/gi; let m;
  while ((m = re.exec(html))) out.push("'sha256-" + crypto.createHash('sha256').update(m[1], 'utf8').digest('base64') + "'");
  return out;
}
function cspFor(html, { connect = "'self'" } = {}) {
  return `default-src 'none'; script-src ${[...scriptHashes(html), TAILWIND].join(' ')}; style-src 'unsafe-inline'; connect-src ${connect}; img-src 'self' data:; base-uri 'none'; form-action 'none'; frame-ancestors 'none'`;
}
// read once (the files are part of the release); { body, csp }
function inlinePage(file) { const html = fs.readFileSync(file, 'utf8'); return { body: Buffer.from(html, 'utf8'), csp: cspFor(html) }; }

module.exports = { inlinePage, cspFor, scriptHashes, TAILWIND, TAILWIND_FILE };
