'use strict';
// 4. NETWORK SURFACES: every listening socket / HTTP route of the console, the daemon, the web remote, the bridge and the portal, with the
// evidence for authentication, the default bind address, CORS, rate limiting, request size limits and path traversal on file routes.
const path = require('node:path');
const { matchAll, stripC, stripLineComments } = require('../lib/core');

const CHECK = 'network';
const TEST = /(^|\/)(test|tests|__tests__|e2e)(\/|$)|\.(test|spec)\.[a-z]+$|(^|\/)test\.js$|_test\.|(^|\/)test_/;

const LOOPBACK = /^(127\.0\.0\.1|localhost|::1|\[::1\])$/;
const ANY = /^(0\.0\.0\.0|::|\[::\]|)$/;

function routeLiterals(text) {
  const out = new Set();
  const pats = [
    /\b(?:app|router|api)\.(?:get|post|put|patch|delete|head|all|route)\(\s*['"`](\/[^'"`]*)['"`]/g,
    /(?:pathname|\bp|\bpath|\burl|\broute)\s*(?:===|==|!==)\s*['"`](\/[^'"`]*)['"`]/g,
    /(?:pathname|\bp|\bpath)\.startsWith\(\s*['"`](\/[^'"`]*)['"`]/g,
    /\bcase\s+['"`](\/[^'"`]*)['"`]\s*:/g,
    /r\.path\s*==\s*"(\/[^"]*)"/g, /r\.path\.rfind\(\s*"(\/[^"]*)"/g, /websocket\(\s*"(\/[^"]*)"/g,
    /@(?:app|router)\.(?:get|post|put|patch|delete)\(\s*["'](\/[^"']*)["']/g,
  ];
  for (const re of pats) for (const m of text.matchAll(re)) out.add(m[1]);
  return [...out].sort();
}

// resolve the host argument of .listen() / an Options default to "loopback", "all interfaces" or "unknown"
function resolveHost(expr, text) {
  expr = String(expr || '').trim();
  const lit = /^['"`]([^'"`]*)['"`]$/.exec(expr);
  if (lit) return LOOPBACK.test(lit[1]) ? 'loopback' : ANY.test(lit[1]) ? 'all' : 'specific:' + lit[1];
  if (!expr) return 'all';
  // an identifier: look at what it is set to
  const idm = /([A-Za-z_$][\w$]*)\s*$/.exec(expr); if (!idm) return 'unknown';
  const id = idm[1];
  const def = new RegExp('(?:const|let|var)\\s+(?:\\{[^}]*\\b' + id + '\\b[^}]*\\}|' + id + ')\\s*=\\s*([^;\\n]+)|\\b' + id + '\\s*=\\s*([^;\\n]+)').exec(text);
  const rhs = def ? (def[1] || def[2] || '') : expr;
  if (/0\.0\.0\.0|'::'|"::"/.test(rhs)) return /127\.0\.0\.1|localhost/.test(rhs) ? 'loopback-default (0.0.0.0 by option)' : 'all';
  if (/127\.0\.0\.1|localhost|::1/.test(rhs)) return /process\.env|arg\(|opts?\.|options\./.test(rhs) ? 'loopback (env / option override)' : 'loopback';
  if (/127\.0\.0\.1/.test(expr)) return 'loopback';
  return 'unknown';
}

function evidence(text) {
  return {
    auth: /timingSafeEqual|[Aa]uthorization|Bearer|verifyJwt|jwt\b|\bJWT|tokenOk|bearerOk|requireStaff|staffAuth|session|\bpin\b|PIN_MS|authorised\(|roleOf\(|Depends\(\s*admin|app_auth|x_app_secret/.test(text),
    rate: /rateLimiter|createLimiter|limiter\b|rateLimit|RateLimit|\bRATE\b|429|ratelimit|pinLockedUntil|fails\.|LOCK_N/.test(text),
    bodyLimit: /MAX_BODY|maxBody|max_body|413|size\s*>\s*\w|content-length['"\]]*\s*\|\|\s*0\)\s*>|kMaxBody|64\s*\*\s*1024|65536|MAX_FILE|maxBytes/i.test(text),
    readsBody: /req\.on\(\s*['"]data['"]|\.body\(\)|await\s+request\.(json|body)|Content-Length|readBody|request\.stream/.test(text),
    headers: /X-Content-Type-Options|Content-Security-Policy|SECURITY_HEADERS|nosniff/.test(text),
    hostCheck: /req\.headers\.host|headers\.host|isLocalOrigin|allowedHost|hosts\.has\(|EXTRA_HOSTS|Host header|localOnly/.test(text),
    corsStar: /['"]Access-Control-Allow-Origin['"]\s*:\s*['"]\*['"]|Access-Control-Allow-Origin: \*/.test(text),
    corsReflect: /['"]Access-Control-Allow-Origin['"]\s*:\s*(origin|req\.headers\.origin)\b/.test(text),
    corsAllowList: /isLocalOrigin|ORIGINS|allowedOrigins|originAllowed|EXTRA_ORIGINS/.test(text),
  };
}

// file routes: a read / stream of a path built from something the request controls
function traversal(ctx, rel, text) {
  const lines = text.split('\n');
  const guards = /startsWith\(\s*(ROOT|root|base|dir|PUBLIC|dirs?\.\w+)|path\.relative\(|includes\(\s*['"]\.\.['"]|basename\(|\^\[[^\]]*\]\+\$\/\.test|\/\^\[\\w|hasOwnProperty\.call\(|index\[\s*name\s*\]|\bDASH\[|safeName|staticAllowed|\.find\([^\n]*===[^\n]*params|\.find\([^\n]*params[^\n]*===|\bnew Set\([^)]*\)\.has\(|ALLOWED\w*\.has\(/;
  for (const h of matchAll(text, /\b(?:fs\.)?(createReadStream|readFile|readFileSync|sendFile|send_file|FileResponse|open)\s*\(\s*(path\.(?:join|resolve)\([^)]*\)|[A-Za-z_][\w.]*)/)) {
    const arg = h.m[2];
    if (/__dirname\s*,\s*['"][^'"]+['"]\s*(,\s*['"][^'"]+['"]\s*)*\)$/.test(arg)) continue;   // constant path
    const vars = (arg.match(/[A-Za-z_]\w*/g) || []).filter(v => !/^(path|join|resolve|__dirname|ROOT|PUBLIC|dir|dirs|releases|history|manifests|dataDir|process|cwd)$/.test(v));
    if (!vars.length) continue;
    const window = lines.slice(Math.max(0, h.line - 15), h.line).join('\n');
    const REQ = '(pathname|req\\.url|req\\.params|c\\.params|ctx\\.params|request\\.(path|url|query|match_info)|decodeURIComponent|searchParams|req\\.query' + (/\.exec\(\s*(p|pathname|url\.pathname|req\.url|path|rel)\s*\)/.test(window) ? '|\\bm\\[\\d\\]' : '') + ')';
    // one-file taint: names assigned from the request, then names assigned from those (two rounds)
    const tainted = new Set(), reqRe = new RegExp(REQ);
    for (let round = 0; round < 3; round++) for (const l of window.split('\n')) for (const st of l.split(/;|\bfor\s*\(/)) {
      const a = /(?:\b(?:const|let|var)\s+)?\b([A-Za-z_$][\w$]*)\s*=(?!=)\s*(.+)$/.exec(st); if (!a) continue;
      if (reqRe.test(a[2]) || [...tainted].some(t => new RegExp('\\b' + t.replace(/\$/g, '\\$') + '\\b').test(a[2]))) tainted.add(a[1]);
    }
    const fromReq = reqRe.test(arg) || vars.some(v => tainted.has(v));
    if (!fromReq) continue;
    // m[n] taken from a regex whose groups cannot contain "/" (no ".", "[^", "\\S" wildcards) is a whitelist
    const rx = [...window.matchAll(/\bm\s*=\s*\/((?:\\.|[^/\n])+)\/[a-z]*\.exec\(/g)].pop();
    const rxSafe = rx && /\bm\[\d\]/.test(window) && !/(^|[^\\])\.|\[\^|\\S|\\W|\\D/.test(rx[1]);
    const guarded = rxSafe || guards.test(window) || vars.some(v => new RegExp('\\b(index|FILES|DASH|TYPES|ALLOWED)\\[\\s*' + v + '\\s*\\]').test(window));
    ctx.add({ severity: guarded ? 'INFO' : 'HIGH', check: CHECK, id: guarded ? 'file-route-guarded' : 'path-traversal', title: guarded ? 'file route with a containment / allow-list guard' : 'file read from a request-controlled path with no containment check', file: rel, line: h.line, detail: arg.slice(0, 100) });
  }
}

function jsServers(ctx) {
  const rows = [];
  for (const rel of ctx.textFiles(/\.(js|mjs|cjs)$/)) {
    if (TEST.test(rel) || rel.startsWith('scripts/security/') || /(^|\/)(dashboard|public|portal|ui)\/[^/]+\.js$/.test(rel) && !/server/.test(rel)) continue;
    const raw = ctx.read(rel); const text = stripLineComments(raw);
    if (!/\b(https?|net|tls|http2)\.createServer\(|createServer\(|new\s+WebSocketServer\(|\.listen\(\s*\d|\.listen\(\s*(port|PORT|opts|o\.port)/.test(text)) continue;
    const listens = matchAll(text, /\.listen\(\s*([^,)]+)\s*(?:,\s*([^,)]+))?/).filter(h => !/listen\(\s*\)/.test(h.m[0]));
    if (!listens.length && !/createServer\(/.test(text)) continue;
    const ev = evidence(text);
    const routes = routeLiterals(text);
    let bind = 'not listening here (handler / library)';
    if (listens.length) {
      const hostArg = h => (h.m[2] && !/^\s*(async\b|\()|=>|function/.test(h.m[2]) ? h.m[2] : '');
      bind = [...new Set(listens.map(h => resolveHost(hostArg(h), text)))].join(', ');
      for (const h of listens) {
        const host = resolveHost(hostArg(h), text);
        if (host === 'all') ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'bind-all', title: 'server listens on all interfaces by default', file: rel, line: h.line, detail: 'bind 127.0.0.1 by default and make LAN / public exposure an explicit option' });
        else if (host === 'unknown') ctx.add({ severity: 'LOW', check: CHECK, id: 'bind-unknown', title: 'bind address could not be determined statically', file: rel, line: h.line, detail: h.m[0].slice(0, 80) });
      }
    }
    const exposed = /all|unknown|option/.test(bind) || /HOST\s*\|\||_HOST\b/.test(text);
    const mutating = /method\s*===?\s*['"](POST|PUT|PATCH|DELETE)['"]|\.(post|put|patch|delete)\(\s*['"`]\//.test(text);
    if (mutating && !ev.auth) ctx.add({ severity: exposed ? 'HIGH' : ev.hostCheck ? 'LOW' : 'MEDIUM', check: CHECK, id: 'no-auth', title: 'server accepts state-changing requests with no authentication evidence', file: rel, line: listens[0] ? listens[0].line : 1, detail: exposed ? 'reachable beyond loopback' : ev.hostCheck ? 'loopback only and the Host header is checked; keep a custom-header / token requirement on POSTs' : 'loopback only, but any web page can POST to localhost: add a token or an Origin / Host check' });
    if (mutating && !ev.auth && !exposed && !ev.hostCheck) ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'no-host-check', title: 'loopback server without a Host / Origin check (DNS rebinding, cross-site requests)', file: rel });
    if (!ev.rate) ctx.add({ severity: exposed ? 'MEDIUM' : 'LOW', check: CHECK, id: 'no-rate-limit', title: 'no rate limiting found', file: rel });
    if (ev.readsBody && !ev.bodyLimit) ctx.add({ severity: exposed ? 'MEDIUM' : 'LOW', check: CHECK, id: 'no-body-limit', title: 'request bodies are read with no size limit', file: rel });
    if (!ev.headers && routes.length) ctx.add({ severity: 'LOW', check: CHECK, id: 'no-security-headers', title: 'no security headers (nosniff / CSP) set by this server', file: rel });
    if (ev.corsStar) { const h = matchAll(raw, /Access-Control-Allow-Origin['"]?\s*:\s*['"]\*['"]/)[0]; ctx.add({ severity: 'LOW', check: CHECK, id: 'cors-star', title: 'CORS allows every origin (fine only for public, read-only, unauthenticated data)', file: rel, line: h ? h.line : 0 }); }
    if (ev.corsReflect && !ev.corsAllowList) ctx.add({ severity: 'HIGH', check: CHECK, id: 'cors-reflect', title: 'CORS reflects the request Origin with no allow-list', file: rel });
    // framework-style routes: /admin routes must carry the auth middleware
    for (const h of matchAll(text, /\bapp\.(get|post|put|patch|delete)\(\s*['"`](\/(?:admin|issue)[^'"`]*)['"`]\s*,([^\n]*)/)) {
      if (/\/(login|login\.js|auth\/login)$/.test(h.m[2])) continue;
      const mw = h.m[3].split(/\basync\b|=>/)[0];
      if (/^\s*page\(/.test(h.m[3]) || /^\s*c\s*=>\s*c\.send\(\s*30[12]/.test(h.m[3])) { ctx.add({ severity: 'INFO', check: CHECK, id: 'admin-page-shell', title: 'admin page shell / redirect served without auth (no data; its API calls need the token): ' + h.m[2], file: rel, line: h.line }); continue; }
      if (!/\b(admin|auth|staff|requireAuth|bearer)\w*\b/i.test(mw)) ctx.add({ severity: 'HIGH', check: CHECK, id: 'admin-route-no-auth', title: 'admin route without the auth middleware: ' + h.m[1].toUpperCase() + ' ' + h.m[2], file: rel, line: h.line });
    }
    traversal(ctx, rel, text);
    rows.push({ component: componentOf(rel), file: rel, kind: 'HTTP (Node.js)', bind, routes, auth: ev.auth, rate: ev.rate, bodyLimit: ev.readsBody ? ev.bodyLimit : 'n/a', cors: ev.corsStar ? '*' : ev.corsReflect ? (ev.corsAllowList ? 'reflect (allow-list)' : 'reflect (ANY)') : 'none' });
  }
  return rows;
}

function pyServers(ctx) {
  const rows = [];
  for (const rel of ctx.textFiles(/\.py$/)) {
    if (TEST.test(rel)) continue;
    const text = ctx.read(rel);
    if (!/FastAPI\(|Flask\(|HTTPServer\(|socketserver|aiohttp\.web|uvicorn\.run/.test(text)) continue;
    const ev = evidence(text);
    const routes = routeLiterals(text);
    // per-route auth for FastAPI / Flask decorators
    for (const h of matchAll(text, /@(?:app|router)\.(get|post|put|patch|delete)\(\s*["'](\/[^"']*)["']([^)]*)\)\s*\n\s*(?:async\s+)?def\s+\w+\(([^)]*)\)/)) {
      const route = h.m[2], deco = h.m[3], sig = h.m[4];
      const authed = /Depends\(|dependencies=|Header\(|Security\(/.test(deco + sig) || /api_key|token|secret|auth/i.test(sig);
      if (/^\/(admin|manage\/api)/.test(route) && !authed) ctx.add({ severity: 'HIGH', check: CHECK, id: 'admin-route-no-auth', title: 'admin route without an auth dependency: ' + h.m[1].toUpperCase() + ' ' + route, file: rel, line: h.line });
      if (h.m[1] !== 'get' && !authed && !/^\/v\d\/(activate|validate|deactivate|heartbeat|check)/.test(route)) ctx.add({ severity: 'LOW', check: CHECK, id: 'route-no-auth', title: 'state-changing route without an auth dependency (verify it is meant to be public): ' + h.m[1].toUpperCase() + ' ' + route, file: rel, line: h.line });
    }
    if (!ev.rate) ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'no-rate-limit', title: 'no rate limiting found', file: rel });
    traversal(ctx, rel, text);
    const dir = rel.split('/')[0];
    const bindSrc = ctx.files.filter(f => f.startsWith(dir + '/') && /(Dockerfile|\.service|\.md)$/.test(f)).map(f => [f, ctx.read(f) || '']);
    const binds = [...new Set(bindSrc.flatMap(([f, t]) => [...t.matchAll(/--host["',\s]+([\w.:]+)/g)].map(m => m[1] + ' (' + path.posix.basename(f) + ')')))];
    rows.push({ component: componentOf(rel), file: rel, kind: 'HTTP (Python)', bind: binds.join(', ') || 'unknown', routes, auth: ev.auth, rate: ev.rate, bodyLimit: 'framework', cors: /CORSMiddleware/.test(text) ? (/allow_origins=\[\s*["']\*/.test(text) ? '*' : 'allow-list') : 'none' });
  }
  return rows;
}

function cppServers(ctx, cfg) {
  const rows = [];
  const extra = new Set(((cfg.networkFiles || {}).cpp) || []);
  const vendored = (cfg.vendored || []).map(v => v.path);
  for (const rel of ctx.textFiles(/\.(cpp|cc|cxx|hpp|h|mm)$/)) {
    if (vendored.some(v => rel.startsWith(v + '/')) || TEST.test(rel)) continue;
    const raw = ctx.read(rel); const text = stripC(raw);
    const isQt = /\b(QTcpServer|QWebSocketServer|QSslServer|QHttpServer|QLocalServer|QUdpSocket)\b/.test(text) && /\.listen\(|->listen\(|bind\(/.test(text);
    const isBsd = /::bind\s*\(|\blisten\s*\(\s*\w+\s*,\s*\d+\s*\)/.test(text);
    if (!isQt && !isBsd && !extra.has(rel)) continue;
    const dirText = ctx.files.filter(f => path.posix.dirname(f) === path.posix.dirname(rel) && /\.(cpp|hpp|h)$/.test(f)).map(f => ctx.read(f) || '').join('\n');
    const ev = evidence(dirText);
    const routes = routeLiterals(raw);
    let bind = 'see caller';
    if (isQt) {
      const any = /QHostAddress::(Any|AnyIPv4|AnyIPv6)\b/.test(text), lo = /QHostAddress::(LocalHost|LocalHostIPv6)\b/.test(text);
      bind = any && lo ? 'loopback (LAN by option)' : any ? 'all' : lo ? 'loopback' : /QLocalServer/.test(text) ? 'local socket (same user)' : 'unknown';
      if (any && lo) ctx.add({ severity: /ipAllowed|allowedIps|isInSubnet/.test(text) ? 'INFO' : 'LOW', check: CHECK, id: 'lan-by-option', title: 'Qt socket is loopback by default, all interfaces when the LAN option is on' + (/ipAllowed|allowedIps/.test(text) ? ' (sender IP allow-list present)' : ' (no sender allow-list found)'), file: rel });
      if (bind === 'all') { const h = matchAll(text, /QHostAddress::Any\w*/)[0]; ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'bind-all', title: 'Qt server listens on all interfaces', file: rel, line: h ? h.line : 1 }); }
    }
    if (/INADDR_ANY|in6addr_any/.test(text)) { const h = matchAll(text, /INADDR_ANY|in6addr_any/)[0]; ctx.add({ severity: 'LOW', check: CHECK, id: 'bind-any-socket', title: 'socket bound to INADDR_ANY (check the caller makes LAN exposure deliberate)', file: rel, line: h.line }); bind = 'all (INADDR_ANY)'; }
    // the daemon's Options default bind
    const hdr = ctx.read(rel.replace(/\.cpp$/, '.hpp')) || '';
    const def = /std::string\s+bind\s*=\s*"([^"]*)"/.exec(hdr + raw);
    if (def) {
      bind = (LOOPBACK.test(def[1]) ? 'loopback' : ANY.test(def[1]) ? 'all' : def[1]) + ' (Options default)';
      if (ANY.test(def[1])) ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'bind-all', title: 'daemon binds all interfaces by default', file: rel });
    }
    if (routes.length && !ev.auth) ctx.add({ severity: 'HIGH', check: CHECK, id: 'no-auth', title: 'HTTP routes with no authentication evidence', file: rel });
    if (/recv\(|read\(/.test(text) && routes.length && !ev.bodyLimit) ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'no-body-limit', title: 'requests read with no size limit found', file: rel });
    rows.push({ component: componentOf(rel), file: rel, kind: isQt ? 'Qt socket' : routes.length ? 'HTTP / WebSocket (C++)' : 'socket (C++)', bind, routes, auth: routes.length ? ev.auth : 'n/a', rate: ev.rate, bodyLimit: ev.bodyLimit, cors: /Access-Control-Allow-Origin/.test(raw) ? (/Allow-Origin: \*/.test(raw) ? '*' : 'set') : 'none' });
  }
  return rows;
}

function kotlinServers(ctx) {
  const rows = [];
  for (const rel of ctx.textFiles(/\.(kt|java)$/)) {
    if (/\/src\/test\//.test(rel)) continue;
    const t = ctx.read(rel);
    for (const h of matchAll(t, /\b(ServerSocket|DatagramSocket|MulticastSocket|LocalServerSocket)\s*\(/)) {
      rows.push({ component: componentOf(rel), file: rel + ':' + h.line, kind: 'Android ' + h.m[1], bind: /InetAddress\.getLoopbackAddress|127\.0\.0\.1/.test(t) ? 'loopback' : 'check', routes: [], auth: 'n/a', rate: 'n/a', bodyLimit: 'n/a', cors: 'n/a' });
      if (h.m[1] === 'ServerSocket') ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'android-server-socket', title: 'Android app opens a listening TCP socket', file: rel, line: h.line });
    }
  }
  return rows;
}

function componentOf(rel) {
  const map = [[/^engine\/cpp\/app\/daemon\//, 'daemon / web remote'], [/^engine\//, 'engine'], [/^native\/amixos\//, 'console (Qt)'], [/^bridge\/license-server\//, 'portal: licence server'], [/^ota-server\//, 'portal: OTA server'],
    [/^bridge\//, 'bridge (engine service)'], [/^hybrid\//, 'hybrid web remote'], [/^management\//, 'management app'], [/^client\//, 'client finder'], [/^lkms\//, 'LKMS licence service'], [/^scripts\//, 'vendor tools'], [/^tools\//, 'dev tools'], [/^android\//, 'Android app']];
  for (const [re, n] of map) if (re.test(rel)) return n; return rel.split('/')[0];
}

function run(ctx, cfg) {
  const rows = [...jsServers(ctx), ...pyServers(ctx), ...cppServers(ctx, cfg), ...kotlinServers(ctx)];
  ctx.sections.network = rows;
  ctx.note(CHECK, `${rows.length} listening components found, ${rows.reduce((n, r) => n + r.routes.length, 0)} distinct routes`);
}

module.exports = { run, CHECK, routeLiterals, resolveHost };
