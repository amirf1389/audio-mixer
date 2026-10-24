#!/usr/bin/env node
'use strict';
// Installs FlexASIO (an ASIO driver for any sound card) silently, Windows only:  node scripts/install-flexasio.js [--url <github release .exe>] [--sha256 <hex>] [--dry-run]
// The installer comes from the pin in drivers/flexasio.json (or --url / --sha256) and is only run when its SHA-256 matches; see bridge/flexasio.js.
const fx = require('../bridge/flexasio');
const a = process.argv.slice(2), arg = (n, d) => { const i = a.indexOf(n); return i >= 0 ? a[i + 1] : d; };
(async () => {
  const pin = { ...fx.loadPin(), ...(arg('--url') ? { url: arg('--url') } : {}), ...(arg('--sha256') ? { sha256: arg('--sha256') } : {}) };
  const r = await fx.install({ pin, dryRun: a.includes('--dry-run') });
  console.log(r.note + '\n  file: ' + r.file + '\n  SHA-256: ' + r.sha256);
})().catch(e => { console.error('Error: ' + e.message); process.exit(1); });
