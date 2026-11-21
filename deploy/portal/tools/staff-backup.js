#!/usr/bin/env node
'use strict';
// Writes a consistent copy of the staff / CRM database (GET /admin/system/backup of the licence server) to stdout or to --out <file>.
// Uses LICENSE_ADMIN_TOKEN and LICENSE_PORT from the environment (inside the licence container they are already set). Nothing secret is printed.
//   docker compose exec -T licence node deploy/portal/tools/staff-backup.js > staff.db
const fs = require('node:fs');
(async () => {
  const token = process.env.LICENSE_ADMIN_TOKEN, port = process.env.LICENSE_PORT || '8787', a = process.argv.slice(2), i = a.indexOf('--out');
  if (!token) { console.error('LICENSE_ADMIN_TOKEN is not set'); process.exit(1); }
  const r = await fetch(`http://127.0.0.1:${port}/admin/system/backup`, { headers: { authorization: 'Bearer ' + token } });
  if (!r.ok) { console.error('backup refused: HTTP ' + r.status); process.exit(1); }
  const buf = Buffer.from(await r.arrayBuffer());
  if (i >= 0) fs.writeFileSync(a[i + 1], buf, { mode: 0o600 }); else process.stdout.write(buf);
})().catch(e => { console.error('backup failed: ' + e.message); process.exit(1); });
