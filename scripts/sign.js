'use strict';
// Authenticode signing for the Windows installers (.exe, .msi) with osslsigncode.
//   * SIGN_PFX=<file.pfx> [SIGN_PFX_PASSWORD=...]   sign with your own code-signing certificate (what Windows SmartScreen needs to trust it)
//   * otherwise a self-signed certificate "Audio Mixer (self-signed)" is created once in dist/cache/signing (the key never leaves that
//     folder, which is git-ignored). A self-signed signature proves the file was not changed after it was built and lets the verification
//     scan pin the publisher, but Windows still shows "unknown publisher" until you trust AudioMixer-signing.cer yourself.
// SIGN_TIMESTAMP_URL (for example http://timestamp.digicert.com) adds a trusted timestamp so the signature outlives the certificate.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawnSync } = require('node:child_process');

const SUBJECT = '/CN=Audio Mixer (self-signed)/O=Audio Mixer';

function tool(cmd, args, opts = {}) {
  const r = spawnSync(cmd, args, { encoding: 'utf8', ...opts });
  if (r.error && r.error.code === 'ENOENT') throw new Error(`${cmd} not found (Linux: apt install ${cmd === 'openssl' ? 'openssl' : 'osslsigncode'})`);
  if (r.status !== 0) throw new Error(`${cmd} failed: ${(r.stderr || r.stdout || '').trim().slice(0, 600)}`);
  return r.stdout;
}

// SHA-1 thumbprint of a DER certificate (what Windows shows as "Thumbprint")
function thumbprint(der) { return crypto.createHash('sha1').update(der).digest('hex').toUpperCase(); }

function ensureSigningCert({ dir, env = process.env } = {}) {
  if (env.SIGN_PFX) {
    if (!fs.existsSync(env.SIGN_PFX)) throw new Error('SIGN_PFX file not found: ' + env.SIGN_PFX);
    return { mode: 'pfx', pfx: env.SIGN_PFX, password: env.SIGN_PFX_PASSWORD || '', selfSigned: false };
  }
  fs.mkdirSync(dir, { recursive: true, mode: 0o700 });
  const key = path.join(dir, 'key.pem'), cert = path.join(dir, 'cert.pem'), cer = path.join(dir, 'AudioMixer-signing.cer');
  if (!fs.existsSync(key) || !fs.existsSync(cert)) {
    tool('openssl', ['req', '-x509', '-newkey', 'rsa:3072', '-sha256', '-days', '1095', '-nodes', '-keyout', key, '-out', cert, '-subj', SUBJECT,
      '-addext', 'keyUsage=critical,digitalSignature', '-addext', 'extendedKeyUsage=codeSigning', '-addext', 'basicConstraints=critical,CA:false']);
    try { fs.chmodSync(key, 0o600); } catch (_) { /* best effort */ }
  }
  tool('openssl', ['x509', '-in', cert, '-outform', 'DER', '-out', cer]);
  return { mode: 'self-signed', key, cert, cer, thumbprint: thumbprint(fs.readFileSync(cer)), selfSigned: true };
}

// Signs `file` in place (the unsigned file is replaced only after the signature verifies).
function signFile(file, id, { env = process.env } = {}) {
  const tmp = file + '.signed';
  const args = ['sign', '-h', 'sha256', '-n', 'Audio Mixer', '-i', 'https://github.com/amirf1389/audio-mixer'];
  if (id.mode === 'pfx') args.push('-pkcs12', id.pfx, '-pass', id.password);
  else args.push('-certs', id.cert, '-key', id.key);
  if (env.SIGN_TIMESTAMP_URL) args.push('-t', env.SIGN_TIMESTAMP_URL);
  args.push('-in', file, '-out', tmp);
  tool('osslsigncode', args);
  verifySignature(tmp, id);
  fs.renameSync(tmp, file);
}

// Checks the Authenticode signature: for a self-signed certificate against that certificate, otherwise against the system trust store.
function verifySignature(file, id) {
  const args = ['verify', '-in', file];
  if (id && id.selfSigned) args.push('-CAfile', id.cert);
  const r = spawnSync('osslsigncode', args, { encoding: 'utf8' });
  if (r.error && r.error.code === 'ENOENT') throw new Error('osslsigncode not found (Linux: apt install osslsigncode)');
  const out = (r.stdout || '') + (r.stderr || '');
  if (r.status !== 0 || !/Signature verification: ok/.test(out)) throw new Error('signature check failed for ' + path.basename(file) + ': ' + out.trim().slice(-400));
  return true;
}

module.exports = { ensureSigningCert, signFile, verifySignature, thumbprint };
