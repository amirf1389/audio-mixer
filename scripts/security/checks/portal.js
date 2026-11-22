'use strict';
// 5. PORTAL / SERVER DEPLOYMENT: npm audit (when the registry is reachable), security headers, JWT secret handling, password hashing,
// parameterised SQL, Caddy / nginx TLS and headers, Docker images and compose files running without root.
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { matchAll, stripLineComments } = require('../lib/core');

const CHECK = 'portal';
const TEST = /(^|\/)(test|tests|e2e)(\/|$)|\.(test|spec)\.[a-z]+$|(^|\/)test\.js$|_test\.|(^|\/)test_/;

function npmAudit(ctx) {
  const locks = ctx.files.filter(f => /(^|\/)package-lock\.json$/.test(f));
  if (ctx.opts.offline) { ctx.note(CHECK, 'npm audit skipped (--offline)'); return; }
  for (const lock of locks) {
    let deps = 0; try { const j = JSON.parse(ctx.read(lock) || '{}'); deps = Object.keys(j.packages || j.dependencies || {}).filter(k => k).length; } catch (_) { /* unreadable lock */ }
    if (!deps) { ctx.add({ severity: 'INFO', check: CHECK, id: 'npm-no-deps', title: 'no third-party npm packages (nothing to audit)', file: lock }); continue; }
    const r = spawnSync('npm', ['audit', '--json', '--package-lock-only', '--audit-level=low'], { cwd: path.join(ctx.root, path.posix.dirname(lock)), encoding: 'utf8', timeout: 60000, maxBuffer: 32 * 1024 * 1024 });
    let j = null; try { j = JSON.parse(r.stdout || ''); } catch (_) { j = null; }
    if (!j || j.error) { ctx.note(CHECK, `npm audit for ${lock}: registry not reachable or audit failed (${(j && j.error && j.error.code) || r.error && r.error.code || 'no JSON'}) - run again with network`); continue; }
    const v = (j.metadata && j.metadata.vulnerabilities) || {};
    for (const [name, info] of Object.entries(j.vulnerabilities || {})) {
      const sev = info.severity === 'critical' || info.severity === 'high' ? 'HIGH' : info.severity === 'moderate' ? 'MEDIUM' : 'LOW';
      ctx.add({ severity: sev, check: CHECK, id: 'npm-audit', title: `npm audit: ${name} (${info.severity})`, file: lock, detail: (info.via || []).map(x => typeof x === 'string' ? x : x.title).filter(Boolean).slice(0, 2).join('; ') });
    }
    ctx.note(CHECK, `npm audit ${lock}: ${JSON.stringify(v)}`);
  }
}

