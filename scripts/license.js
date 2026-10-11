#!/usr/bin/env node
'use strict';
// Vendor tool for Audio Mixer licenses and update manifests. The PRIVATE key never goes into the repo or the installers.
//   node scripts/license.js init  [--dir D]                       create the vendor key pair (default ~/.audio-mixer-vendor), then embed the public key
//   node scripts/license.js embed [--dir D]                       write the public key into bridge/license-public.json and index.html
//   node scripts/license.js issue --plan basic|pro|studio --name "Jane Doe" [--email e] [--days 365] [--machine XXXX-...] [--seats 1] [--dir D]
//   node scripts/license.js show <key>                             decode and verify a key
//   node scripts/license.js manifest [--dir D]                     sign releases/update.json for the OTA updater (see scripts/make-update.js)
// The key directory can also be set with AUDIO_MIXER_VENDOR_DIR. Keep a backup of private.pem: without it no further keys can be issued
// for builds that contain the matching public key.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const lic = require('../bridge/license');

const ROOT = path.resolve(__dirname, '..');
const arg = (a, n, d) => { const i = a.indexOf(n); return i >= 0 ? a[i + 1] : d; };
const vendorDir = a => path.resolve(arg(a, '--dir', process.env.AUDIO_MIXER_VENDOR_DIR || path.join(os.homedir(), '.audio-mixer-vendor')));

function initKeys(dir) {
  fs.mkdirSync(dir, { recursive: true, mode: 0o700 });
  const priv = path.join(dir, 'private.pem'), pub = path.join(dir, 'public.jwk.json');
  if (fs.existsSync(priv)) throw new Error('a key already exists in ' + dir + ' (delete it only if you accept that every key issued so far stops working)');
  const { privateKey, publicKey } = crypto.generateKeyPairSync('ec', { namedCurve: 'P-256' });
  fs.writeFileSync(priv, privateKey.export({ type: 'pkcs8', format: 'pem' }), { mode: 0o600 });
  const jwk = publicKey.export({ format: 'jwk' });
  fs.writeFileSync(pub, JSON.stringify({ kty: jwk.kty, crv: jwk.crv, x: jwk.x, y: jwk.y }) + '\n');
  return { priv, pub };
}
const loadPrivate = dir => crypto.createPrivateKey(fs.readFileSync(path.join(dir, 'private.pem')));
const loadPublic = dir => JSON.parse(fs.readFileSync(path.join(dir, 'public.jwk.json'), 'utf8'));

// The page carries the public key between these markers.
const MARK_A = '/*LICENSE_PUBLIC_JWK*/', MARK_B = '/*END_LICENSE_PUBLIC_JWK*/';
function embedPublic(jwk, root = ROOT) {
  fs.writeFileSync(path.join(root, 'bridge', 'license-public.json'), JSON.stringify(jwk) + '\n');
  const page = path.join(root, 'index.html');
  let s = fs.readFileSync(page, 'utf8');
  const a = s.indexOf(MARK_A), b = s.indexOf(MARK_B);
  if (a < 0 || b < 0) throw new Error('license key markers not found in index.html');
  s = s.slice(0, a + MARK_A.length) + JSON.stringify(jwk) + s.slice(b);
  fs.writeFileSync(page, s);
}

function issue({ dir, plan, name, email, days, machine, seats, now = Date.now() }) {
  if (!lic.PLANS[plan]) throw new Error('plan must be one of: ' + Object.keys(lic.PLANS).join(', '));
  if (!name) throw new Error('--name is required');
  const payload = { v: 1, id: crypto.randomBytes(6).toString('hex').toUpperCase(), plan, name, issued: now, expires: days ? now + Math.round(days * 86400000) : null, seats: seats || 1 };
  if (email) payload.email = email;
  if (machine) payload.mid = machine.toUpperCase();
  return { key: lic.signKey(payload, loadPrivate(dir)), payload };
}

function main(argv) {
  const cmd = argv[0], a = argv.slice(1);
  try {
    if (cmd === 'init') {
      const dir = vendorDir(a), r = initKeys(dir);
      embedPublic(loadPublic(dir));
      console.log(`Key pair created in ${dir}\n  private: ${r.priv}  (keep it secret, back it up)\n  public:  ${r.pub}\nThe public key is embedded in bridge/license-public.json and index.html. Rebuild the installers, then issue keys with "issue".`);
    } else if (cmd === 'embed') { embedPublic(loadPublic(vendorDir(a))); console.log('Public key embedded.'); }
    else if (cmd === 'issue') {
      const r = issue({ dir: vendorDir(a), plan: arg(a, '--plan'), name: arg(a, '--name'), email: arg(a, '--email'), days: Number(arg(a, '--days', 0)) || 0, machine: arg(a, '--machine'), seats: Number(arg(a, '--seats', 1)) });
      console.log(r.key);
      console.error(`\n${lic.PLANS[r.payload.plan].name} for ${r.payload.name}, ${r.payload.expires ? 'expires ' + new Date(r.payload.expires).toISOString().slice(0, 10) : 'no expiry'}${r.payload.mid ? ', bound to ' + r.payload.mid : ', not bound to a computer'}`);
    } else if (cmd === 'show') {
      const v = lic.verifyKey(a[0]);
      console.log(JSON.stringify(v.ok ? { ok: true, plan: v.plan.name, payload: v.payload } : { ok: false, reason: v.reason, message: v.message, payload: v.payload }, null, 2));
      return v.ok ? 0 : 1;
    } else { console.error('Usage: node scripts/license.js init | embed | issue | show | manifest (see the header of this file)'); return 2; }
    return 0;
  } catch (e) { console.error('Error: ' + e.message); return 1; }
}

if (require.main === module) process.exit(main(process.argv.slice(2)));
module.exports = { initKeys, loadPrivate, loadPublic, embedPublic, issue, vendorDir, MARK_A, MARK_B };
