#!/usr/bin/env node
// Test fixtures for engine/cpp/test/license_test.cpp, signed with a THROWAWAY key pair made here (never the vendor key).
//   node scripts/make-license-vectors.js                 -> engine/cpp/test/license_vectors.json (keys, edition token, tampered key)
//   node scripts/make-license-vectors.js --mid CODE --out f.json [--term perpetual|annual|trial] [--revoke LID]
//        -> f.json with the same throwaway public key, an activation for CODE and a revocation list, for the store tests
'use strict';
const crypto = require('crypto'), fs = require('fs'), path = require('path');
const arg = n => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : null; };
const b64u = b => Buffer.from(b).toString('base64').replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
const keyFile = path.join(require('os').tmpdir(), 'amfx1-test-signing-key.json');   // the same throwaway pair for one test run
let pair; try { const j = JSON.parse(fs.readFileSync(keyFile, 'utf8')); pair = { privateKey: crypto.createPrivateKey({ key: j, format: 'jwk' }) }; } catch (_) {
  pair = crypto.generateKeyPairSync('ec', { namedCurve: 'P-256' }); fs.writeFileSync(keyFile, JSON.stringify(pair.privateKey.export({ format: 'jwk' })), { mode: 0o600 }); }
const pub = crypto.createPublicKey(pair.privateKey).export({ format: 'jwk' }), jwk = { x: pub.x, y: pub.y };
const kid = crypto.createHash('sha256').update(jwk.x + '.' + jwk.y).digest('hex').slice(0, 16).toUpperCase();
const sign = (prefix, p) => { const body = b64u(JSON.stringify(p)); return `${prefix}.${body}.${b64u(crypto.sign('sha256', Buffer.from(body), { key: pair.privateKey, dsaEncoding: 'ieee-p1363' }))}`; };
const now = 1762700000000, fp = kid.slice(0, 4) + '-' + kid.slice(4, 8) + '-' + kid.slice(8, 12) + '-' + kid.slice(12, 16);
const mid = arg('--mid');
if (!mid) {
  const key = sign('AMIX1', { v: 1, plan: 'pro', id: 'TEST-0001', name: 'Test User', issued: now, expires: 1794236000000, mid: 'ABCD-EF01-2345-6789-ABCD' });
  const out = { jwk, now, fingerprint: fp, key,
    keyAny: sign('AMIX1', { v: 1, plan: 'studio', id: 'TEST-0002', name: 'Any PC', issued: now }),
    token: sign('AMFXE1', { v: 1, kind: 'amfx1-edition', edition: 'studio-master', plan: 'master', serial: 'AMFX-SM-ZSNP-X4WZ-3DSX', issued: now, order: 'SHOP-77' }),
    keyUltimate: sign('AMIX1', { v: 1, plan: 'ultimate', id: 'TEST-0005', name: 'Big Show', issued: now }),
    tokenUltimate: sign('AMFXE1', { v: 1, kind: 'amfx1-edition', edition: 'ultimate', plan: 'ultimate', serial: 'AMFX-ULT-ZSNP-X4WZ-3DSX', issued: now }),
    tokenUltimateWrongPlan: sign('AMFXE1', { v: 1, kind: 'amfx1-edition', edition: 'ultimate', plan: 'master', serial: 'AMFX-ULT-ZSNP-X4WZ-3DSX', issued: now }),
    tampered: key.replace(/\.([A-Za-z0-9_-])/, (m, c) => '.' + (c === 'A' ? 'B' : 'A')),
    activationOther: sign('AMXA1', { v: 1, kind: 'amfx1-activation', kid, lid: 'TEST-0003', mid: 'ABCD-EF01-2345-6789-ABCD', plan: 'master', term: 'perpetual', iat: now, exp: 0 }) };
  fs.writeFileSync(path.join(__dirname, '..', 'engine', 'cpp', 'test', 'license_vectors.json'), JSON.stringify(out, null, 2) + '\n');
  console.log('license vectors written (throwaway key ' + fp + ')');
} else {
  const term = arg('--term') || 'perpetual', exp = term === 'perpetual' ? 0 : now + (term === 'trial' ? 30 : 365) * 86400000;
  const out = { jwk, now, activation: sign('AMXA1', { v: 1, kind: 'amfx1-activation', kid, lid: 'TEST-0004', mid, plan: 'studio', term, iat: now, exp }),
    revocations: sign('AMXR1', { v: 1, kind: 'amfx1-revocations', kid, issued: now + 1000, lids: [arg('--revoke') || 'NOTHING'] }),
    revocationsOld: sign('AMXR1', { v: 1, kind: 'amfx1-revocations', kid, issued: now, lids: [] }),
    // CHANNEL EXTEND packs (+30 channels, bound to this computer) and the keys they sit on
    proBound: sign('AMIX1', { v: 1, plan: 'pro', id: 'TEST-0010', name: 'Pro', issued: now, mid }),
    masterBound: sign('AMIX1', { v: 1, plan: 'master', id: 'TEST-0011', name: 'Master', issued: now, mid }),
    ultimateBound: sign('AMIX1', { v: 1, plan: 'ultimate', id: 'TEST-0012', name: 'Ultimate', issued: now, mid }),
    extends: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10].map(n => sign('AMXX1', { v: 1, kind: 'amfx1-extend', id: 'EXT-000' + n, channels: 30, mid, issued: now })),
    extendOther: sign('AMXX1', { v: 1, kind: 'amfx1-extend', id: 'EXT-0100', channels: 30, mid: 'ABCD-EF01-2345-6789-ABCD', issued: now }),
    extendUnbound: sign('AMXX1', { v: 1, kind: 'amfx1-extend', id: 'EXT-0101', channels: 30, issued: now }),
    extendBig: sign('AMXX1', { v: 1, kind: 'amfx1-extend', id: 'EXT-0102', channels: 300, mid, issued: now }),
    extendExpired: sign('AMXX1', { v: 1, kind: 'amfx1-extend', id: 'EXT-0103', channels: 30, mid, issued: now - 86400000 * 40, expires: now - 86400000 }),
    revocationsExt: sign('AMXR1', { v: 1, kind: 'amfx1-revocations', kid, issued: now + 5000, lids: ['EXT-0002'] }) };
  fs.writeFileSync(arg('--out'), JSON.stringify(out) + '\n'); console.log('activation for ' + mid + ' written');
}
