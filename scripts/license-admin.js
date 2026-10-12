#!/usr/bin/env node
'use strict';
// ADMIN license issuer: a second, delegatable signing authority alongside the vendor key (scripts/license.js) — so a support team or a reseller can issue
// real PRO / STUDIO license keys, with an expiration date, without ever holding the vendor's own private key. A key this tool signs verifies exactly like
// a vendor key (bridge/license.js, and the page's own WebCrypto check) on every installed copy, once the admin PUBLIC key is embedded (see "embed" below)
// and the installers are rebuilt. The PRIVATE key never goes into this repository, a server process, or an environment variable: it stays only on the
// machine of whoever runs "init" here, the same rule as the vendor key in scripts/license.js.
//   node scripts/license-admin.js init   [--dir D]           create the admin key pair (default ~/.audio-mixer-admin), embed its public half
//   node scripts/license-admin.js embed  [--dir D]           (re-)write the public key into bridge/license-admin-public.json and index.html
//   node scripts/license-admin.js issue --plan pro|studio --name "Client Co" [--email e] [--days 30] [--machine XXXX-...] [--seats 1] [--dir D]
//   node scripts/license-admin.js show <key>                 decode and verify a key (against the vendor, admin and, in sandbox, the sandbox key)
// The key directory can also be set with AUDIO_MIXER_ADMIN_DIR. Keep a backup of admin-private.pem: without it this admin identity cannot issue further
// keys (keys it already issued keep working: the public key stays trusted, and verification never needs the private key again). To stop trusting a
// compromised admin key, delete bridge/license-admin-public.json, run "embed" with no key pair restored (or just leave the file deleted) and rebuild the
// installers: index.html's LICENSE_ADMIN_JWK marker goes back to null and no copy of the app trusts that key, or anything it signed, again.
// "issue" defaults to a 30-day key (an admin-issued key is meant to be a bounded upgrade, not a permanent one); pass --days 0 for no expiry.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const lic = require('../bridge/license');

const ROOT = path.resolve(__dirname, '..');
const arg = (a, n, d) => { const i = a.indexOf(n); return i >= 0 ? a[i + 1] : d; };
const adminDir = a => path.resolve(arg(a, '--dir', process.env.AUDIO_MIXER_ADMIN_DIR || path.join(os.homedir(), '.audio-mixer-admin')));

function initKeys(dir) {
  fs.mkdirSync(dir, { recursive: true, mode: 0o700 });
  const priv = path.join(dir, 'admin-private.pem'), pub = path.join(dir, 'admin-public.jwk.json');
  if (fs.existsSync(priv)) throw new Error('an admin key already exists in ' + dir + ' (delete it only if you accept that every key it issued stops working)');
  const { privateKey, publicKey } = crypto.generateKeyPairSync('ec', { namedCurve: 'P-256' });
  fs.writeFileSync(priv, privateKey.export({ type: 'pkcs8', format: 'pem' }), { mode: 0o600 });
  const jwk = publicKey.export({ format: 'jwk' });
  fs.writeFileSync(pub, JSON.stringify({ kty: jwk.kty, crv: jwk.crv, x: jwk.x, y: jwk.y }) + '\n');
  return { priv, pub };
}
const loadPrivate = dir => crypto.createPrivateKey(fs.readFileSync(path.join(dir, 'admin-private.pem')));
const loadPublic = dir => JSON.parse(fs.readFileSync(path.join(dir, 'admin-public.jwk.json'), 'utf8'));

// The page carries the admin public key between these markers, the same way it carries the vendor one.
const MARK_A = '/*LICENSE_ADMIN_JWK*/', MARK_B = '/*END_LICENSE_ADMIN_JWK*/';
function embedPublic(jwk, root = ROOT) {
  fs.writeFileSync(path.join(root, 'bridge', 'license-admin-public.json'), JSON.stringify(jwk) + '\n');
  const page = path.join(root, 'index.html');
  let s = fs.readFileSync(page, 'utf8');
  const a = s.indexOf(MARK_A), b = s.indexOf(MARK_B);
  if (a < 0 || b < 0) throw new Error('admin license key markers not found in index.html');
  s = s.slice(0, a + MARK_A.length) + JSON.stringify(jwk) + s.slice(b);
  fs.writeFileSync(page, s);
}

function issue({ dir, plan, name, email, days, machine, seats, now = Date.now() }) {
  if (!lic.PLANS[plan]) throw new Error('plan must be one of: ' + Object.keys(lic.PLANS).join(', '));
  if (!name) throw new Error('--name is required');
  const payload = { v: 1, id: 'ADM-' + crypto.randomBytes(6).toString('hex').toUpperCase(), plan, name, issued: now, expires: days ? now + Math.round(days * 86400000) : null, seats: seats || 1, admin: true };
  if (email) payload.email = email;
  if (machine) payload.mid = machine.toUpperCase();
  return { key: lic.signKey(payload, loadPrivate(dir)), payload };
}

function main(argv) {
  const cmd = argv[0], a = argv.slice(1);
  try {
    if (cmd === 'init') {
      const dir = adminDir(a), r = initKeys(dir);
      embedPublic(loadPublic(dir));
      console.log(`Admin key pair created in ${dir}\n  private: ${r.priv}  (keep it secret, back it up; never commit it or put it in a server's environment)\n  public:  ${r.pub}\nThe public key is embedded in bridge/license-admin-public.json and index.html. Rebuild the installers, then issue keys with "issue".`);
    } else if (cmd === 'embed') { embedPublic(loadPublic(adminDir(a))); console.log('Admin public key embedded.'); }
    else if (cmd === 'issue') {
      const r = issue({ dir: adminDir(a), plan: arg(a, '--plan'), name: arg(a, '--name'), email: arg(a, '--email'), days: Number(arg(a, '--days', 30)) || 0, machine: arg(a, '--machine'), seats: Number(arg(a, '--seats', 1)) });
      console.log(r.key);
      console.error(`\nADMIN-ISSUED ${lic.PLANS[r.payload.plan].name} for ${r.payload.name}, ${r.payload.expires ? 'expires ' + new Date(r.payload.expires).toISOString().slice(0, 10) : 'no expiry'}${r.payload.mid ? ', bound to ' + r.payload.mid : ', not bound to a computer'}`);
    } else if (cmd === 'show') {
      const v = lic.verifyKey(a[0]);
      console.log(JSON.stringify(v.ok ? { ok: true, plan: v.plan.name, payload: v.payload } : { ok: false, reason: v.reason, message: v.message, payload: v.payload }, null, 2));
      return v.ok ? 0 : 1;
    } else { console.error('Usage: node scripts/license-admin.js init | embed | issue | show (see the header of this file)'); return 2; }
    return 0;
  } catch (e) { console.error('Error: ' + e.message); return 1; }
}

if (require.main === module) process.exit(main(process.argv.slice(2)));
module.exports = { initKeys, loadPrivate, loadPublic, embedPublic, issue, adminDir, MARK_A, MARK_B };
