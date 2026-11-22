'use strict';
// 1. SECRETS: private keys, tokens, passwords, API keys, .env files, key stores and high-entropy strings anywhere in the tree.
const path = require('node:path');
const { matchAll, shannon, globToRe } = require('../lib/core');

const CHECK = 'secrets';

// Private key material in text form. The header is built from parts so this file never matches itself.
// (a header followed by base64 key data: a header alone in a regex, a comment or a parser is not a key)
const PEM_PRIVATE = new RegExp('-----BEGIN ((?:RSA|EC|DSA|OPENSSH|ENCRYPTED|PGP|ED25519)[ ])?PRIVATE KEY' + '( BLOCK)?-----\\s*\\r?\\n(?:[\\w-]+:[^\\n]*\\r?\\n)*\\s*[A-Za-z0-9+/=]{40,}');
const PEM_CERT = new RegExp('-----BEGIN (CERTIFICATE|PUBLIC KEY|X509 CRL|DH PARAMETERS|CERTIFICATE REQUEST)-----');
// a private JWK: an EC / OKP / RSA key object carrying "d"
const JWK_PRIVATE = /"kty"\s*:\s*"(EC|OKP|RSA)"[^{}]*"d"\s*:\s*"[A-Za-z0-9_-]{20,}"|"d"\s*:\s*"[A-Za-z0-9_-]{20,}"[^{}]*"kty"\s*:\s*"(EC|OKP|RSA)"/;

