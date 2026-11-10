#!/usr/bin/env node
'use strict';
// Signs the Windows programs with YOUR code-signing certificate (Authenticode, SHA-256, RFC 3161 timestamp). Nothing here makes or ships a
// certificate: the PFX and its password come from the environment, stay on the machine that runs this, and are never written anywhere.
//   AMFX1_SIGN_PFX=/path/to/your.pfx AMFX1_SIGN_PASS=... node scripts/sign-windows.js "dist/windows/AmF X1.exe" dist/windows/AudioMixerASIO64.dll ...
// On Linux / macOS it uses osslsigncode (apt install osslsigncode); on Windows it uses signtool.exe from the Windows SDK. --verify only checks.
// An EV certificate gives SmartScreen reputation at once; an OV certificate earns it over downloads. See docs/windows-defender.md.
const fs = require('node:fs');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const has = n => spawnSync(process.platform === 'win32' ? 'where' : 'which', [n]).status === 0;
const args = process.argv.slice(2), verify = args.includes('--verify'), files = args.filter(a => !a.startsWith('--'));
if (!files.length) { console.error('usage: node scripts/sign-windows.js [--verify] <file.exe|file.dll> ...'); process.exit(2); }
const ts = process.env.AMFX1_TIMESTAMP_URL || 'http://timestamp.digicert.com', pfx = process.env.AMFX1_SIGN_PFX, pass = process.env.AMFX1_SIGN_PASS || '';
let failed = 0;
for (const f of files) {
  if (!fs.existsSync(f)) { console.error(`${f}: not found`); failed++; continue; }
  let r;
  if (verify) {
    r = has('osslsigncode') ? spawnSync('osslsigncode', ['verify', f], { encoding: 'utf8' }) : spawnSync('signtool', ['verify', '/pa', '/v', f], { encoding: 'utf8' });
  } else {
    if (!pfx) { console.error('set AMFX1_SIGN_PFX (your .pfx) and AMFX1_SIGN_PASS; no certificate is created by this script'); process.exit(2); }
    if (process.platform === 'win32' && has('signtool')) r = spawnSync('signtool', ['sign', '/fd', 'SHA256', '/td', 'SHA256', '/tr', ts, '/f', pfx, '/p', pass, '/d', 'AmF X1 by AMFB Audio', f], { encoding: 'utf8' });
    else if (has('osslsigncode')) { const tmp = f + '.signed'; r = spawnSync('osslsigncode', ['sign', '-pkcs12', pfx, '-pass', pass, '-n', 'AmF X1 by AMFB Audio', '-i', 'https://github.com/amirf1389/audio-mixer', '-h', 'sha256', '-ts', ts, '-in', f, '-out', tmp], { encoding: 'utf8' }); if (r.status === 0) fs.renameSync(tmp, f); }
    else { console.error('neither signtool (Windows SDK) nor osslsigncode is installed'); process.exit(2); }
  }
  const ok = r.status === 0; if (!ok) failed++;
  console.log(`${path.basename(f)}: ${verify ? (ok ? 'signature valid' : 'NOT signed / invalid') : (ok ? 'signed' : 'FAILED')}${ok ? '' : '\n' + (r.stderr || r.stdout || '').trim().slice(-600)}`);
}
process.exit(failed ? 1 : 0);
