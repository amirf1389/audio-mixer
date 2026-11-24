'use strict';
// The one script inside "AmF X1 Key Manager.exe" (a Node single executable application, built by tools/key-manager/build.js). A SEA main script
// can require only Node's built-in modules, so the generator and the modules it needs travel as SEA assets (with a manifest of SHA-256 hashes)
// and are written to <local app data>/AmF X1 Key Manager/app/<version>-<id>/ on start: every file is re-hashed on every start and rewritten
// when it differs, so a changed or damaged copy is never run. Then tools/key-manager/main.js runs from there with an ordinary require.
// No key material is ever an asset: the build refuses to embed a private key (build.js), and the key folder is only read at run time.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { createRequire } = require('node:module');

const sha = b => crypto.createHash('sha256').update(b).digest('hex');
function appBase(env = process.env, platform = process.platform, home = os.homedir()) {
  if (platform === 'win32') return path.join(env.LOCALAPPDATA || env.APPDATA || home, 'AmF X1 Key Manager');
  if (platform === 'darwin') return path.join(home, 'Library', 'Application Support', 'AmF X1 Key Manager');
  return path.join(env.XDG_CACHE_HOME || path.join(home, '.cache'), 'amf-x1-key-manager');
}
const safeRel = r => typeof r === 'string' && r.length < 200 && !path.isAbsolute(r) && !r.split(/[\\/]/).includes('..') && /^[A-Za-z0-9_./-]+$/.test(r);

// getAsset(name) -> ArrayBuffer (node:sea), or any function with that shape (the tests pass one)
function extract(getAsset, base) {
  const man = JSON.parse(Buffer.from(getAsset('manifest.json')).toString('utf8'));
  if (!/^[A-Za-z0-9.-]{1,40}$/.test(String(man.version)) || !/^[a-f0-9]{8,64}$/.test(String(man.id))) throw new Error('bad manifest');
  const dir = path.join(base, 'app', man.version + '-' + man.id);
  for (const [rel, want] of Object.entries(man.files || {})) {
    if (!safeRel(rel)) throw new Error('bad file name in the manifest: ' + rel);
    const f = path.join(dir, rel);
    let ok = false; try { ok = sha(fs.readFileSync(f)) === want; } catch (_) { /* not there yet */ }
    if (ok) continue;
    const data = Buffer.from(getAsset(rel));
    if (sha(data) !== want) throw new Error('embedded file does not match its hash: ' + rel);
    fs.mkdirSync(path.dirname(f), { recursive: true, mode: 0o700 });
    const tmp = f + '.' + process.pid + '.tmp'; fs.writeFileSync(tmp, data, { mode: 0o600 }); fs.renameSync(tmp, f);
  }
  return { dir, man };
}

module.exports = { extract, appBase, safeRel };

let sea = null; try { sea = require('node:sea'); } catch (_) { /* older Node: not a single executable */ }
if (sea && sea.isSea()) {
  const { dir, man } = extract(name => sea.getAsset(name), appBase());
  const main = createRequire(path.join(dir, 'package.json'))(path.join(dir, 'tools', 'key-manager', 'main.js'));
  main.cli(process.argv.slice(2).filter(a => a !== process.execPath), { sea: true, version: man.version, exeDir: path.dirname(process.execPath) });
}
