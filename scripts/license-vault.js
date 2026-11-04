'use strict';
// PIN lock for the vendor key folder (~/.audio-mixer-vendor): the private key is kept ONLY as private.pem.enc, encrypted with AES-256-GCM under a key
// derived from the PIN with scrypt (random 16-byte salt and 12-byte IV per file; N = 2^17, r = 8, p = 1, about 0.3 s and 128 MB per try, so guessing
// is slow). GCM's tag means a wrong PIN or a changed file is detected, never mistaken for a key. Nothing here stores, logs or sends the PIN.
//   A PIN of 6+ characters is required; a longer passphrase is much stronger: anyone who copies the .enc file can try PINs offline at the scrypt
//   rate, so 6 digits (10^6 tries) slows them down, it does not stop them. The rate limit in the generator only guards the running page.
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const ENC = 'private.pem.enc', PLAIN = 'private.pem';
const KDF = { N: 1 << 17, r: 8, p: 1, maxmem: 256 * 1024 * 1024 };
const MIN_PIN = 6;

function checkPin(pin) {
  if (typeof pin !== 'string' || pin.length < MIN_PIN) { const e = new Error(`the PIN must be at least ${MIN_PIN} characters`); e.status = 400; throw e; }
  if (pin.length > 256) { const e = new Error('the PIN is too long'); e.status = 400; throw e; }
}
function derive(pin, salt, kdf = KDF) { return crypto.scryptSync(Buffer.from(pin, 'utf8'), salt, 32, kdf); }

function encrypt(pem, pin) {
  checkPin(pin);
  const salt = crypto.randomBytes(16), iv = crypto.randomBytes(12), key = derive(pin, salt);
  const c = crypto.createCipheriv('aes-256-gcm', key, iv), ct = Buffer.concat([c.update(pem, 'utf8'), c.final()]);
  return { v: 1, alg: 'aes-256-gcm', kdf: 'scrypt', N: KDF.N, r: KDF.r, p: KDF.p, salt: salt.toString('base64'), iv: iv.toString('base64'), tag: c.getAuthTag().toString('base64'), ct: ct.toString('base64') };
}
function decrypt(box, pin) {
  if (!box || box.v !== 1 || box.alg !== 'aes-256-gcm' || box.kdf !== 'scrypt') throw new Error('not a key vault file');
  const key = derive(String(pin || ''), Buffer.from(box.salt, 'base64'), { N: box.N, r: box.r, p: box.p, maxmem: KDF.maxmem });
  const d = crypto.createDecipheriv('aes-256-gcm', key, Buffer.from(box.iv, 'base64'));
  d.setAuthTag(Buffer.from(box.tag, 'base64'));
  try { return Buffer.concat([d.update(Buffer.from(box.ct, 'base64')), d.final()]).toString('utf8'); }
  catch (_) { const e = new Error('wrong PIN'); e.status = 401; e.code = 'WRONG_PIN'; throw e; }
}

const isLocked = dir => fs.existsSync(path.join(dir, ENC));
const hasKey = dir => isLocked(dir) || fs.existsSync(path.join(dir, PLAIN));

function writeLocked(dir, pem, pin) {
  const box = encrypt(pem, pin), f = path.join(dir, ENC), tmp = f + '.tmp';
  fs.writeFileSync(tmp, JSON.stringify(box) + '\n', { mode: 0o600 });
  if (decrypt(JSON.parse(fs.readFileSync(tmp, 'utf8')), pin) !== pem) throw new Error('the encrypted copy did not read back');   // never lose the key
  fs.renameSync(tmp, f);
}
// Encrypt an existing plain private.pem in place: written, read back, and only then is the plain file overwritten and removed.
function lock(dir, pin) {
  checkPin(pin);
  const plain = path.join(dir, PLAIN);
  if (isLocked(dir)) { const e = new Error('this key folder is already locked'); e.status = 409; throw e; }
  if (!fs.existsSync(plain)) { const e = new Error('no private.pem in ' + dir); e.status = 404; throw e; }
  const pem = fs.readFileSync(plain, 'utf8');
  writeLocked(dir, pem, pin);
  try { fs.writeFileSync(plain, crypto.randomBytes(pem.length)); } catch (_) { /* best effort: SSDs may keep old blocks */ }
  fs.unlinkSync(plain);
  return path.join(dir, ENC);
}
function changePin(dir, oldPin, newPin) { checkPin(newPin); const pem = decrypt(readBox(dir), oldPin); writeLocked(dir, pem, newPin); }
function readBox(dir) { return JSON.parse(fs.readFileSync(path.join(dir, ENC), 'utf8')); }
// The private key: from the vault with the PIN, or from a plain private.pem (folders made before the lock existed).
function loadKey(dir, pin) {
  if (isLocked(dir)) {
    if (!pin) { const e = new Error('the key folder is locked: give the PIN (AUDIO_MIXER_VENDOR_PIN, or type it when asked)'); e.status = 401; e.code = 'LOCKED'; throw e; }
    return crypto.createPrivateKey(decrypt(readBox(dir), pin));
  }
  return crypto.createPrivateKey(fs.readFileSync(path.join(dir, PLAIN)));
}

// Asks for the PIN on a terminal without echoing it (for the command-line tools).
function askPin(question = 'PIN: ') {
  if (process.env.AUDIO_MIXER_VENDOR_PIN) return Promise.resolve(process.env.AUDIO_MIXER_VENDOR_PIN);
  if (!process.stdin.isTTY) return Promise.reject(new Error('the key folder is locked: set AUDIO_MIXER_VENDOR_PIN or run this in a terminal'));
  return new Promise(resolve => {
    process.stderr.write(question);
    const stdin = process.stdin; let s = '';
    stdin.setRawMode(true); stdin.resume(); stdin.setEncoding('utf8');
    const on = ch => {
      if (ch === '\r' || ch === '\n' || ch === '\u0004') { stdin.setRawMode(false); stdin.pause(); stdin.removeListener('data', on); process.stderr.write('\n'); resolve(s); }
      else if (ch === '\u0003') { process.stderr.write('\n'); process.exit(130); }
      else if (ch === '\u007f' || ch === '\b') s = s.slice(0, -1);
      else s += ch;
    };
    stdin.on('data', on);
  });
}

module.exports = { ENC, PLAIN, MIN_PIN, encrypt, decrypt, isLocked, hasKey, lock, changePin, writeLocked, loadKey, askPin, checkPin };
