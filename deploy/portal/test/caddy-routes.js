'use strict';
// The public host's routing, read from deploy/portal/Caddyfile itself (the {$PUBLIC_DOMAIN} block): which server a path goes to. The tests
// use it to say "this link works on the public site" and "this staff route does not exist there", and the licence-site end-to-end test runs
// a small proxy with it in place of Caddy. Matchers there are `path` lists of exact paths and `/prefix/*`.
const fs = require('node:fs');
const path = require('node:path');

function publicMatchers(file = path.join(__dirname, '..', 'Caddyfile')) {
  const text = fs.readFileSync(file, 'utf8'), start = text.indexOf('{$PUBLIC_DOMAIN} {');
  if (start < 0) throw new Error('no {$PUBLIC_DOMAIN} block in ' + file);
  let depth = 0, end = start; for (let i = text.indexOf('{', start + 16); i < text.length; i++) { if (text[i] === '{') depth++; else if (text[i] === '}' && --depth === 0) { end = i; break; } }
  const block = text.slice(start, end), get = name => { const m = new RegExp('@' + name + '\\s+path\\s+([^\\n]+)').exec(block); if (!m) throw new Error('no @' + name + ' matcher'); return m[1].trim().split(/\s+/); };
  return { staff: get('staffonly'), licence: get('licence') };
}
const matches = (p, list) => list.some(m => (m.endsWith('/*') ? p.startsWith(m.slice(0, -1)) : p === m));
let cached = null;
// 'staff' (answered 404 by Caddy), 'licence' (the licence server) or 'ota' (the OTA server: everything else)
function publicRoute(p) {
  const m = cached || (cached = publicMatchers()), clean = path.posix.normalize(decodeURIComponent(String(p).split('?')[0]));
  if (matches(clean, m.staff)) return 'staff';
  return matches(clean, m.licence) ? 'licence' : 'ota';
}
module.exports = { publicMatchers, publicRoute };
