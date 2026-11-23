#!/usr/bin/env node
'use strict';
// Runs the whole portal WITHOUT Docker: the licence server (licence keys, customer portal, staff dashboard) and the OTA update server, as two
// child processes of this one, with the settings of deploy/portal/.env. Caddy (deploy/portal/Caddyfile) goes in front for HTTPS.
//   node deploy/portal/start.js [--env <file>]        (npm start in deploy/portal does the same)
// It exits when either server exits, so systemd (amfx1-portal.service) or Docker restarts the pair. No packages: plain Node.js 22+.
const fs = require('node:fs');
const path = require('node:path');
const { spawn } = require('node:child_process');

const HERE = __dirname, ROOT = path.resolve(HERE, '..', '..');

// KEY=value lines; # comments; optional quotes. Values are never printed.
function readEnv(file) {
  const out = {};
  for (const raw of fs.readFileSync(file, 'utf8').split(/\r?\n/)) {
    const line = raw.trim(); if (!line || line.startsWith('#')) continue;
    const m = /^([A-Z][A-Z0-9_]*)\s*=\s*(.*)$/.exec(line); if (!m) continue;
    let v = m[2].trim(); if (/^(["']).*\1$/.test(v)) v = v.slice(1, -1);
    out[m[1]] = v;
  }
  return out;
}

// The environment of each server, from the .env values. Relative paths are taken from the folder the .env file is in.
function plan(env, base) {
  const abs = p => (p ? path.resolve(base, p) : '');
  const data = abs(env.DATA_DIR || 'data'), host = env.BIND_HOST || '127.0.0.1', proxy = env.TRUST_PROXY === '0' ? '0' : '1';
  const need = ['LICENSE_ADMIN_TOKEN', 'OTA_ADMIN_TOKEN', 'PORTAL_JWT_SECRET', 'PORTAL_CRM_SECRET'];
  const missing = need.filter(k => !env[k] || env[k].length < 32);
  if (missing.length) throw new Error('these must be set to at least 32 characters in .env (run init.sh / init.ps1): ' + missing.join(', '));
  const shared = { PATH: process.env.PATH || '', SYSTEMROOT: process.env.SYSTEMROOT || '', TMPDIR: process.env.TMPDIR || '', TEMP: process.env.TEMP || '', TMP: process.env.TMP || '',
    NODE_ENV: 'production', PORTAL_DB: path.join(data, 'staff', 'portal.db'), PORTAL_JWT_SECRET: env.PORTAL_JWT_SECRET };
  const publicUrl = env.OTA_PUBLIC_URL || (env.PUBLIC_DOMAIN ? 'https://' + env.PUBLIC_DOMAIN : '');
  const licence = { ...shared, LICENSE_HOST: host, LICENSE_PORT: env.LICENSE_PORT || '8787', LICENSE_ADMIN_TOKEN: env.LICENSE_ADMIN_TOKEN,
    LICENSE_PRIVATE_KEY_FILE: abs(env.LICENSE_PRIVATE_KEY_FILE || 'secrets/vendor-private.pem'), LICENSE_DATA_KEY_FILE: abs(env.LICENSE_DATA_KEY_FILE || 'secrets/licence-data.key'),
    LICENSE_DATA_FILE: path.join(data, 'licence', 'licenses.json'), LICENSE_RELEASES_DIR: abs(env.LICENSE_RELEASES_DIR || 'site-releases'), LICENSE_TRUST_PROXY: proxy,
    PORTAL_CRM_SECRET: env.PORTAL_CRM_SECRET, LICENSE_UPDATE_URL: env.LICENSE_UPDATE_URL || (publicUrl ? publicUrl + '/update.json' : '') };
  const ota = { ...shared, OTA_HOST: host, OTA_PORT: env.OTA_PORT || '8780', OTA_ADMIN_TOKEN: env.OTA_ADMIN_TOKEN, OTA_DATA: path.join(data, 'ota'), OTA_PUBLIC_URL: publicUrl,
    OTA_TRUST_PROXY: proxy, OTA_RATE: env.OTA_RATE || '120', OTA_ALLOW_HTTP: env.OTA_ALLOW_HTTP === '1' ? '1' : '', OTA_PUBLIC_KEY_FILE: abs(env.OTA_PUBLIC_KEY_FILE || '') };
  for (const k of ['SMTP_HOST', 'SMTP_PORT', 'SMTP_SECURE', 'SMTP_USER', 'SMTP_PASS', 'SMTP_FROM']) if (env[k]) ota[k] = env[k];
  for (const o of [licence, ota]) for (const k of Object.keys(o)) if (o[k] === '') delete o[k];
  return { data, licence, ota };
}

// The staff hosts (Caddyfile, `import staff`) answer only ADMIN_ALLOW_IPS. Unset or "everyone" keeps the old password-only behaviour, so say so loudly.
function staffAllowWarning(env) {
  const v = (env.ADMIN_ALLOW_IPS || '').trim();
  if (v && !/(^|\s)(0\.0\.0\.0\/0|::\/0)(\s|$)/.test(v)) return '';
  return 'WARNING: ADMIN_ALLOW_IPS is not set (or allows every address): the staff hosts are protected by the password only. Set it in .env to your own addresses, e.g. ADMIN_ALLOW_IPS=203.0.113.7/32';
}

function main(argv = process.argv.slice(2)) {
  const i = argv.indexOf('--env'), envFile = path.resolve(i >= 0 ? argv[i + 1] : process.env.PORTAL_ENV_FILE || path.join(HERE, '.env'));
  if (!fs.existsSync(envFile)) { console.error('No ' + envFile + ': run deploy/portal/init.sh (or init.ps1) first.'); return process.exit(1); }
  let env, p; try { env = readEnv(envFile); p = plan(env, path.dirname(envFile)); } catch (e) { console.error(e.message); return process.exit(1); }
  const warn = staffAllowWarning(env); if (warn) console.warn(warn);
  for (const d of ['staff', 'licence', 'ota']) fs.mkdirSync(path.join(p.data, d), { recursive: true, mode: 0o700 });
  const kids = [];
  const run = (name, script, env) => {
    const ch = spawn(process.execPath, [path.join(ROOT, script)], { env, stdio: ['ignore', 'pipe', 'pipe'], cwd: ROOT });
    for (const s of [ch.stdout, ch.stderr]) s.on('data', d => { for (const l of String(d).split('\n')) if (l) (s === ch.stderr ? process.stderr : process.stdout).write(`[${name}] ${l}\n`); });
    ch.on('exit', code => { console.error(`[${name}] stopped (${code})`); for (const k of kids) if (k !== ch && k.exitCode === null) k.kill('SIGTERM'); process.exitCode = code || 1; });
    kids.push(ch);
  };
  run('licence', 'bridge/license-server/server.js', p.licence);
  run('ota', 'ota-server/server.js', p.ota);
  const stop = () => { for (const k of kids) if (k.exitCode === null) k.kill('SIGTERM'); };
  process.on('SIGTERM', stop); process.on('SIGINT', stop);
}

module.exports = { staffAllowWarning, readEnv, plan };
if (require.main === module) main();
