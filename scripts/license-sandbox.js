#!/usr/bin/env node
'use strict';
// SANDBOX license issuer: for trying the key request -> issue -> activate flow in a development copy, with a key pair of its own that has nothing to do with the vendor key.
//   node scripts/license-sandbox.js init  [--dir D]                                   make the sandbox key pair (default ~/.audio-mixer-sandbox) and write bridge/license-sandbox-public.json
//   node scripts/license-sandbox.js issue --request request.json [--plan P] [--days N] [--dir D]    key for a request file made by the app (GET /api/license/request, LICENSE page)
//   node scripts/license-sandbox.js issue --plan pro --name "Test" [--machine XXXX-...] [--days N]   key without a request file
//   node scripts/license-sandbox.js show <key>
//   node scripts/license-sandbox.js dev [--plan P] [--name N] [--dir D]                 issue a 10-year key for this machine (STUDIO by default) and activate it
// Start the server with AUDIO_MIXER_SANDBOX=1 so it accepts the sandbox key. The public file is not committed and not packaged: installed copies never accept sandbox keys.
// The vendor private key is never read or created here (scripts/license.js is the vendor tool).
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const lic = require('../bridge/license');

const ROOT = path.resolve(__dirname, '..');
const arg = (a, n, d) => { const i = a.indexOf(n); return i >= 0 ? a[i + 1] : d; };
const sandboxDir = a => path.resolve(arg(a, '--dir', process.env.AUDIO_MIXER_SANDBOX_DIR || path.join(os.homedir(), '.audio-mixer-sandbox')));
const PUB_FILE = path.join(ROOT, 'bridge', 'license-sandbox-public.json');

function init(dir, pubFile = PUB_FILE) {
  fs.mkdirSync(dir, { recursive: true, mode: 0o700 });
  const priv = path.join(dir, 'sandbox-private.pem');
  if (!fs.existsSync(priv)) {
    const { privateKey } = crypto.generateKeyPairSync('ec', { namedCurve: 'P-256' });
    fs.writeFileSync(priv, privateKey.export({ type: 'pkcs8', format: 'pem' }), { mode: 0o600 });
  }
  const pub = crypto.createPublicKey(crypto.createPrivateKey(fs.readFileSync(priv))).export({ format: 'jwk' });
  fs.writeFileSync(pubFile, JSON.stringify({ kty: pub.kty, crv: pub.crv, x: pub.x, y: pub.y }) + '\n');
  return { priv, pubFile };
}

function issue({ dir, request, plan, name, email, days = 30, machine, now = Date.now() }) {
  const r = request || {};
  if (request && (r.kind !== 'license-request' || r.app !== 'audio-mixer')) throw new Error('this is not an Audio Mixer license request file');
  plan = plan || r.plan || 'pro';
  if (!lic.PLANS[plan]) throw new Error('plan must be one of: ' + Object.keys(lic.PLANS).join(', '));
  name = name || r.name || 'Sandbox';
  const payload = { v: 1, id: 'SBX-' + crypto.randomBytes(4).toString('hex').toUpperCase(), plan, name, issued: now, expires: now + Math.round(days * 86400000), seats: 1, sandbox: true };
  email = email || r.email; if (email) payload.email = email;
  machine = machine || r.machineId; if (machine) payload.mid = String(machine).toUpperCase();
  const key = lic.signKey(payload, crypto.createPrivateKey(fs.readFileSync(path.join(dir, 'sandbox-private.pem'))));
  return { key, payload };
}

function main(argv) {
  const cmd = argv[0], a = argv.slice(1);
  try {
    if (cmd === 'init') { const r = init(sandboxDir(a)); console.log(`Sandbox key pair in ${path.dirname(r.priv)}\nPublic key written to ${path.relative(ROOT, r.pubFile)} (not committed, not packaged).\nStart the server with AUDIO_MIXER_SANDBOX=1.`); }
    else if (cmd === 'issue') {
      const reqFile = arg(a, '--request');
      const request = reqFile ? JSON.parse(fs.readFileSync(reqFile, 'utf8')) : null;
      const r = issue({ dir: sandboxDir(a), request, plan: arg(a, '--plan'), name: arg(a, '--name'), email: arg(a, '--email'), days: Number(arg(a, '--days', 30)) || 30, machine: arg(a, '--machine') });
      console.log(r.key);
      console.error(`\nSANDBOX ${lic.PLANS[r.payload.plan].name} for ${r.payload.name}, expires ${new Date(r.payload.expires).toISOString().slice(0, 10)}${r.payload.mid ? ', bound to ' + r.payload.mid : ''}`);
    } else if (cmd === 'dev') {
      // Development copy only: makes the sandbox key pair if needed, issues a STUDIO key (every feature) bound to this machine for 10 years, and
      // activates it, so a server started with AUDIO_MIXER_SANDBOX=1 comes up licensed. Servers without that flag reject the key.
      const dir = sandboxDir(a);
      init(dir);
      const r = issue({ dir, plan: arg(a, '--plan', 'studio'), name: arg(a, '--name', 'Development'), days: 3650, machine: lic.machineId() });
      const res = lic.activate(r.key, { jwk: lic.sandboxJwk({ env: { AUDIO_MIXER_SANDBOX: '1' } }) });
      if (!res.ok) throw new Error(`activation failed: ${res.message || res.reason}`);
      console.log(`SANDBOX ${lic.PLANS[r.payload.plan].name} activated for ${r.payload.name} on ${r.payload.mid}, expires ${new Date(r.payload.expires).toISOString().slice(0, 10)}.\nStart the server with AUDIO_MIXER_SANDBOX=1 to use it.`);
    } else if (cmd === 'show') {
      const v = lic.verifyKey(a[0]);
      console.log(JSON.stringify(v.ok ? { ok: true, plan: v.plan.name, payload: v.payload } : { ok: false, reason: v.reason, message: v.message }, null, 2));
      return v.ok ? 0 : 1;
    } else { console.error('Usage: node scripts/license-sandbox.js init | issue | show (see the header of this file)'); return 2; }
    return 0;
  } catch (e) { console.error('Error: ' + e.message); return 1; }
}

if (require.main === module) process.exit(main(process.argv.slice(2)));
module.exports = { init, issue, sandboxDir, PUB_FILE };