function serverCode(ctx) {
  const serverFiles = ctx.textFiles(/\.(js|py)$/).filter(f => !TEST.test(f) && /(server|auth|framework|security|service|main|ratelimit|keygen|sitehost|config)\.(js|py)$/.test(f) && !f.startsWith('scripts/security/'));
  for (const rel of serverFiles) {
    const raw = ctx.read(rel), text = /\.py$/.test(rel) ? raw : stripLineComments(raw);
    // JWT secret with a literal default
    for (const h of matchAll(text, /(JWT_SECRET|jwtSecret|JWT_KEY|SESSION_SECRET|COOKIE_SECRET|ADMIN_TOKEN|SECRET_KEY)[\w.'"\]]*\s*(\|\||\?\?|or|,)\s*['"]([^'"]{1,})['"]/)) {
      ctx.add({ severity: 'HIGH', check: CHECK, id: 'default-secret', title: 'secret falls back to a hard-coded default: ' + h.m[1], file: rel, line: h.line, detail: 'refuse to start without the secret instead' });
    }
    // JWT verification that accepts "none" or does not pin the algorithm
    if (/jsonwebtoken|jwt\.verify\(/.test(text) && !/algorithms\s*:\s*\[/.test(text)) ctx.add({ severity: 'HIGH', check: CHECK, id: 'jwt-alg-not-pinned', title: 'jwt.verify without a pinned algorithms list', file: rel });
    if (/alg['"]?\s*[:=]+\s*['"]none['"]/.test(text) && !/!==?\s*['"]HS256['"]|refuse|reject/.test(text)) ctx.add({ severity: 'HIGH', check: CHECK, id: 'jwt-none', title: 'JWT "none" algorithm referenced', file: rel });
    if (/HS256/.test(text) && /createHmac/.test(text) && /length\s*<\s*32/.test(text)) ctx.add({ severity: 'INFO', check: CHECK, id: 'jwt-ok', title: 'JWT: HS256 with a secret of at least 32 characters enforced', file: rel });
    // password hashing
    for (const h of matchAll(text, /createHash\(\s*['"](md5|sha1|sha256|sha512)['"]\s*\)\s*\.update\(\s*[\w.]*pass/i)) ctx.add({ severity: 'HIGH', check: CHECK, id: 'fast-password-hash', title: 'password hashed with a fast hash (' + h.m[1] + '): use scrypt / argon2 / bcrypt', file: rel, line: h.line });
    for (const h of matchAll(text, /hashlib\.(md5|sha1|sha256)\(\s*[\w.]*pass/i)) ctx.add({ severity: 'HIGH', check: CHECK, id: 'fast-password-hash', title: 'password hashed with a fast hash', file: rel, line: h.line });
    if (/scrypt|argon2|bcrypt|pbkdf2/i.test(text) && /pass(word)?/i.test(text)) ctx.add({ severity: 'INFO', check: CHECK, id: 'password-kdf', title: 'passwords use a slow KDF (' + (/scrypt/i.test(text) ? 'scrypt' : /argon2/i.test(text) ? 'argon2' : /bcrypt/i.test(text) ? 'bcrypt' : 'pbkdf2') + ')', file: rel });
    // SQL built from strings
    for (const h of matchAll(text, /\.(prepare|exec|query|execute|executemany|run|all|get)\(\s*(`[^`]*\$\{[^`]*`|['"][^'"]*(SELECT|INSERT|UPDATE|DELETE|WHERE)[^'"]*['"]\s*\+(?!\s*\(?\s*['"])|f["'][^"']*(SELECT|INSERT|UPDATE|DELETE|WHERE)[^"']*\{|["'][^"']*(SELECT|INSERT|UPDATE|DELETE|WHERE)[^"']*%s?["']\s*%)/i)) {
      const lineText = text.split('\n')[h.line - 1];
      // a template literal that only splices fixed identifiers chosen from a constant list (column lists, ? placeholders) is reviewed as LOW
      // only fixed fragments spliced in (column lists, "?" / %s placeholders, WHERE clauses of bound parameters) is reviewed as LOW
      const spliced = [...lineText.matchAll(/\$\{([^}]*)\}|(?<![$\\])\{([^}]*)\}/g)].map(x => (x[1] || x[2] || '').trim()).filter(Boolean);
      const onlyPlaceholders = spliced.length > 0 && spliced.every(e => /^((cols|columns|fields|placeholders|qs|marks|where|cond|order|sets|COLS|FIELDS)\b|['"], ?['"]\.join\((sets|cols|fields|where)\)|(sets|cols|fields|where|placeholders)\.join\()/.test(e));
      ctx.add({ severity: onlyPlaceholders ? 'LOW' : 'HIGH', check: CHECK, id: 'sql-string-built', title: onlyPlaceholders ? 'SQL text assembled from fixed parts (verify none comes from the request)' : 'SQL built by string interpolation / concatenation (use bound parameters)', file: rel, line: h.line, detail: lineText.trim().slice(0, 120) });
    }
    // helmet-equivalent headers on HTTP servers
    if (/createServer\(|createApp\(|FastAPI\(/.test(text)) {
      const need = [['X-Content-Type-Options', /X-Content-Type-Options|nosniff/i], ['Content-Security-Policy', /Content-Security-Policy/i], ['frame protection', /X-Frame-Options|frame-ancestors/i], ['Referrer-Policy', /Referrer-Policy/i]];
      const dir = path.posix.dirname(rel);
      const nearby = ctx.files.filter(f => (f.startsWith(dir + '/') || path.posix.dirname(f) === dir) && /\.(js|py)$/.test(f)).map(f => ctx.read(f) || '').join('\n') + text;
      const missing = need.filter(([, re]) => !re.test(nearby)).map(([n]) => n);
      if (missing.length && /(license-server|ota-server|lkms|portal|management)/.test(rel)) ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'missing-headers', title: 'security headers not set: ' + missing.join(', '), file: rel, detail: 'Caddy adds some, but the app should not rely on the proxy' });
    }
  }
}

function proxies(ctx) {
  for (const rel of ctx.files.filter(f => /(^|\/)Caddyfile$/.test(f))) {
    const t = ctx.read(rel) || '';
    if (/auto_https\s+off/.test(t)) ctx.add({ severity: 'HIGH', check: CHECK, id: 'caddy-no-https', title: 'Caddy automatic HTTPS switched off', file: rel });
    if (/\btls\s+internal\b/.test(t)) ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'caddy-tls-internal', title: 'Caddy uses its internal CA (self-signed) for a site', file: rel });
    for (const h of matchAll(t, /^\s*http:\/\/[^\s{]+\s*\{/m)) ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'caddy-plain-http', title: 'plain http:// site block', file: rel, line: h.line });
    if (!/Strict-Transport-Security/.test(t)) ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'caddy-no-hsts', title: 'Caddy config sends no HSTS header', file: rel });
    if (!/X-Content-Type-Options/.test(t)) ctx.add({ severity: 'LOW', check: CHECK, id: 'caddy-no-nosniff', title: 'Caddy config sends no X-Content-Type-Options', file: rel });
    if (!/max_size|max_header_size/.test(t)) ctx.add({ severity: 'LOW', check: CHECK, id: 'caddy-no-size-limit', title: 'no request body / header size limits in Caddy', file: rel });
    const allow = /ADMIN_ALLOW_IPS:([^}]+)\}/.exec(t);
    if (allow && /0\.0\.0\.0\/0/.test(allow[1])) ctx.add({ severity: 'LOW', check: CHECK, id: 'admin-open-by-default', title: 'staff hosts are reachable from every address unless ADMIN_ALLOW_IPS is set (password still required)', file: rel, line: t.slice(0, allow.index).split('\n').length });
    if (/Strict-Transport-Security/.test(t) && /reverse_proxy/.test(t)) ctx.add({ severity: 'INFO', check: CHECK, id: 'caddy-ok', title: 'Caddy: automatic HTTPS, HSTS and headers configured', file: rel });
  }
  for (const rel of ctx.files.filter(f => /\.(conf|inc)$/.test(f) && /nginx|deploy|hybrid/.test(f))) {
    const t = ctx.read(rel) || ''; if (!/server\s*\{|add_header|ssl_/.test(t)) continue;
    for (const h of matchAll(t, /ssl_protocols[^;]*(SSLv3|TLSv1(\.0|\.1)?)(?![.\d])/)) ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'nginx-old-tls', title: 'nginx allows an obsolete TLS / SSL version: ' + h.m[1], file: rel, line: h.line });
    if (/listen\s+443|ssl_certificate/.test(t) && !/Strict-Transport-Security/.test(t) && !/include[^;]*headers/.test(t)) ctx.add({ severity: 'LOW', check: CHECK, id: 'nginx-no-hsts', title: 'nginx TLS server without HSTS', file: rel });
    for (const h of matchAll(t, /autoindex\s+on/)) ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'nginx-autoindex', title: 'nginx directory listing on', file: rel, line: h.line });
  }
}

function docker(ctx) {
  for (const rel of ctx.files.filter(f => /(^|\/)Dockerfile[^/]*$/.test(f) && !/dockerignore$/.test(f))) {
    const t = ctx.read(rel) || '';
    const users = [...t.matchAll(/^\s*USER\s+(\S+)/gim)].map(m => m[1]);
    const last = users[users.length - 1];
    if (!last || /^(root|0)(:|$)/.test(last)) {
      const compose = ctx.files.filter(f => /docker-compose\.ya?ml$|compose\.ya?ml$/.test(f)).map(f => [f, ctx.read(f) || '']).find(([, c]) => c.includes(rel) || c.includes(path.posix.basename(path.posix.dirname(rel))) && /user:\s*["']?\d/.test(c));
      ctx.add({ severity: compose ? 'MEDIUM' : 'HIGH', check: CHECK, id: 'docker-root', title: 'container image runs as root (no USER)' + (compose ? '; compose sets user: (still add USER to the image)' : ''), file: rel, detail: 'add a non-root USER after installing packages' });
    } else ctx.add({ severity: 'INFO', check: CHECK, id: 'docker-user', title: 'image runs as non-root user ' + last, file: rel });
    for (const h of matchAll(t, /^\s*FROM\s+(\S+)/im)) if (!/:[\w.-]+|@sha256:/.test(h.m[1]) || /:latest$/.test(h.m[1])) ctx.add({ severity: 'LOW', check: CHECK, id: 'docker-unpinned', title: 'base image not pinned to a version: ' + h.m[1], file: rel, line: h.line });
    for (const h of matchAll(t, /^\s*(ENV|ARG)\s+\w*(SECRET|TOKEN|PASSWORD|KEY)\w*\s*=?\s*\S+/im)) ctx.add({ severity: 'HIGH', check: CHECK, id: 'docker-secret', title: 'secret baked into the image (' + h.m[1] + ')', file: rel, line: h.line });
    for (const h of matchAll(t, /^\s*(COPY|ADD)\s+[^\n]*(\.env\b|secrets\/|private[^\s]*\.pem)/im)) ctx.add({ severity: 'HIGH', check: CHECK, id: 'docker-copies-secret', title: 'secret file copied into the image', file: rel, line: h.line });
  }
  for (const rel of ctx.files.filter(f => /(docker-)?compose\.ya?ml$/.test(f))) {
    const t = ctx.read(rel) || '';
    for (const h of matchAll(t, /privileged:\s*true/)) ctx.add({ severity: 'HIGH', check: CHECK, id: 'compose-privileged', title: 'privileged container', file: rel, line: h.line });
    for (const h of matchAll(t, /network_mode:\s*["']?host/)) ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'compose-host-net', title: 'container on the host network', file: rel, line: h.line });
    for (const h of matchAll(t, /\/var\/run\/docker\.sock/)) ctx.add({ severity: 'HIGH', check: CHECK, id: 'compose-docker-sock', title: 'Docker socket mounted into a container (root on the host)', file: rel, line: h.line });
    // published ports of services that are not the TLS proxy
    for (const h of matchAll(t, /ports:\s*\[?\s*["']?(\d+):(\d+)/)) {
      const before = t.slice(0, h.index); const svc = (before.match(/^ {2}(\w[\w-]*):\s*$/gm) || []).pop() || '';
      if (/caddy|nginx|proxy|traefik/i.test(svc) || ['80', '443'].includes(h.m[2])) continue;
      ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'compose-port-public', title: `service ${svc.replace(':', '').trim()} publishes port ${h.m[1]} on every interface (bind 127.0.0.1:${h.m[1]}:${h.m[2]} behind the proxy)`, file: rel, line: h.line });
    }
    for (const h of matchAll(t, /^\s+\w*(SECRET|TOKEN|PASSWORD|PASS)\w*:\s*["']?(?!\$\{)([^\s"']{8,})/gm)) ctx.add({ severity: 'HIGH', check: CHECK, id: 'compose-literal-secret', title: 'literal secret in compose file', file: rel, line: h.line });
  }
}

function run(ctx) {
  npmAudit(ctx);
  serverCode(ctx);
  proxies(ctx);
  docker(ctx);
}

module.exports = { run, CHECK };