const TOKENS = [
  { id: 'aws-access-key', re: /\b(AKIA|ASIA)[0-9A-Z]{16}\b/, sev: 'HIGH', title: 'AWS access key id' },
  { id: 'github-token', re: /\b(gh[pousr]_[A-Za-z0-9]{36,}|github_pat_[A-Za-z0-9_]{60,})\b/, sev: 'HIGH', title: 'GitHub token' },
  { id: 'slack-token', re: /\bxox[abposr]-[A-Za-z0-9-]{10,}\b/, sev: 'HIGH', title: 'Slack token' },
  { id: 'google-api-key', re: /\bAIza[0-9A-Za-z_-]{35}\b/, sev: 'HIGH', title: 'Google API key' },
  { id: 'stripe-key', re: /\b(sk|rk)_live_[0-9a-zA-Z]{20,}\b/, sev: 'HIGH', title: 'Stripe live secret key' },
  { id: 'npm-token', re: /\bnpm_[A-Za-z0-9]{36}\b/, sev: 'HIGH', title: 'npm access token' },
  { id: 'openai-style-key', re: /\bsk-(proj-)?[A-Za-z0-9_-]{32,}\b/, sev: 'HIGH', title: 'API secret key (sk-...)' },
  { id: 'private-key-json', re: /"private_key"\s*:\s*"-----BEGIN/, sev: 'HIGH', title: 'service-account JSON with a private key' },
  { id: 'jwt', re: /\beyJ[A-Za-z0-9_-]{10,}\.eyJ[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{20,}\b/, sev: 'MEDIUM', title: 'JSON Web Token' },
  { id: 'url-credentials', re: /\b[a-z][a-z0-9+.-]{1,10}:\/\/[^\s:/@'"`]{1,64}:[^\s:/@'"`$<>{}]{6,128}@[a-z0-9.-]+/i, sev: 'MEDIUM', title: 'credentials inside a URL' },
];

// name = "literal" assignments to secret-looking names
const ASSIGN = /\b([A-Za-z0-9_]*(?:password|passwd|pwd|secret|api[_-]?key|apikey|access[_-]?key|auth[_-]?token|token|private[_-]?key|client[_-]?secret)[A-Za-z0-9_]*)["']?\s*(?::|=|=>|\?\?|\|\|)\s*["'`]([^"'`\s]{6,})["'`]/gi;
const PLACEHOLDER = /^(x+|\*+|\.+|changeme|change[_-]?me|your[_-].*|<.*>|\$\{.*|%.*%|example.*|sample.*|dummy.*|test.*|fake.*|placeholder.*|redacted|none|null|undefined|true|false|string|required|optional|hidden|password|secret|token|bearer|basic|text|number|new-password|current-password|off|on)$/i;
const WEAK = /^(admin|administrator|password|passw0rd|123456+|qwerty|letmein|changeme|secret|default|root|toor)$/i;

const SOURCE_EXT = /\.(js|mjs|cjs|ts|tsx|jsx|py|kt|kts|java|cpp|cc|cxx|c|h|hpp|hh|m|mm|swift|cs|go|rs|rb|php|sh|bash|ps1|bat|cmd|yml|yaml|json|toml|ini|cfg|conf|env|properties|xml|html|htm|gradle|cmake|txt|inc|service|vb|iss|nsi)$|(^|\/)(Dockerfile|Caddyfile|\.env[^/]*)$/i;
const KEYSTORE_EXT = /\.(pfx|p12|jks|keystore|bks|pvk|snk|ppk|asc|gpg|kdbx|key|der)$/i;
const ENV_FILE = /(^|\/)\.env(\.[^/]*)?$|(^|\/)[^/]+\.env$/i;
const ENV_TEMPLATE = /\.(example|sample|template|dist|defaults)$/i;
const TEST_PATH = /(^|\/)(test|tests|__tests__|spec|fixtures?|e2e|mock|mocks)(\/|$)|(^|\/)test\.(js|py)$|\.(test|spec)\.[a-z]+$|_test\.[a-z]+$|(^|\/)test_[^/]+$/i;
const HASHY_CONTEXT = /\b(sha(1|224|256|384|512)|md5|integrity|digest|checksum|hash|etag|nonce|fingerprint|thumbprint|uuid|guid|salt|iv|commit|revision|rev|ref|version|vectors?|sig|signature|kid|x|y|n|e)\b|sha(256|384|512)-|"(x|y|n|e)"\s*:/i;

function allowedSecret(cfg, rel) {
  for (const a of cfg.secretAllow || []) if (globToRe(a.path).test(rel)) return a.reason;
  return null;
}

function publicKeyValues(cfg) {
  const s = new Set(); for (const k of cfg.publicKeys || []) { if (k.x) s.add(k.x); if (k.y) s.add(k.y); } return s;
}

function run(ctx, cfg) {
  const pub = publicKeyValues(cfg);
  const add = (sev, id, title, file, line, detail) => {
    const reason = allowedSecret(cfg, file);
    if (reason && sev !== 'INFO') ctx.add({ severity: 'INFO', check: CHECK, id: id + '-allowlisted', title: title + ' (allowlisted)', file, line, detail: 'allowlist: ' + reason });
    else ctx.add({ severity: sev, check: CHECK, id, title, file, line, detail });
  };
  let entropyHits = 0; const entropyPerFile = new Map();

  for (const rel of ctx.files) {
    const base = path.posix.basename(rel);
    // .env files (templates are fine, and are checked for real values below)
    if (ENV_FILE.test(rel) && !ENV_TEMPLATE.test(rel)) add('HIGH', 'env-file', '.env file present in the tree (would be committed / shipped)', rel, 0, 'move it out of the repository or make sure it is ignored; rotate anything in it that was ever committed');
    // binary key stores / key files by name
    if (KEYSTORE_EXT.test(rel)) {
      const t = ctx.read(rel);
      if (t === null || PEM_PRIVATE.test(t) || /\.(pfx|p12|jks|keystore|bks|pvk|snk|ppk|kdbx)$/i.test(rel)) add('HIGH', 'keystore-file', 'key store / private key file in the tree', rel, 0, 'signing keys and key stores belong outside the repository');
    }
    if (/\.(pem|crt|cer)$/i.test(rel)) {
      const t = ctx.read(rel) || '';
      if (!PEM_PRIVATE.test(t) && PEM_CERT.test(t)) ctx.add({ severity: 'INFO', check: CHECK, id: 'pem-public', title: 'PEM file with public material only (certificate / public key / DH params)', file: rel, line: 1 });
    }
    const text = ctx.read(rel); if (text === null) continue;
    const isTest = TEST_PATH.test(rel);

    // private key blocks
    for (const h of matchAll(text, PEM_PRIVATE)) add('HIGH', 'private-key', 'private key (PEM) in the tree', rel, h.line, 'a private key must never be in the repository; rotate it if it was ever committed');
    for (const h of matchAll(text, JWK_PRIVATE)) add('HIGH', 'private-jwk', 'private JWK (has "d") in the tree', rel, h.line, 'only the public half (x, y) may be committed');
    // well-known token formats
    for (const t of TOKENS) for (const h of matchAll(text, t.re)) {
      const v = h.m[0]; if (/EXAMPLE|XXXX|0000000000/.test(v)) continue;
      if (t.id === 'url-credentials') { const pw = /:\/\/[^:]*:([^@]*)@/.exec(v); if (!pw || PLACEHOLDER.test(pw[1]) || /^(\$|%|<|\{)/.test(pw[1]) || ENV_TEMPLATE.test(rel)) continue; }
      add(isTest && t.sev === 'HIGH' ? 'MEDIUM' : t.sev, t.id, t.title, rel, h.line, 'value starts ' + v.slice(0, 6) + '...');
    }
    if (!SOURCE_EXT.test(rel) || /(^|\/)(package-lock\.json|yarn\.lock|pnpm-lock\.yaml|.*\.lock)$/.test(rel)) continue;

    // .env templates must not carry real values for secret names
    if (ENV_FILE.test(rel) && ENV_TEMPLATE.test(rel)) {
      text.split('\n').forEach((l, i) => {
        const m = /^\s*([A-Z0-9_]*(SECRET|TOKEN|PASSWORD|PASS|KEY)[A-Z0-9_]*)\s*=\s*(.+?)\s*$/.exec(l);
        if (m && !/_FILE$|_PATH$|_DIR$/.test(m[1]) && !PLACEHOLDER.test(m[3].replace(/^["']|["']$/g, '')) && m[3].length >= 8 && !/^(secrets\/|\/run\/|\.\/)/.test(m[3])) add('HIGH', 'env-template-value', 'secret-looking value in an env template: ' + m[1], rel, i + 1, 'templates must leave secrets empty');
      });
    }

    // secret = "literal"
    for (const h of matchAll(text, ASSIGN)) {
      const name = h.m[1], val = h.m[2];
      if (PLACEHOLDER.test(val) || pub.has(val) || /^(process\.env|env\.|os\.environ|getenv|System\.getenv|BuildConfig|\$|%|<|\{|https?:|\/|\.\/|\.\.\/|[A-Z_]{3,}$)/.test(val)) continue;
      if (/(^|_)(file|path|dir|name|field|label|header|type|id|url|key_?id|hint|len|length|min|max|count|prefix|env|var|storage|param|query|cookie|policy|mode|kind|event|route|endpoint|scope|placeholder)$/i.test(name)) continue;
      const lineText = text.split('\n')[h.line - 1] || '';
      if (/\b(getElementById|querySelector|addEventListener|setAttribute|innerText|textContent|\.on\(|emit\(|i18n|tr\(|qsTr|t\()/.test(lineText)) continue;
      if (/\s/.test(val) || /^[a-z]+([-_.][a-z]+)*$/.test(val) && !WEAK.test(val)) continue;   // plain words, ids, css classes
      if (WEAK.test(val)) { add(isTest ? 'LOW' : 'MEDIUM', 'weak-default-credential', 'weak / default credential "' + val + '" for ' + name, rel, h.line, 'never ship a default password; require one to be set'); continue; }
      const hasDigit = /\d/.test(val), hasAlpha = /[A-Za-z]/.test(val);
      if (val.length >= 12 && hasDigit && hasAlpha && shannon(val) >= 3.3) add(isTest ? 'LOW' : 'MEDIUM', 'hardcoded-secret', 'hard-coded value for secret-looking name ' + name, rel, h.line, 'move it to the environment / a key file outside the repo, or confirm it is not a secret');
    }

    // high-entropy literals (review list; very noisy sources such as lock files and generated data are skipped above)
    if (/\.(json|xml|html?|txt)$/i.test(rel) && text.length > 200 * 1024) continue;
    for (const h of matchAll(text, /["'`]([A-Za-z0-9+/=_-]{32,})["'`]/)) {
      const val = h.m[1]; if (pub.has(val)) continue;
      const lineText = text.split('\n')[h.line - 1] || '';
      if (HASHY_CONTEXT.test(lineText) || /^[a-f0-9]+$/i.test(val) && val.length % 8 === 0 || /^[A-Za-z]+$/.test(val) || /^[a-z0-9_-]+$/.test(val) && !/\d.*\d.*\d/.test(val)) continue;
      const classes = [/[a-z]/, /[A-Z]/, /\d/].filter(r => r.test(val)).length;
      if (classes < 3 || shannon(val) < 4.3) continue;
      const n = (entropyPerFile.get(rel) || 0) + 1; entropyPerFile.set(rel, n); entropyHits++;
      if (n <= 3) add('LOW', 'high-entropy', 'high-entropy string literal (review: secret or not?)', rel, h.line, 'length ' + val.length + ', entropy ' + shannon(val).toFixed(2) + ' bits/char, starts ' + val.slice(0, 4) + '...');
    }
  }
  for (const [rel, n] of entropyPerFile) if (n > 3) ctx.note(CHECK, `${rel}: ${n - 3} more high-entropy literals not listed`);
  ctx.note(CHECK, `scanned ${ctx.files.length} files; ${entropyHits} high-entropy literals reviewed`);
}

module.exports = { run, CHECK, PEM_PRIVATE, JWK_PRIVATE };
