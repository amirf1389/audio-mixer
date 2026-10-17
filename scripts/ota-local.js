#!/usr/bin/env node
'use strict';
// Local, secure OTA demo/dev environment: runs a real ota-server (ota-server/server.js) on 127.0.0.1 behind a real,
// freshly generated TLS certificate, with its own vendor signing keypair (separate from the project's real one), then
// drives the real app-side update client (bridge/update.js) against it end to end:
//   1. publish a signed, checksummed manifest + release file (scripts/ota.js's own publish path, in-process)
//   2. the client accepts it, downloads the file and verifies its SHA-256
//   3. a file tampered on disk after publishing is rejected (checksum mismatch)
//   4. a manifest signed with a different key is rejected (signature check)
//   5. a manifest whose payload was altered after signing is rejected (signature check)
// Nothing here touches the project's real vendor key, releases/update.json, or bridge/license-public.json. Everything
// lives under .local-ota/ (gitignored) and is regenerated each run except the keypair and TLS cert, which are reused
// while valid so repeat runs are fast.
//   node scripts/ota-local.js
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const https = require('node:https');
const crypto = require('node:crypto');
const { execFileSync } = require('node:child_process');

const ROOT = path.resolve(__dirname, '..');
const LOCAL = path.join(ROOT, '.local-ota');
const VENDOR_DIR = path.join(LOCAL, 'vendor');
const OTHER_VENDOR_DIR = path.join(LOCAL, 'vendor-other');   // a second, unrelated keypair used only to prove a wrong-key signature is rejected
const TLS_DIR = path.join(LOCAL, 'tls');
const CERT = path.join(TLS_DIR, 'cert.pem');
const KEY = path.join(TLS_DIR, 'key.pem');

function ensureVendorKeys(dir) {
  const { initKeys, loadPrivate, loadPublic } = require('./license');
  if (!fs.existsSync(path.join(dir, 'private.pem'))) initKeys(dir);
  return { privateKey: loadPrivate(dir), jwk: loadPublic(dir) };
}

function certValid() {
  if (!fs.existsSync(CERT) || !fs.existsSync(KEY)) return false;
  try { execFileSync('openssl', ['x509', '-checkend', '3600', '-noout', '-in', CERT]); return true; } catch (_) { return false; }
}

function ensureTlsCert() {
  if (certValid()) return;
  fs.mkdirSync(TLS_DIR, { recursive: true, mode: 0o700 });
  execFileSync('openssl', ['ecparam', '-genkey', '-name', 'prime256v1', '-noout', '-out', KEY]);
  fs.chmodSync(KEY, 0o600);
  execFileSync('openssl', ['req', '-new', '-x509', '-key', KEY, '-out', CERT, '-days', '7', '-subj', '/CN=127.0.0.1', '-addext', 'subjectAltName=IP:127.0.0.1']);
}

// Phase 1 (plain node, no extra trusted CA yet): make sure the keys and certificate exist, then re-exec this same
// script with NODE_EXTRA_CA_CERTS pointed at our certificate - Node only reads that variable at process start, so a
// fresh process is the only way the later fetch() calls in phase 2 will trust it.
if (!process.env.__OTA_LOCAL_PHASE2) {
  fs.mkdirSync(LOCAL, { recursive: true });
  ensureVendorKeys(VENDOR_DIR);
  ensureVendorKeys(OTHER_VENDOR_DIR);
  ensureTlsCert();
  const { status } = require('node:child_process').spawnSync(process.execPath, [__filename], {
    stdio: 'inherit',
    env: { ...process.env, NODE_EXTRA_CA_CERTS: CERT, BRIDGE_UPDATE_HOSTS: '127.0.0.1', __OTA_LOCAL_PHASE2: '1' },
  });
  process.exit(status == null ? 1 : status);
}

