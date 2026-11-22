'use strict';
// Unit tests of the security audit:  node --test scripts/security/test/
// The fixture tree (scripts/security/test-fixtures/repo, files ending in .fx) is copied to a temporary folder; the secrets are planted
// there at run time (a freshly generated private key, an AWS-style key id, a .env file), so no secret-looking value is ever committed.
const test = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawnSync } = require('node:child_process');
const { audit } = require('../audit');
const { stripC, listFiles, shannon } = require('../lib/core');
const { routeLiterals, resolveHost } = require('../checks/network');

const FIX = path.join(__dirname, '..', 'test-fixtures');
const CONFIG = path.join(FIX, 'config.json');
const AUDIT = path.join(__dirname, '..', 'audit.js');

function makeTree() {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'amfx-audit-'));
  const walk = (from, to) => {
    for (const e of fs.readdirSync(from, { withFileTypes: true })) {
      const src = path.join(from, e.name);
      if (e.isDirectory()) { fs.mkdirSync(path.join(to, e.name), { recursive: true }); walk(src, path.join(to, e.name)); }
      else fs.copyFileSync(src, path.join(to, e.name.replace(/\.fx$/, '')));
    }
  };
  walk(path.join(FIX, 'repo'), root);
  // planted at run time
  const { privateKey } = crypto.generateKeyPairSync('ec', { namedCurve: 'P-256' });
  fs.writeFileSync(path.join(root, 'keys', 'leaked.pem'), privateKey.export({ type: 'pkcs8', format: 'pem' }));
  const jwk = privateKey.export({ format: 'jwk' });
  fs.writeFileSync(path.join(root, 'keys', 'leaked.jwk.json'), JSON.stringify(jwk));
  fs.writeFileSync(path.join(root, '.env'), 'API_TOKEN=' + crypto.randomBytes(24).toString('base64url') + '\n');
  fs.writeFileSync(path.join(root, 'server', 'cloud.js'), "module.exports = { id: '" + 'AKIA' + 'Q7XRT2MZ4KD9WB3N' + "' };\n");
  fs.mkdirSync(path.join(root, 'allowed'));
  fs.writeFileSync(path.join(root, 'allowed', 'demo.pem'), privateKey.export({ type: 'pkcs8', format: 'pem' }));
  return root;
}

let ROOT, R;
test.before(() => { ROOT = makeTree(); R = audit({ root: ROOT, config: CONFIG, offline: true, noTools: true }); });
test.after(() => { if (ROOT) fs.rmSync(ROOT, { recursive: true, force: true }); });

const find = (pred) => R.findings.filter(pred);
const has = (check, id, file, sev) => find(f => f.check === check && f.id === id && (!file || f.file === file) && (!sev || f.severity === sev)).length > 0;

test('secrets: planted private key, private JWK, AWS key id and .env file are HIGH; the allow-listed copy is INFO', () => {
  assert.ok(has('secrets', 'private-key', 'keys/leaked.pem', 'HIGH'));
  assert.ok(has('secrets', 'private-jwk', 'keys/leaked.jwk.json', 'HIGH'));
  assert.ok(has('secrets', 'aws-access-key', 'server/cloud.js', 'HIGH'));
  assert.ok(has('secrets', 'env-file', '.env', 'HIGH'));
  assert.ok(has('secrets', 'private-key-allowlisted', 'allowed/demo.pem', 'INFO'));
  assert.ok(!has('secrets', 'private-key', 'allowed/demo.pem'), 'allow-list downgrades, it does not hide');
  const line = find(f => f.id === 'private-key' && f.file === 'keys/leaked.pem')[0].line;
  assert.strictEqual(line, 1, 'file:line points at the key');
});

test('signing: private key material, unknown public key, known key pinned, verifier missing its calls', () => {
  assert.ok(has('signing', 'private-signing-key', 'keys/leaked.pem', 'HIGH'));
  assert.ok(has('signing', 'unknown-public-key', 'keys/unknown.json', 'MEDIUM'));
  assert.ok(!has('signing', 'unknown-public-key', 'keys/known.json'), 'the pinned key is known');
  assert.ok(has('signing', 'pinned', 'keys/known.json', 'INFO'));
  assert.ok(has('signing', 'verifier-incomplete', 'server/safe.js', 'HIGH'));
});

