'use strict';
// 7. WEB PAGES: CSP (meta or sent by the serving code / proxy), SRI on CDN scripts and styles, inline secrets, and staff pages in public roots.
const path = require('node:path');
const { matchAll } = require('../lib/core');

const CHECK = 'web';
const SENSITIVE_PAGE = /(login|admin|dashboard|crm|system|staff|vendor|account|portal|manage|license-server|release-manager)/i;

function run(ctx, cfg) {
  const vendored = (cfg.vendored || []).map(v => v.path);
  const pages = ctx.files.filter(f => /\.html?$/i.test(f) && !vendored.some(v => f.startsWith(v + '/')) && !/(^|\/)(node_modules|docs\/manual)\//.test(f));
  // files that set a CSP header for pages they serve (Node servers, nginx / Caddy configs, C++ daemon)
  const cspProviders = ctx.files.filter(f => !/\.html?$/.test(f) && /\.(js|cpp|py|conf|inc)$|Caddyfile$/.test(f) && /Content-Security-Policy/.test(ctx.read(f) || ''));
  // servers that send the header under its lower-case name (Node): they count only for the pages in their own folder tree that they name
  const lowerProviders = ctx.files.filter(f => /\.(js|cpp|py)$/.test(f) && !/(^|\/)tests?\/|\.test\.js$/.test(f) && !cspProviders.includes(f) && /['"]content-security-policy['"]/.test(ctx.read(f) || '')).map(f => [f, ctx.read(f) || '']);
  const providerText = cspProviders.map(f => [f, ctx.read(f) || '']);
  const publicRoots = cfg.publicWebRoots || [];

  for (const rel of pages) {
    const t = ctx.read(rel); if (t === null) continue;
    const base = path.posix.basename(rel), dir = path.posix.dirname(rel);
    const sensitive = SENSITIVE_PAGE.test(rel);
    // CSP
    const meta = /<meta[^>]+http-equiv=["']Content-Security-Policy["'][^>]*content=["']([^"']+)["']/i.exec(t);
    const served = providerText.find(([f, x]) => x.includes(base) || x.includes(dir + '/') || (path.posix.dirname(f) === dir) || x.includes(path.posix.basename(dir) + '/') || (publicRoots.some(r => rel.startsWith(r + '/')) && /site|website/.test(f)))
      || lowerProviders.find(([f, x]) => { const d = path.posix.dirname(f); return (rel.startsWith(d + '/')) && x.includes("'" + base + "'"); });
    const csp = meta ? meta[1] : null;
    if (!meta && !served) ctx.add({ severity: sensitive ? 'MEDIUM' : 'LOW', check: CHECK, id: 'no-csp', title: 'page has no Content-Security-Policy (no meta tag, and no server config found that sends one for it)', file: rel });
    else ctx.add({ severity: 'INFO', check: CHECK, id: 'csp', title: 'CSP ' + (meta ? 'meta tag' : 'sent by ' + served[0]), file: rel });
    if (csp && /'unsafe-eval'/.test(csp)) ctx.add({ severity: 'LOW', check: CHECK, id: 'csp-unsafe-eval', title: "CSP allows 'unsafe-eval'", file: rel });
    if (csp && /script-src[^;]*\*(?![.\w])/.test(csp)) ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'csp-wildcard', title: 'CSP script-src allows any host', file: rel });
    // third-party scripts / styles
    for (const h of matchAll(t, /<script\b[^>]*\bsrc=["']((?:https?:)?\/\/[^"']+)["'][^>]*>/i)) {
      const tag = h.m[0], url = h.m[1];
      if (/^http:/.test(url)) { ctx.add({ severity: 'HIGH', check: CHECK, id: 'script-http', title: 'script loaded over plain http: ' + url, file: rel, line: h.line }); continue; }
      if (!/\bintegrity=/.test(tag)) ctx.add({ severity: sensitive ? 'HIGH' : 'MEDIUM', check: CHECK, id: 'script-no-sri', title: 'CDN script without Subresource Integrity' + (sensitive ? ' on a staff / sign-in page' : '') + ': ' + url, file: rel, line: h.line, detail: 'a compromised CDN runs code with the page\'s privileges; add integrity="sha384-..." crossorigin="anonymous" or serve the file yourself' });
      else if (!/crossorigin=/.test(tag)) ctx.add({ severity: 'LOW', check: CHECK, id: 'sri-no-crossorigin', title: 'SRI script without crossorigin attribute', file: rel, line: h.line });
    }
    for (const h of matchAll(t, /<link\b[^>]*rel=["']stylesheet["'][^>]*>/i)) {
      const m = /href=["']((?:https?:)?\/\/[^"']+)["']/.exec(h.m[0]); if (!m) continue;
      if (/fonts\.googleapis\.com/.test(m[1])) continue;   // dynamic per user agent: SRI cannot be used
      if (!/\bintegrity=/.test(h.m[0])) ctx.add({ severity: 'LOW', check: CHECK, id: 'style-no-sri', title: 'CDN stylesheet without SRI: ' + m[1], file: rel, line: h.line });
    }
    // inline secrets in page scripts
    for (const h of matchAll(t, /\b(api[_-]?key|apiKey|secret|token|password)\s*[:=]\s*["']([A-Za-z0-9_\-+/=]{16,})["']/i)) ctx.add({ severity: 'HIGH', check: CHECK, id: 'inline-secret', title: 'secret-looking literal in a page: ' + h.m[1], file: rel, line: h.line });
    // staff / admin pages in a public web root
    if (publicRoots.some(r => rel.startsWith(r + '/')) && (/(admin|login|dashboard|staff|crm)/i.test(base) || /<form[^>]+action=["'][^"']*\/admin/i.test(t) || /type=["']password["']/i.test(t))) {
      ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'admin-page-public', title: 'staff / sign-in page inside the public web root', file: rel });
    }
    for (const h of matchAll(t, /target=["']_blank["'](?![^>]*rel=["'][^"']*noopener)/i)) { ctx.add({ severity: 'INFO', check: CHECK, id: 'blank-no-noopener', title: 'target=_blank without rel=noopener (modern browsers imply it)', file: rel, line: h.line }); break; }
  }
  // robots / sitemap must not list staff paths
  for (const rel of ctx.files.filter(f => /(robots\.txt|sitemap\.xml)$/.test(f))) {
    const t = ctx.read(rel) || '';
    for (const h of matchAll(t, /\/(admin|staff|dashboard)\b/)) if (!/Disallow/.test(t.split('\n')[h.line - 1])) ctx.add({ severity: 'LOW', check: CHECK, id: 'robots-admin', title: 'staff path listed in ' + path.posix.basename(rel), file: rel, line: h.line });
  }
  ctx.note(CHECK, `${pages.length} HTML pages checked; CSP sent by: ${cspProviders.join(', ') || 'none'}`);
}

module.exports = { run, CHECK };