// ---- Phase 2: runs with our certificate trusted; everything below is the real, signed, checksummed OTA flow. ----
async function main() {
  const update = require('../bridge/update');
  const { createOta } = require('../ota-server/server');

  const dataDir = path.join(LOCAL, 'ota-data'), downloadDir = path.join(LOCAL, 'downloads'), srcDir = path.join(LOCAL, 'release-src');
  for (const d of [dataDir, downloadDir, srcDir]) { fs.rmSync(d, { recursive: true, force: true }); fs.mkdirSync(d, { recursive: true }); }

  const { loadPrivate, loadPublic } = require('./license');
  const privateKey = loadPrivate(VENDOR_DIR), jwk = loadPublic(VENDOR_DIR);
  const otherPrivateKey = loadPrivate(OTHER_VENDOR_DIR);
  const token = crypto.randomBytes(24).toString('hex');

  const results = [];
  const check = (name, pass, detail) => { results.push({ name, pass, detail }); console.log((pass ? 'PASS' : 'FAIL') + '  ' + name + (detail ? '  -  ' + detail : '')); };

  // the demo release file: real bytes, real SHA-256, served and downloaded for real
  const fileName = 'AudioMixer-9.9.9-demo-linux.tar.gz';
  const fileBytes = crypto.randomBytes(4096);
  fs.writeFileSync(path.join(srcDir, fileName), fileBytes);
  const fileSha256 = crypto.createHash('sha256').update(fileBytes).digest('hex');

  const ota = createOta({ dataDir, jwk, token, publicUrl: '', rate: 1000, allowHttp: false, log: () => {} });
  await ota.reindex();
  const server = https.createServer({ cert: fs.readFileSync(CERT), key: fs.readFileSync(KEY) }, ota.handler);
  await new Promise((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); });
  const port = server.address().port, base = `https://127.0.0.1:${port}`;
  console.log(`Local secure OTA server: ${base}  (admin token: ${token.slice(0, 8)}...)`);

  try {
    // ---- 1: upload the file and publish a correctly signed manifest ----
    const upload = await fetch(`${base}/admin/files/${encodeURIComponent(fileName)}`, {
      method: 'PUT',
      headers: { Authorization: `Bearer ${token}`, 'X-SHA256': fileSha256, 'Content-Type': 'application/octet-stream' },
      body: fileBytes,
    });
    check('upload the release file to the admin API (TLS verified against our own CA)', upload.ok, `HTTP ${upload.status}`);

    const manifest = {
      product: 'audio-mixer', version: '9.9.9', channel: 'stable', released: new Date().toISOString().slice(0, 10),
      notes: ['Local secure OTA demo build'],
      files: { 'linux-tar': { name: fileName, url: `${base}/releases/${encodeURIComponent(fileName)}`, size: fileBytes.length, sha256: fileSha256 } },
    };
    const envelope = update.signManifest(manifest, privateKey);
    const publish = await fetch(`${base}/admin/manifest/stable`, { method: 'PUT', headers: { Authorization: `Bearer ${token}`, 'Content-Type': 'application/json' }, body: JSON.stringify(envelope) });
    const publishBody = await publish.json();
    check('publish the signed manifest', publish.ok && publishBody.ok, `HTTP ${publish.status} ${JSON.stringify(publishBody)}`);

    // ---- 2: the real client accepts it and downloads a verified file ----
    const info = await update.check({ current: '1.0.0', url: `${base}/update.json`, platform: 'linux', arch: 'x64', jwk });
    check('client check(): signed update is recognized', info.ok && info.updateAvailable && info.latest === '9.9.9', JSON.stringify({ updateAvailable: info.updateAvailable, latest: info.latest }));

    const dl = await update.download({ current: '1.0.0', url: `${base}/update.json`, platform: 'linux', arch: 'x64', jwk, dir: downloadDir });
    const onDisk = fs.readFileSync(dl.file);
    check('client download(): file saved and SHA-256 matches the signed manifest', dl.verified && dl.sha256 === fileSha256 && crypto.createHash('sha256').update(onDisk).digest('hex') === fileSha256, dl.sha256);

    // ---- 3: a file tampered on disk after publishing is rejected by the client's own checksum check ----
    const releaseFile = path.join(dataDir, 'releases', fileName);
    const original = fs.readFileSync(releaseFile);
    const tampered = Buffer.from(original); tampered[0] ^= 0xff;
    fs.writeFileSync(releaseFile, tampered);
    let tamperRejected = false, tamperError = '';
    try { await update.download({ current: '1.0.0', url: `${base}/update.json`, platform: 'linux', arch: 'x64', jwk, dir: downloadDir }); }
    catch (e) { tamperRejected = /checksum mismatch/i.test(e.message); tamperError = e.message; }
    fs.writeFileSync(releaseFile, original);   // restore so later checks in this run see the real file again
    check('a release file tampered on disk after publishing is rejected (checksum mismatch)', tamperRejected, tamperError);

    // ---- 4: a manifest signed with a different vendor key is rejected ----
    const wrongKeyEnvelope = update.signManifest(manifest, otherPrivateKey);
    const wrongKeyCheck = update.verifyManifest(wrongKeyEnvelope, jwk);
    check('a manifest signed with a different key is rejected', !wrongKeyCheck.ok, wrongKeyCheck.error);

    // ---- 5: a manifest whose signed payload was altered after signing is rejected ----
    const alteredPayload = envelope.payload.replace('"version":"9.9.9"', '"version":"99.99.99"');
    const alteredCheck = update.verifyManifest({ payload: alteredPayload, signature: envelope.signature }, jwk);
    check('a manifest altered after signing is rejected', !alteredCheck.ok, alteredCheck.error);

    // ---- also confirm the admin API itself is not reachable without the token ----
    const noAuth = await fetch(`${base}/admin/files`, {});
    check('the admin API refuses requests without the token', noAuth.status === 401, `HTTP ${noAuth.status}`);
  } finally {
    await new Promise(r => server.close(r));
  }

  const failed = results.filter(r => !r.pass);
  console.log(`\n${results.length - failed.length}/${results.length} checks passed.`);
  if (failed.length) { console.log('Failed: ' + failed.map(r => r.name).join('; ')); process.exit(1); }
  console.log('The local secure OTA environment works end to end: signed manifests and checksummed files are accepted, tampering and wrong keys are rejected.');
}

main().catch(e => { console.error('Error: ' + (e && e.stack || e)); process.exit(1); });