test('cpp: unsafe calls with their lines, comments and strings ignored, bounded memcpy ignored, hardening flags missing', () => {
  const f = 'engine/x/unsafe.cpp';
  const at = id => find(x => x.file === f && x.id === id).map(x => x.line);
  assert.deepStrictEqual(at('unsafe-gets'), [7]);
  assert.deepStrictEqual(at('unsafe-strcpy'), [8], 'the commented-out strcpy on line 12 is not reported');
  assert.deepStrictEqual(at('unsafe-sprintf'), [9], 'snprintf is not sprintf');
  assert.deepStrictEqual(at('unsafe-system'), [10]);
  assert.deepStrictEqual(at('unsafe-strcat'), [], 'strcat inside a string literal is not a call');
  assert.deepStrictEqual(at('memcpy-size'), [11], 'memcpy(.., sizeof line) is bounded');
  assert.strictEqual(find(x => x.id === 'unsafe-gets')[0].severity, 'HIGH');
  assert.ok(has('cpp', 'hardening-stack-protector', 'engine/x/CMakeLists.txt', 'MEDIUM'));
  assert.ok(has('cpp', 'hardening-fortify', 'engine/x/CMakeLists.txt', 'MEDIUM'));
});

test('network: unsafe server flagged (all interfaces, no auth, traversal, reflected CORS); the safe one is not', () => {
  const bad = 'server/server.js', good = 'server/safe.js';
  assert.ok(has('network', 'bind-all', bad, 'MEDIUM'));
  assert.ok(has('network', 'no-auth', bad, 'HIGH'));
  assert.ok(has('network', 'path-traversal', bad, 'HIGH'));
  assert.ok(has('network', 'cors-reflect', bad, 'HIGH'));
  assert.ok(has('network', 'no-body-limit', bad));
  assert.strictEqual(find(x => x.file === good && x.check === 'network' && (x.severity === 'HIGH' || x.severity === 'MEDIUM')).length, 0, JSON.stringify(find(x => x.file === good)));
  assert.ok(has('network', 'file-route-guarded', good, 'INFO'));
  const rows = R.sections.network; const g = rows.find(r => r.file === good), b = rows.find(r => r.file === bad);
  assert.strictEqual(g.bind, 'loopback'); assert.strictEqual(b.bind, 'all'); assert.deepStrictEqual(b.routes, ['/api/delete']); assert.strictEqual(g.auth, true);
});

test('portal: default JWT secret, fast password hash, interpolated SQL, root container, secret in image, Caddy without HTTPS', () => {
  assert.ok(has('portal', 'default-secret', 'portal/auth.js', 'HIGH'));
  assert.ok(has('portal', 'fast-password-hash', 'portal/auth.js', 'HIGH'));
  assert.ok(has('portal', 'sql-string-built', 'portal/auth.js', 'HIGH'));
  assert.ok(has('portal', 'docker-root', 'deploy/Dockerfile', 'HIGH'));
  assert.ok(has('portal', 'docker-secret', 'deploy/Dockerfile', 'HIGH'));
  assert.ok(has('portal', 'docker-unpinned', 'deploy/Dockerfile', 'LOW'));
  assert.ok(has('portal', 'caddy-no-https', 'deploy/Caddyfile', 'HIGH'));
  assert.ok(has('portal', 'caddy-plain-http', 'deploy/Caddyfile', 'MEDIUM'));
  assert.ok(has('portal', 'caddy-no-hsts', 'deploy/Caddyfile', 'MEDIUM'));
});

test('android: debuggable, backup, cleartext, exported service / provider, user CAs, unverified install, JS bridge', () => {
  const m = 'android/app/src/main/AndroidManifest.xml';
  assert.ok(has('android', 'debuggable', m, 'HIGH'));
  assert.ok(has('android', 'allow-backup', m, 'MEDIUM'));
  assert.ok(has('android', 'cleartext', m, 'MEDIUM'));
  assert.ok(has('android', 'exported-component', m, 'MEDIUM'));
  assert.ok(has('android', 'exported-provider', m, 'HIGH'));
  assert.ok(!find(x => x.file === m && /\.Main/.test(x.title)).length, 'the launcher activity is exported by design');
  assert.ok(has('android', 'nsc-cleartext-base', 'android/app/src/main/res/xml/network_security_config.xml', 'MEDIUM'));
  assert.ok(has('android', 'nsc-user-ca', 'android/app/src/main/res/xml/network_security_config.xml', 'MEDIUM'));
  assert.ok(has('android', 'install-unverified', 'android/app/src/main/java/com/example/Updater.kt', 'HIGH'));
  assert.ok(has('android', 'webview-js-bridge', 'android/app/src/main/java/com/example/Updater.kt', 'HIGH'));
});

