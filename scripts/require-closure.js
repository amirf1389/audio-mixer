'use strict';
// Prints the files a Node entry point loads through relative require() calls (one per line, relative to the repo root).
// Used by scripts/build-console-windows.sh to check that web\bridge in the Windows package has every file the bridge needs.
const fs = require('node:fs'), path = require('node:path');
const root = path.resolve(__dirname, '..'); const seen = new Set();
function walk(f) {
  if (seen.has(f)) return; seen.add(f);
  for (const m of fs.readFileSync(f, 'utf8').matchAll(/require\('(\.[^']+)'\)/g)) {
    const p = path.resolve(path.dirname(f), m[1]);
    for (const c of [p, p + '.js', path.join(p, 'index.js')]) if (fs.existsSync(c) && fs.statSync(c).isFile()) { if (c.endsWith('.js')) walk(c); else seen.add(c); break; }
  }
}
walk(path.resolve(process.argv[2] || path.join(root, 'bridge', 'server.js')));
process.stdout.write([...seen].map(f => path.relative(root, f).split(path.sep).join('/')).join('\n') + '\n');
