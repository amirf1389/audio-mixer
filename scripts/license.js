#!/usr/bin/env node
'use strict';
// Vendor tool for Audio Mixer licenses and update manifests. The PRIVATE key never goes into the repo or the installers.
//   node scripts/license.js init  [--dir D]                       create the vendor key pair (default ~/.audio-mixer-vendor), LOCKED with a PIN you choose
//                                                                  (asked twice; --no-pin keeps a plain private.pem), then embed the public key
//   node scripts/license.js lock  [--dir D]                       encrypt an existing plain private.pem with a PIN (scripts/license-vault.js)
//   node scripts/license.js change-pin [--dir D]                  new PIN for a locked key folder
// A locked folder holds private.pem.enc only; issue / batch / manifest ask for the PIN (or read AUDIO_MIXER_VENDOR_PIN).
//   node scripts/license.js embed [--dir D]                       write the public key into bridge/license-public.json and index.html
//   node scripts/license.js issue --plan basic|pro|studio|master [--name "Jane Doe"] [--email e] [--days 365] [--machine XXXX-...] [--seats 1] [--dir D]
//                                                                  (no --name: an automatic one from the key's random id, "Customer 3F9A12C4B7E0")
//   node scripts/license.js batch --plan pro|studio|master --count 10 [--days 365] [--dir D]   that many random keys, one CSV line each: id,plan,expires,key
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

function initKeys(dir, { pin } = {}) {
  fs.mkdirSync(dir, { recursive: true, mode: 0o700 });
  let priv = path.join(dir, 'private.pem'); const pub = path.join(dir, 'public.jwk.json');
  if (vault.hasKey(dir)) throw new Error('a key already exists in ' + dir + ' (delete it only if you accept that every key issued so far stops working)');
  if (pin !== undefined) vault.checkPin(pin);
  const { privateKey, publicKey } = crypto.generateKeyPairSync('ec', { namedCurve: 'P-256' });
  const pem = privateKey.export({ type: 'pkcs8', format: 'pem' });
  if (pin !== undefined) { vault.writeLocked(dir, pem, pin); priv = path.join(dir, vault.ENC); }   // only the encrypted copy is ever written
  else fs.writeFileSync(priv, pem, { mode: 0o600 });
  const jwk = publicKey.export({ format: 'jwk' });
  fs.writeFileSync(pub, JSON.stringify({ kty: jwk.kty, crv: jwk.crv, x: jwk.x, y: jwk.y }) + '\n');
  return { priv, pub };
}
const vault = require('./license-vault');
const loadPrivate = (dir, pin) => vault.loadKey(dir, pin);   // locked folder: needs the PIN; a plain private.pem: read as before
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

function issue({ dir, plan, name, email, days, machine, seats, now = Date.now(), pin, privateKey }) {
  if (!lic.PLANS[plan]) throw new Error('plan must be one of: ' + Object.keys(lic.PLANS).join(', '));
  const id = crypto.randomBytes(6).toString('hex').toUpperCase();   // 48 random bits from the OS generator: the key's serial
  if (!name) name = 'Customer ' + id;
  const payload = { v: 1, id, plan, name, issued: now, expires: days ? now + Math.round(days * 86400000) : null, seats: seats || 1 };
  if (email) payload.email = email;
  if (machine) payload.mid = machine.toUpperCase();
  return { key: lic.signKey(payload, privateKey || loadPrivate(dir, pin)), payload };
}

async function newPin() {
  const p1 = await vault.askPin(`New PIN (at least ${vault.MIN_PIN} characters; a longer passphrase is stronger): `);
  vault.checkPin(p1);
  if (!process.env.AUDIO_MIXER_VENDOR_PIN && p1 !== await vault.askPin('Same PIN again: ')) throw new Error('the two PINs differ');
  return p1;
}
// the private key once per run: a locked folder asks for the PIN a single time, even for a batch
async function keyFor(dir) { return vault.isLocked(dir) ? loadPrivate(dir, await vault.askPin('PIN for ' + dir + ': ')) : loadPrivate(dir); }

async function main(argv) {
  const cmd = argv[0], a = argv.slice(1);
  try {
    if (cmd === 'init') {
      const dir = vendorDir(a), r = initKeys(dir, a.includes('--no-pin') ? {} : { pin: await newPin() });
      embedPublic(loadPublic(dir));
      console.log(`Key pair created in ${dir}\n  private: ${r.priv}  (${r.priv.endsWith('.enc') ? 'encrypted with your PIN; back it up AND remember the PIN' : 'keep it secret, back it up'})\n  public:  ${r.pub}\nThe public key is embedded in bridge/license-public.json and index.html. Rebuild the installers, then issue keys with "issue".`);
    } else if (cmd === 'lock') { const dir = vendorDir(a); const f = vault.lock(dir, await newPin()); console.log('Locked: ' + f + ' (the plain private.pem was removed). Back up this file and remember the PIN.'); }
    else if (cmd === 'change-pin') { const dir = vendorDir(a); const old = await vault.askPin('Current PIN: '); vault.changePin(dir, old, await newPin()); console.log('PIN changed.'); }
    else if (cmd === 'embed') { embedPublic(loadPublic(vendorDir(a))); console.log('Public key embedded.'); }
    else if (cmd === 'issue') {
      const dir = vendorDir(a);
      const r = issue({ dir, privateKey: await keyFor(dir), plan: arg(a, '--plan'), name: arg(a, '--name'), email: arg(a, '--email'), days: Number(arg(a, '--days', 0)) || 0, machine: arg(a, '--machine'), seats: Number(arg(a, '--seats', 1)) });
      console.log(r.key);
      console.error(`\n${lic.PLANS[r.payload.plan].name} for ${r.payload.name}, ${r.payload.expires ? 'expires ' + new Date(r.payload.expires).toISOString().slice(0, 10) : 'no expiry'}${r.payload.mid ? ', bound to ' + r.payload.mid : ', not bound to a computer'}`);
    } else if (cmd === 'batch') {
      const n = Number(arg(a, '--count', 1)), dir = vendorDir(a);
      if (!Number.isInteger(n) || n < 1 || n > 10000) throw new Error('--count must be 1..10000');
      const privateKey = await keyFor(dir);
      console.log('id,plan,expires,key');
      for (let k = 0; k < n; k++) {
        const r = issue({ dir, privateKey, plan: arg(a, '--plan'), days: Number(arg(a, '--days', 0)) || 0 });
        console.log([r.payload.id, r.payload.plan, r.payload.expires ? new Date(r.payload.expires).toISOString().slice(0, 10) : '', r.key].join(','));
      }
      console.error(`\n${n} ${lic.PLANS[arg(a, '--plan')].name} key(s), random ids, not bound to a computer`);
    } else if (cmd === 'show') {
      const v = lic.verifyKey(a[0]);
      console.log(JSON.stringify(v.ok ? { ok: true, plan: v.plan.name, payload: v.payload } : { ok: false, reason: v.reason, message: v.message, payload: v.payload }, null, 2));
      return v.ok ? 0 : 1;
    } else { console.error('Usage: node scripts/license.js init | lock | change-pin | embed | issue | batch | show | manifest (see the header of this file)'); return 2; }
    return 0;
  } catch (e) { console.error('Error: ' + e.message); return 1; }
}

if (require.main === module) main(process.argv.slice(2)).then(c => process.exit(c));
module.exports = { main, keyFor, initKeys, loadPrivate, loadPublic, embedPublic, issue, vendorDir, MARK_A, MARK_B };
