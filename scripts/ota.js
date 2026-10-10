#!/usr/bin/env node
'use strict';
// Vendor tool for the Audio Mixer OTA server (ota-server/server.js): builds the manifest of the current version, signs it with the vendor key
// ON THIS MACHINE (the key never goes to the server), uploads the release files that the server does not have yet, then publishes the manifest.
//   node scripts/ota.js publish --server https://ota.example.com --token T [--channel stable] [--notes "a|b|c"] [--dir <vendor key dir>] [--releases releases] [--force]
//   node scripts/ota.js status  --server https://ota.example.com --token T          files and versions held by the server, downloads per day
//   node scripts/ota.js sign    --base https://ota.example.com/releases [--out file] writes the signed manifest only (no upload)
// The token can also be given as OTA_ADMIN_TOKEN, the server as OTA_SERVER. Plain http is only accepted for localhost (testing).
// The apps are pointed at the server with  BRIDGE_UPDATE_URL=https://ota.example.com/update.json  BRIDGE_UPDATE_HOSTS=ota.example.com
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { signManifest } = require('../bridge/update');
const { loadPrivate, vendorDir } = require('./license');
const { build } = require('./make-update');

const ROOT = path.resolve(__dirname, '..');
const arg = (a, n, d) => { const i = a.indexOf(n); return i >= 0 ? a[i + 1] : d; };
const trim = s => String(s || '').replace(/\/+$/, '');

function server(a) {
  const s = trim(arg(a, '--server', process.env.OTA_SERVER));
  if (!s) throw new Error('--server https://... is required');
  if (!/^https:\/\//.test(s) && !/^http:\/\/(localhost|127\.0\.0\.1)(:|\/|$)/.test(s)) throw new Error('the server must be https (http is only accepted for localhost)');
  return s;
}
const authHeader = a => { const t = arg(a, '--token', process.env.OTA_ADMIN_TOKEN); if (!t) throw new Error('--token (or OTA_ADMIN_TOKEN) is required'); return { Authorization: 'Bearer ' + t }; };
async function call(url, opts) {
  const r = await fetch(url, opts), j = await r.json().catch(() => ({}));
  if (!r.ok || j.ok === false) throw new Error((j.error || 'HTTP ' + r.status) + (j.problems ? '\n  - ' + j.problems.join('\n  - ') : ''));
  return j;
}
const sha = f => new Promise((res, rej) => { const h = crypto.createHash('sha256'); fs.createReadStream(f).on('data', c => h.update(c)).on('end', () => res(h.digest('hex'))).on('error', rej); });

// manifest for the version in package.json, files from the releases folder, URLs on the server
function manifestFor(a, base) {
  const version = JSON.parse(fs.readFileSync(path.join(ROOT, 'package.json'), 'utf8')).version, channel = arg(a, '--channel', 'stable');
  const m = build({ releases: path.resolve(arg(a, '--releases', path.join(ROOT, 'releases'))), version, base, notes: String(arg(a, '--notes', '')).split('|').filter(Boolean) });
  m.channel = channel;
  return { manifest: m, channel };
}

async function main(a) {
  const cmd = a[0], rest = a.slice(1);
  if (cmd === 'sign') {
    const { manifest } = manifestFor(rest, trim(arg(rest, '--base', '')) || (() => { throw new Error('--base https://.../releases is required'); })());
    const env = signManifest(manifest, loadPrivate(vendorDir(rest))), out = arg(rest, '--out', '');
    const text = JSON.stringify(env, null, 2) + '\n'; if (out) fs.writeFileSync(out, text); else process.stdout.write(text);
    console.error(`signed: version ${manifest.version}, ${Object.keys(manifest.files).length} files`);
    return 0;
  }
  const s = server(rest), auth = authHeader(rest);
  if (cmd === 'status') {
    const f = await call(s + '/admin/files', { headers: auth }), st = await call(s + '/admin/stats', { headers: auth }), h = await call(s + '/healthz');
    console.log('channels: ' + (Object.entries(h.channels).map(([c, v]) => c + ' ' + v).join(', ') || 'none published'));
    for (const x of f.files) console.log(`  ${x.name}  ${(x.size / 1048576).toFixed(1)} MB  ${x.sha256.slice(0, 12)}…  ${x.referencedBy.length ? 'listed by ' + x.referencedBy.join(', ') : 'not listed'}`);
    for (const [d, v] of Object.entries(st.days).slice(-7)) console.log(`  ${d}: ${Object.values(v.manifest).reduce((x, y) => x + y, 0)} update checks, ${Object.values(v.download).reduce((x, y) => x + y, 0)} downloads`);
    return 0;
  }
  if (cmd === 'publish') {
    const { manifest, channel } = manifestFor(rest, s + '/releases');
    const env = signManifest(manifest, loadPrivate(vendorDir(rest)));
    const have = Object.fromEntries((await call(s + '/admin/files', { headers: auth })).files.map(x => [x.name, x.sha256]));
    for (const f of Object.values(manifest.files)) {
      if (have[f.name] === f.sha256) { console.log('  have   ' + f.name); continue; }
      const file = path.join(path.resolve(arg(rest, '--releases', path.join(ROOT, 'releases'))), f.name);
      process.stdout.write(`  upload ${f.name} (${(f.size / 1048576).toFixed(1)} MB) ... `);
      await call(s + '/admin/files/' + encodeURIComponent(f.name) + (have[f.name] ? '?overwrite=1' : ''), { method: 'PUT', headers: { ...auth, 'X-SHA256': await sha(file), 'Content-Type': 'application/octet-stream', 'Content-Length': String(f.size) }, body: fs.createReadStream(file), duplex: 'half' });
      console.log('ok');
    }
    const r = await call(s + '/admin/manifest/' + channel + (rest.includes('--force') ? '?force=1' : ''), { method: 'PUT', headers: { ...auth, 'Content-Type': 'application/json' }, body: JSON.stringify(env) });
    console.log(r.unchanged ? 'already published' : `published ${channel} ${r.version}` + (r.previous ? ` (was ${r.previous})` : ''));
    console.log(`apps use it with:  BRIDGE_UPDATE_URL=${s}${channel === 'stable' ? '' : '/' + channel}/update.json  BRIDGE_UPDATE_HOSTS=${new URL(s).hostname}`);
    return 0;
  }
  console.error('Usage: node scripts/ota.js publish | status | sign (see the header of this file)');
  return 2;
}

if (require.main === module) main(process.argv.slice(2)).then(c => process.exit(c), e => { console.error('Error: ' + e.message); process.exit(1); });
module.exports = { manifestFor, main };
