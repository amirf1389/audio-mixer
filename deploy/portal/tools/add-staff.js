#!/usr/bin/env node
'use strict';
// Creates a staff account (dashboard sign-in) with the admin token. The password is asked for, never taken from the command line
// (it would land in the shell history and the process list). At least 12 characters.
//   docker compose exec licence node deploy/portal/tools/add-staff.js you@example.com "Your Name"
//   (no Docker: set LICENSE_ADMIN_TOKEN / LICENSE_PORT from .env, e.g. node deploy/portal/tools/add-staff.js --env deploy/portal/.env you@example.com "Name")
const readline = require('node:readline');
const path = require('node:path');
function ask(q, hidden) {
  return new Promise(res => {
    const rl = readline.createInterface({ input: process.stdin, output: process.stdout, terminal: !!process.stdin.isTTY });
    if (hidden && process.stdin.isTTY) rl._writeToOutput = s => { if (s.includes(q)) process.stdout.write(s); };
    rl.question(q, v => { rl.close(); if (hidden && process.stdin.isTTY) process.stdout.write('\n'); res(v); });
  });
}
(async () => {
  let a = process.argv.slice(2);
  const e = a.indexOf('--env'); if (e >= 0) { const env = require('../start').readEnv(path.resolve(a[e + 1])); for (const k of ['LICENSE_ADMIN_TOKEN', 'LICENSE_PORT']) if (env[k] && !process.env[k]) process.env[k] = env[k]; a = a.filter((_, j) => j !== e && j !== e + 1); }
  const [email, ...rest] = a, name = rest.join(' ');
  if (!email || !name) { console.error('usage: add-staff.js <e-mail> "<name>"'); process.exit(2); }
  const token = process.env.LICENSE_ADMIN_TOKEN, port = process.env.LICENSE_PORT || '8787';
  if (!token) { console.error('LICENSE_ADMIN_TOKEN is not set'); process.exit(1); }
  const password = process.env.STAFF_PASSWORD || await ask('Password (12+ characters): ', true);
  const r = await fetch(`http://127.0.0.1:${port}/admin/staff`, { method: 'POST', headers: { authorization: 'Bearer ' + token, 'content-type': 'application/json' }, body: JSON.stringify({ email, name, password }) });
  const j = await r.json().catch(() => ({}));
  if (!r.ok) { console.error('refused: ' + (j.error || 'HTTP ' + r.status)); process.exit(1); }
  console.log('staff account created: ' + email);
})().catch(e => { console.error('failed: ' + e.message); process.exit(1); });
