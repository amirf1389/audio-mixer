#!/usr/bin/env node
'use strict';
// Builds the Windows installers of a PAID EDITION (AmF X1 Pro / AmF X1 Studio Master) for one sale:
//   npm run build:edition -- --token <edition-token file or AMFXE1.… text> [--no-sign] [--out dist/editions]
// The token comes from the Key Generator (BUILD EDITION TOKEN, signed with the vendor key there; the private key never comes here). This script
// REFUSES to build unless the token verifies with the vendor public key this project ships (bridge/license-public.json): a missing, changed,
// unsigned or wrongly signed token stops the build with exit code 1, so no edition installer can exist without a genuine token.
// Output (in --out, default dist/editions/<serial>): "AmF X1 Pro-<version>.exe", AudioMixer-Pro-<version>-x64.msi / -x86.msi (Studio Master: "StudioMaster").
// The installed app verifies the token again on first start and activates offline, bound to that computer (bridge/edition.js).
const fs = require('node:fs');
const path = require('node:path');
const edition = require('../bridge/edition');

const FILE_NAMES = { pro: 'Pro', 'studio-master': 'StudioMaster' };

// the token text from a file ({"token": "AMFXE1…"} as the Key Generator saves it, or the bare token) or from the argument itself
function readToken(arg) {
  const a = String(arg || '').trim();
  if (!a) return '';
  if (a.startsWith(edition.PREFIX + '.')) return a;
  let t; try { t = fs.readFileSync(a, 'utf8').trim(); } catch (e) { throw new Error('cannot read the token file ' + a + ': ' + e.code); }
  try { const j = JSON.parse(t); return String((j && j.token) || '').trim(); } catch (_) { return t; }
}

// the gate: returns the edition to build, or throws with the reason. jwk: only for tests (the CLI always uses the shipped vendor key)
function checkToken(token, { jwk = null, now = Date.now() } = {}) {
  if (!token) throw new Error('no edition token: make one in the Key Generator (BUILD EDITION TOKEN) and pass it with --token');
  const v = edition.verifyToken(token, { jwk: jwk || require('../bridge/license').PUBLIC_JWK, now });
  if (!v.ok) throw new Error('the edition token is not valid: ' + v.message + ' (' + v.reason + ')');
  return { id: v.edition.id, name: v.edition.name, file: FILE_NAMES[v.edition.id], plan: v.plan, serial: v.payload.serial, token, expires: v.payload.expires || null };
}

async function main(argv) {
  const arg = (n, d) => { const i = argv.indexOf(n); return i >= 0 ? argv[i + 1] : d; };
  let ed;
  try { ed = checkToken(readToken(arg('--token'))); } catch (e) { console.error('Edition build refused: ' + e.message); return 1; }
  const out = path.resolve(arg('--out', path.join(__dirname, '..', 'dist', 'editions', ed.serial)));
  console.log(`Building ${ed.name} (plan ${ed.plan.toUpperCase()}, serial ${ed.serial}${ed.expires ? ', expires ' + new Date(ed.expires).toISOString().slice(0, 10) : ''}) into ${out}`);
  const { buildInstallers } = require('./build-installers');
  const r = await buildInstallers({ out, sign: !argv.includes('--no-sign'), edition: ed });
  for (const [a, m] of Object.entries(r.msi)) console.log(`MSI ${a}: ${m.msi}\n     SHA-256 ${m.sha256}`);
  if (r.exe) console.log(`EXE: ${r.exe.exe}\n     SHA-256 ${r.exe.sha256}`);
  console.log('Send these files to the buyer only: they carry this sale\'s serial.');
  return 0;
}

if (require.main === module) main(process.argv.slice(2)).then(c => { process.exitCode = c; }, e => { console.error('Edition build failed: ' + e.message); process.exitCode = 1; });
module.exports = { readToken, checkToken, main, FILE_NAMES };
