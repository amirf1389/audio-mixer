'use strict';
// INLINE PAGES (management/vendor/index.html, management/customer/index.html): one HTML file each, its script and its Tailwind component
// styles written inside it, the Tailwind Play CDN (pinned to one version) for the utility classes. The Content-Security-Policy still lets
// no other script run: each inline <script> is allowed by its SHA-256 hash (worked out here from the file itself), the CDN by its exact
// address. Tailwind's Play CDN writes its styles into the page at run time, hence 'unsafe-inline' for styles only (never for scripts).
const crypto = require('node:crypto');
const fs = require('node:fs');

const TAILWIND = 'https://cdn.tailwindcss.com/3.4.16';

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

module.exports = { inlinePage, cspFor, scriptHashes, TAILWIND };
