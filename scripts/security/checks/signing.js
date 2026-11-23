'use strict';
// 2. SIGNING SAFETY: no private signing key in the repo, every public key in the tree is a known (pinned) one, the pinned key is where
// the apps expect it, and the code that installs updates / accepts licences really verifies signatures and hashes.
const path = require('node:path');
const { matchAll } = require('../lib/core');
const { PEM_PRIVATE, JWK_PRIVATE } = require('./secrets');

const CHECK = 'signing';
const JWK_PUBLIC = /"kty"\s*:\s*"EC"[^{}]*?"x"\s*:\s*"([A-Za-z0-9_-]{40,})"/;
const PRIVATE_NAME = /(^|\/)[^/]*(private|signing|vendor-priv|release-key|upload-key)[^/]*\.(pem|key|jwk|json|der|p8|pfx|p12|jks|keystore)$/i;

function run(ctx, cfg) {
  const known = new Map(); for (const k of cfg.publicKeys || []) known.set(k.x, k);
  // a) no private key file by name, and none by content (the secrets check reports content too; here it is the signing verdict)
  let privateFound = 0;
  for (const rel of ctx.files) {
    if (/^native\/asio\/src\/(examples|tests)\//.test(rel)) continue;   // upstream demo material, see config.secretAllow
    const t = ctx.read(rel);
    const named = PRIVATE_NAME.test(rel) && !/(public|\.pub)/i.test(path.posix.basename(rel));
    const content = t !== null && (PEM_PRIVATE.test(t) || JWK_PRIVATE.test(t));
    if (content || (named && (t === null || /"d"\s*:|PRIVATE KEY/.test(t)))) {
      privateFound++;
      ctx.add({ severity: 'HIGH', check: CHECK, id: 'private-signing-key', title: 'private key material in the repository', file: rel, line: 1, detail: 'signing keys must stay in the vendor folder outside the repo; rotate the key and remove it from git history' });
    }
  }
  if (!privateFound) ctx.add({ severity: 'INFO', check: CHECK, id: 'no-private-key', title: 'no private signing key found in the tree' });

  // b) every EC public key (JWK form) in the tree is a known one
  for (const rel of ctx.files) {
    if (!/\.(json|js|mjs|kt|cpp|hpp|h|html|py)$/.test(rel)) continue;
    const t = ctx.read(rel); if (t === null) continue;
    for (const h of matchAll(t, JWK_PUBLIC)) {
      if (known.has(h.m[1])) continue;
      if (/(test|fixture|vector|sandbox|example|e2e)/i.test(rel)) { ctx.add({ severity: 'INFO', check: CHECK, id: 'test-public-key', title: 'test / sandbox public key (not trusted by release builds?)', file: rel, line: h.line }); continue; }
      ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'unknown-public-key', title: 'EC public key that is not in the pinned list', file: rel, line: h.line, detail: 'if the apps trust it, add it to scripts/security/config.json publicKeys after review; otherwise remove it' });
    }
  }
  // c) the pinned key really is pinned where the apps read it
  for (const k of cfg.publicKeys || []) for (const rel of k.mustBePinnedIn || []) {
    const t = ctx.read(rel);
    if (t === null) ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'pin-file-missing', title: 'expected key pin file is missing: ' + k.name, file: rel });
    else if (!t.includes(k.x) || (k.y && !t.includes(k.y))) ctx.add({ severity: 'HIGH', check: CHECK, id: 'pin-mismatch', title: 'pinned public key differs from the known vendor key', file: rel, detail: k.name });
    else ctx.add({ severity: 'INFO', check: CHECK, id: 'pinned', title: 'vendor public key pinned', file: rel, line: t.slice(0, t.indexOf(k.x)).split('\n').length });
  }
  // d) verification code is present and has the expected calls
  const expect = (list, sevMissing) => {
    for (const v of list || []) {
      const t = ctx.read(v.file);
      if (t === null) { ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'verifier-missing', title: 'expected file not found: ' + v.title, file: v.file }); continue; }
      const miss = v.must.filter(r => !new RegExp(r).test(t));
      if (miss.length) ctx.add({ severity: sevMissing, check: CHECK, id: 'verifier-incomplete', title: 'signature / hash verification not found: ' + v.title, file: v.file, detail: 'missing: ' + miss.join(', ') });
      else ctx.add({ severity: 'INFO', check: CHECK, id: 'verifier-ok', title: v.title, file: v.file });
    }
  };
  expect(cfg.signatureVerifiers, 'HIGH');
  expect(cfg.signers, 'MEDIUM');

  // e) weak / bypassable verification patterns
  for (const rel of ctx.textFiles(/\.(js|mjs|kt|java|py)$/)) {
    if (/(^|\/)(test|tests|e2e)\/|\.test\.|_test\.|(^|\/)test\.js$|(^|\/)test_|^scripts\/security\//.test(rel)) continue;
    const t = ctx.read(rel);
    for (const h of matchAll(t, /rejectUnauthorized\s*:\s*false|NODE_TLS_REJECT_UNAUTHORIZED\s*=\s*['"]?0|verify\s*=\s*False|HostnameVerifier\s*\{[^}]*true|X509TrustManager|ALLOW_ALL_HOSTNAME_VERIFIER/)) {
      ctx.add({ severity: 'HIGH', check: CHECK, id: 'tls-verification-off', title: 'TLS certificate verification disabled', file: rel, line: h.line, detail: h.m[0] });
    }
    for (const h of matchAll(t, /createHash\(\s*['"](md5|sha1)['"]\s*\)/)) {
      const line = t.split('\n')[h.line - 1];
      if (/etag|cache|nonce|id\b|fingerprint/i.test(line)) continue;
      ctx.add({ severity: 'LOW', check: CHECK, id: 'weak-hash', title: 'MD5 / SHA-1 hash (fine for ids, not for integrity)', file: rel, line: h.line });
    }
  }
}

module.exports = { run, CHECK };
