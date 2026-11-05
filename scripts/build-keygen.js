#!/usr/bin/env node
'use strict';
// "AmF X1 Key Generator.exe": the license key generator + local update server (scripts/license-generator.js) as ONE Windows program,
// so it needs no Node.js install and no project folder next to it. Double-click: the page opens in the browser, and the public
// listener for customers' key requests starts on 127.0.0.1:8780 (put nginx with HTTPS in front: deploy/nginx/keys-api.conf; add
// --public-bind 0.0.0.0 only to open port 8780 itself to all networks).
//   node scripts/build-keygen.js [--targets windows-x64,linux-x64] [--out dist]
// Built the same way as the single-file server (scripts/build-single.js): esbuild bundles the script, Node.js single-executable support injects
// it into the official Node.js binary (cached in dist/cache). The executable is not code-signed here: Windows shows "unknown publisher".
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const single = require('./build-single');

const ROOT = path.resolve(__dirname, '..');
const NAMES = { 'windows-x64': 'AmF X1 Key Generator.exe', 'windows-x86': 'AmF X1 Key Generator (x86).exe', 'linux-x64': 'amfx1-key-generator-linux-x64' };

function bundle(cache) {
  const { esbuild } = single.ensureTools(cache);
  const entry = path.join(os.tmpdir(), `am-keygen-entry-${process.pid}.js`);
  // double-click defaults: open the page, take requests from the internet on 8780; any argument given replaces nothing, it is added
  fs.writeFileSync(entry, `const a = process.argv.slice(2); if (!a.includes('--no-open')) a.push('--open'); if (!a.includes('--no-public') && !a.includes('--public')) a.push('--public');\n` +
    `const code = require(${JSON.stringify(path.join(ROOT, 'scripts', 'license-generator.js'))}).main(a.filter(x => x !== '--no-open' && x !== '--no-public')); if (code) process.exit(code);\n`);
  try {
    const out = esbuild.buildSync({ entryPoints: [entry], bundle: true, platform: 'node', target: 'node18', format: 'cjs', minify: true, legalComments: 'none', write: false,
      external: ['audify', 'naudiodon2'], logLevel: 'error', absWorkingDir: ROOT });
    return `'use strict';\n${Buffer.from(out.outputFiles[0].contents).toString('utf8')}`;
  } finally { fs.rmSync(entry, { force: true }); }
}

async function build({ targets = ['windows-x64'], out = path.join(ROOT, 'dist'), cache = path.join(ROOT, 'dist', 'cache') } = {}) {
  fs.mkdirSync(out, { recursive: true });
  const script = bundle(cache), made = [];
  for (const t of targets) {
    if (!NAMES[t]) throw new Error('unknown target ' + t + ' (' + Object.keys(NAMES).join(', ') + ')');
    const file = path.join(out, NAMES[t]);
    single.buildExecutable({ target: t, cache, outFile: file, script });
    made.push(file);
  }
  return made;
}

if (require.main === module) {
  const a = process.argv.slice(2), arg = (n, d) => { const i = a.indexOf(n); return i >= 0 ? a[i + 1] : d; };
  build({ targets: String(arg('--targets', 'windows-x64')).split(','), out: path.resolve(arg('--out', path.join(ROOT, 'dist'))) })
    .then(m => { m.forEach(f => console.log(`${f}  (${(fs.statSync(f).size / 1048576).toFixed(1)} MB)`)); })
    .catch(e => { console.error('Error: ' + e.message); process.exit(1); });
}
module.exports = { build, bundle, NAMES };
