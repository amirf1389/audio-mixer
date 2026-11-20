#!/usr/bin/env node
'use strict';
// THE PRESET PACK: the owner's factory presets (MASTERING, the effect presets, the channel chains and EQ curves) as one file,
// releases/presets/presets-pack.json, for the apps that cannot read the console's code: the phone app bundles it, the network service
// serves it to the browser remote, the licence portal publishes newer packs.
//
// ONE SOURCE OF TRUTH: the values come from the console's code itself. `amixos --dump-presets <file>` writes them (Engine::presetPack);
// this script adds the version and the date and writes the pack. The console's --smoke fails when the pack and the code differ.
//   node scripts/make-presets.js [--bin <amixos>] [--version <v>] [--out <file>]
//   node scripts/make-presets.js --check [--bin <amixos>]     exit 2 when the pack in releases/presets/ is not what the code has
// The pack is not signed here: its sha256 goes into releases/update.pending.json (node scripts/make-update.js --prepare) and an app
// trusts a downloaded pack only once the owner has signed a manifest with that sha256.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { execFileSync } = require('node:child_process');

const ROOT = path.join(__dirname, '..');
const PACK = path.join(ROOT, 'releases', 'presets', 'presets-pack.json');
const arg = (a, n, d) => { const i = a.indexOf(n); return i >= 0 && i + 1 < a.length ? a[i + 1] : d; };

// the three lists the code has, in a fixed order (the order the apps show them)
const BODY_KEYS = ['mastering', 'effects', 'tone'];

function findBinary(a) {
  const c = [arg(a, '--bin'), process.env.AMIXOS_BIN, '/tmp/amixbuild/amixos', path.join(ROOT, 'build', 'amixos'), path.join(ROOT, 'native', 'amixos', 'build', 'amixos')].filter(Boolean);
  for (const f of c) if (fs.existsSync(f)) return f;
  throw new Error('amixos was not found (build the console, or pass --bin <path to amixos>)');
}

// what the code has, as `amixos --dump-presets` writes it
function dump(bin) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'amix-presets-'));
  const out = path.join(dir, 'pack.json');
  try {
    const home = path.join(dir, 'home');   // a throw-away profile: the dump never touches the user's console settings
    execFileSync(bin, ['--dump-presets', out], { stdio: ['ignore', 'ignore', 'inherit'], timeout: 120000,
      env: Object.assign({}, process.env, { HOME: home, XDG_CONFIG_HOME: path.join(home, '.config'), AMIX_DATA_DIR: path.join(home, 'data'), AMIX_CACHE_DIR: path.join(home, 'cache'), QT_QPA_PLATFORM: 'offscreen' }) });
    return JSON.parse(fs.readFileSync(out, 'utf8'));
  } finally { fs.rmSync(dir, { recursive: true, force: true }); }
}

function body(p) { const o = {}; for (const k of BODY_KEYS) o[k] = p[k]; return o; }
function sameBody(a, b) { return JSON.stringify(body(a)) === JSON.stringify(body(b)); }
function sha256(buf) { return crypto.createHash('sha256').update(buf).digest('hex'); }

// a pack's shape (also used by the licence server before it publishes one)
function validate(p) {
  if (!p || typeof p !== 'object') throw new Error('the pack is not a JSON object');
  if (typeof p.version !== 'string' || !/^[0-9A-Za-z.+-]{1,32}$/.test(p.version)) throw new Error('version: a short text such as 2026.11.20');
  for (const k of BODY_KEYS) if (!Array.isArray(p[k]) || !p[k].length) throw new Error(k + ': a non-empty list');
  for (const m of p.mastering) if (typeof m.name !== 'string' || !m.params || typeof m.params !== 'object') throw new Error('mastering: {name, params}');
  for (const e of p.effects) if (typeof e.kind !== 'string' || typeof e.name !== 'string' || !Number.isInteger(e.id) || !e.values || typeof e.values !== 'object') throw new Error('effects: {kind, id, name, values}');
  for (const t of p.tone) if (typeof t.kind !== 'string' || typeof t.name !== 'string') throw new Error('tone: {kind, name}');
  return p;
}

function today() { const d = new Date(); return `${d.getUTCFullYear()}.${d.getUTCMonth() + 1}.${d.getUTCDate()}`; }

if (require.main === module) {
  const a = process.argv.slice(2);
  try {
    const code = dump(findBinary(a));
    let old = null; try { old = JSON.parse(fs.readFileSync(PACK, 'utf8')); } catch (_) {}
    if (a.includes('--check')) {
      const ok = old && sameBody(old, code);
      console.log(ok ? `releases/presets/presets-pack.json ${old.version} matches the code` : 'releases/presets/presets-pack.json differs from the code: run node scripts/make-presets.js');
      process.exit(ok ? 0 : 2);
    }
    // the version stays while nothing changed (so the signed sha256 stays valid); a change takes today's date unless --version is given
    const unchanged = old && sameBody(old, code) && old.version;
    const pack = Object.assign({ version: arg(a, '--version', unchanged ? old.version : today()), generated: unchanged && old.generated ? old.generated : new Date().toISOString().slice(0, 10) }, body(code));
    validate(pack);
    const out = arg(a, '--out', PACK); fs.mkdirSync(path.dirname(out), { recursive: true });
    const text = JSON.stringify(pack, null, 1) + '\n'; fs.writeFileSync(out, text);
    console.log(`${path.relative(ROOT, out)}: version ${pack.version}, ${pack.mastering.length} mastering, ${pack.effects.length} effect, ${pack.tone.length} tone presets, sha256 ${sha256(Buffer.from(text))}`);
    console.log('Next: node scripts/make-update.js --prepare puts this sha256 into releases/update.pending.json; apps trust the pack once the owner signs it.');
  } catch (e) { console.error('Error: ' + e.message); process.exit(1); }
}

module.exports = { PACK, BODY_KEYS, validate, sameBody, sha256, body };
