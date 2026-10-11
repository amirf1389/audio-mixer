'use strict';
// Consumer licensing: signed license keys, plans, machine code and activation storage.
//   Key = "AMIX1." + base64url(payload JSON) + "." + base64url(ECDSA P-256 / SHA-256 signature, 64 bytes IEEE-P1363).
//   Keys are issued by the vendor with scripts/license.js (private key stays with the vendor); the app only holds the PUBLIC key
//   (license-public.json) and can verify a key offline, in Node here and in the page with WebCrypto.
// Without a valid key the app runs the BASIC plan (8 channels). Note: checks in a locally running app can be bypassed by someone who edits
// the code; the signature stops forged keys, not a determined local modification.
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { execFileSync } = require('node:child_process');

const PUBLIC_JWK = require('./license-public.json');

// SANDBOX (development only): with AUDIO_MIXER_SANDBOX=1 and a bridge/license-sandbox-public.json made by `node scripts/license-sandbox.js init`, keys signed with the
// sandbox key are accepted as well as vendor keys, so the whole key request -> issue -> activate flow can be tried without the vendor key. That file is never committed or
// packaged (it is not in scripts/build.js), so installed copies cannot enter sandbox mode.
function sandboxJwk({ env = process.env, file = path.join(__dirname, 'license-sandbox-public.json') } = {}) {
  if (env.AUDIO_MIXER_SANDBOX !== '1') return null;
  try { const j = JSON.parse(fs.readFileSync(file, 'utf8')); return j && j.kty === 'EC' && j.crv === 'P-256' && j.x && j.y ? { kty: j.kty, crv: j.crv, x: j.x, y: j.y } : null; } catch (_) { return null; }
}

// One table drives everything: the mixer page, the bridge and the vendor tool.
const PLANS = {
  basic:  { id: 'basic',  name: 'BASIC',  channels: 8,  features: ['core'] },
  pro:    { id: 'pro',    name: 'PRO',    channels: 16, features: ['core', 'plugins', 'ota'] },
  studio: { id: 'studio', name: 'STUDIO', channels: 32, features: ['core', 'plugins', 'ota', 'studio'] },
};
const PREFIX = 'AMIX1';

const b64u = buf => Buffer.from(buf).toString('base64').replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
const unb64u = s => Buffer.from(String(s).replace(/-/g, '+').replace(/_/g, '/'), 'base64');

function publicKey(jwk = PUBLIC_JWK) { return crypto.createPublicKey({ key: jwk, format: 'jwk' }); }

// Vendor side (also used by tests): sign a payload with a private KeyObject.
function signKey(payload, privateKey) {
  const body = b64u(Buffer.from(JSON.stringify(payload)));
  const sig = crypto.sign('sha256', Buffer.from(body), { key: privateKey, dsaEncoding: 'ieee-p1363' });
  return `${PREFIX}.${body}.${b64u(sig)}`;
}

// Checks a key: shape, signature, plan, validity dates and (when the key is bound) the machine code.
function verifyKey(key, { now = Date.now(), machine = null, jwk = null } = {}) {
  const parts = String(key || '').trim().replace(/\s+/g, '').split('.');
  if (parts.length !== 3 || parts[0] !== PREFIX) return { ok: false, reason: 'malformed', message: 'This is not an Audio Mixer license key.' };
  let payload;
  try {
    const keys = jwk ? [jwk] : [PUBLIC_JWK, sandboxJwk()].filter(Boolean);                       // the vendor key; in the sandbox the sandbox key too
    if (!keys.some(k => crypto.verify('sha256', Buffer.from(parts[1]), { key: publicKey(k), dsaEncoding: 'ieee-p1363' }, unb64u(parts[2])))) throw new Error('bad');
    payload = JSON.parse(unb64u(parts[1]).toString('utf8'));
  } catch (_) { return { ok: false, reason: 'bad-signature', message: 'The license key is not valid (signature check failed).' }; }
  if (!payload || payload.v !== 1 || !PLANS[payload.plan]) return { ok: false, reason: 'unknown-plan', message: 'The license key is for an unknown plan.' };
  if (payload.issued && payload.issued > now + 86400000) return { ok: false, reason: 'not-yet-valid', message: 'The license key is not valid yet (check the date of this computer).' };
  if (payload.expires && payload.expires < now) return { ok: false, reason: 'expired', message: `The license key expired on ${new Date(payload.expires).toISOString().slice(0, 10)}.`, payload };
  if (payload.mid && machine && payload.mid.replace(/-/g, '') !== machine.replace(/-/g, '')) return { ok: false, reason: 'wrong-machine', message: 'This license key was issued for a different computer.', payload };
  return { ok: true, payload, plan: PLANS[payload.plan] };
}