test('web: CDN script without SRI on a sign-in page is HIGH, inline secret, staff page in the public root; SRI + CSP page is clean', () => {
  assert.ok(has('web', 'script-no-sri', 'site/login.html', 'HIGH'));
  assert.ok(has('web', 'inline-secret', 'site/login.html', 'HIGH'));
  assert.ok(has('web', 'admin-page-public', 'site/login.html', 'MEDIUM'));
  assert.ok(has('web', 'no-csp', 'site/login.html'));
  assert.strictEqual(find(x => x.file === 'site/index.html' && x.severity !== 'INFO').length, 0);
});

test('deps: vendored folder without a licence file is MEDIUM', () => {
  assert.ok(has('deps', 'licence-missing', 'third/libfoo', 'MEDIUM'));
  assert.ok(R.sections.vendored.some(v => v.path === 'third/libfoo' && v.detected === 'NONE'));
});

test('CLI: writes the Markdown and JSON reports and exits 1 on HIGH; --fail-on is respected', () => {
  const out = path.join(ROOT, 'out', 'report.md'); fs.mkdirSync(path.dirname(out));
  const r = spawnSync(process.execPath, [AUDIT, '--root', ROOT, '--config', CONFIG, '--out', out, '--offline', '--no-tools', '--quiet'], { encoding: 'utf8' });
  assert.strictEqual(r.status, 1, r.stdout + r.stderr);
  const md = fs.readFileSync(out, 'utf8'), json = JSON.parse(fs.readFileSync(out.replace(/\.md$/, '.json'), 'utf8'));
  assert.match(md, /\| HIGH \| secrets \| `keys\/leaked\.pem:1` \|/);
  assert.match(md, /### Listening components/);
  assert.ok(json.counts.HIGH > 10 && json.findings.every(f => ['HIGH', 'MEDIUM', 'LOW', 'INFO'].includes(f.severity)));
  // a clean tree passes
  const clean = fs.mkdtempSync(path.join(os.tmpdir(), 'amfx-audit-clean-'));
  try {
    fs.writeFileSync(path.join(clean, 'README.md'), '# nothing to see\n');
    const c = spawnSync(process.execPath, [AUDIT, '--root', clean, '--config', CONFIG, '--out', path.join(clean, 'r.md'), '--offline', '--no-tools', '--quiet', '--only', 'secrets,cpp,network,web'], { encoding: 'utf8' });
    assert.strictEqual(c.status, 0, c.stdout + c.stderr);
  } finally { fs.rmSync(clean, { recursive: true, force: true }); }
  const bad = spawnSync(process.execPath, [AUDIT, '--fail-on', 'nope'], { encoding: 'utf8' });
  assert.strictEqual(bad.status, 2);
});

test('helpers: comment / string stripping keeps lines, entropy, route and host parsing, fixtures excluded from the real scan', () => {
  const s = stripC('a(); // strcpy(x)\n/* gets(\n */ "sprintf(" R"(system()" b();');
  assert.strictEqual(s.split('\n').length, 3); assert.ok(!/strcpy|gets|sprintf|system/.test(s)); assert.ok(/a\(\);/.test(s) && /b\(\);/.test(s));
  assert.ok(shannon('aaaaaaaa') === 0 && shannon('aB3$kL9#') > 2.9);
  assert.deepStrictEqual(routeLiterals("if (url.pathname === '/a') x; app.post('/b', admin, f); case '/c':"), ['/a', '/b', '/c']);
  assert.strictEqual(resolveHost("'127.0.0.1'", ''), 'loopback');
  assert.strictEqual(resolveHost('HOST', "const HOST = process.env.X_HOST || '127.0.0.1';"), 'loopback (env / option override)');
  assert.strictEqual(resolveHost("'0.0.0.0'", ''), 'all');
  const real = listFiles(path.join(__dirname, '..', '..', '..'));
  assert.ok(real.length > 100 && !real.some(f => f.startsWith('scripts/security/test-fixtures/')), 'the real scan never sees the planted fixtures');
});