// A stable code for this computer (shown to the customer so a key can be bound to it). Not secret, not personal: a hash.
let cachedMachine = null;
function machineId() {
  if (cachedMachine) return cachedMachine;
  const parts = [os.platform(), os.arch()];
  try {
    if (os.platform() === 'linux') parts.push(fs.readFileSync('/etc/machine-id', 'utf8').trim());
    else if (os.platform() === 'win32') parts.push((/MachineGuid\s+REG_SZ\s+(\S+)/i.exec(execFileSync('reg', ['query', 'HKLM\\SOFTWARE\\Microsoft\\Cryptography', '/v', 'MachineGuid'], { encoding: 'utf8', timeout: 4000, windowsHide: true })) || [])[1]);
    else if (os.platform() === 'darwin') parts.push((/"IOPlatformUUID" = "([^"]+)"/.exec(execFileSync('ioreg', ['-rd1', '-c', 'IOPlatformExpertDevice'], { encoding: 'utf8', timeout: 4000 })) || [])[1]);
  } catch (_) { /* fall back to the host name below */ }
  if (parts.length < 3 || !parts[2]) parts.push(os.hostname());
  const h = crypto.createHash('sha256').update('audio-mixer:' + parts.join('|')).digest('hex').toUpperCase().slice(0, 20);
  cachedMachine = h.match(/.{4}/g).join('-');
  return cachedMachine;
}

const storeFile = () => process.env.BRIDGE_LICENSE_FILE || path.join(os.homedir(), '.audio-mixer', 'license.json');

function readStored() {
  try { const j = JSON.parse(fs.readFileSync(storeFile(), 'utf8')); return j && typeof j.key === 'string' ? j : null; } catch (_) { return null; }
}

// What the app should run as right now.
function status(opts = {}) {
  const machine = opts.machine || machineId();
  const stored = opts.key !== undefined ? { key: opts.key } : readStored();
  const base = { machineId: machine, plans: PLANS, sandbox: !!sandboxJwk() };
  if (!stored || !stored.key) return { ...base, state: 'basic', plan: PLANS.basic, license: null };
  const v = verifyKey(stored.key, { now: opts.now, machine, jwk: opts.jwk });
  if (v.ok) {
    const p = v.payload;
    return { ...base, state: 'active', plan: v.plan, license: { id: p.id || null, plan: p.plan, name: p.name || '', email: p.email || '', issued: p.issued || null, expires: p.expires || null, boundToThisPc: !!p.mid, seats: p.seats || 1 } };
  }
  // an expired / invalid stored key never raises the plan
  return { ...base, state: v.reason === 'expired' ? 'expired' : 'invalid', plan: PLANS.basic, license: v.payload ? { id: v.payload.id || null, plan: v.payload.plan, name: v.payload.name || '', expires: v.payload.expires || null } : null, message: v.message };
}

// What the customer sends to the vendor to get a key: the machine code, the plan wanted and who it is for. Nothing secret.
function request({ plan = 'pro', name = '', email = '', now = Date.now(), machine = machineId() } = {}) {
  if (!PLANS[plan]) { const e = new Error('plan must be one of: ' + Object.keys(PLANS).join(', ')); e.status = 400; throw e; }
  const clean = (v, n) => String(v || '').replace(/[\u0000-\u001f<>]/g, '').trim().slice(0, n);
  let version = '1.0.0'; try { version = require('../package.json').version; } catch (_) { /* packaged without it */ }
  return { app: 'audio-mixer', kind: 'license-request', v: 1, machineId: machine, plan, name: clean(name, 80), email: clean(email, 120), platform: os.platform(), arch: os.arch(), version, at: now, sandbox: !!sandboxJwk() };
}

function activate(key, opts = {}) {
  const v = verifyKey(key, { machine: opts.machine || machineId(), now: opts.now, jwk: opts.jwk });
  if (!v.ok) return { ok: false, reason: v.reason, message: v.message };
  const file = opts.file || storeFile();
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, JSON.stringify({ key: String(key).trim().replace(/\s+/g, ''), activatedAt: Date.now() }) + '\n', { mode: 0o600 });
  return { ok: true, ...status({ key: String(key).trim() , machine: opts.machine, now: opts.now, jwk: opts.jwk }) };
}

function deactivate(opts = {}) {
  const file = opts.file || storeFile();
  let removed = false;
  try { fs.unlinkSync(file); removed = true; } catch (_) { /* nothing stored */ }
  return { ok: true, removed, ...status({ key: null }) };
}

const hasFeature = (st, f) => !!(st && st.plan && st.plan.features.includes(f));

module.exports = { PLANS, PREFIX, PUBLIC_JWK, sandboxJwk, request, publicKeyOf: publicKey, signKey, verifyKey, machineId, status, activate, deactivate, hasFeature, readStored, storeFile, b64u, unb64u };
