'use strict';
// Smoke test for the EXPERIMENTAL, unwired asio-binder addon (see README.md). Run from this folder:
//   npm install && npm run build && npm test
// Skips itself (rather than failing) when the addon has not been built here, so it does not block
// `node --test` runs elsewhere in the repo that never touch this folder.
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

test('binding.gyp and package.json are well-formed', () => {
  const gyp = JSON.parse(fs.readFileSync(path.join(__dirname, 'binding.gyp'), 'utf8'));
  assert.strictEqual(gyp.targets[0].target_name, 'asio_binder');
  assert.deepStrictEqual(gyp.targets[0].sources, ['src/main.cc', 'src/asiosdk.cc']);
  const pkg = JSON.parse(fs.readFileSync(path.join(__dirname, 'package.json'), 'utf8'));
  assert.strictEqual(pkg.name, 'asio-binder');
  assert.ok(pkg.dependencies['node-addon-api']);
});

test('index.js fails clearly when the addon is not built', (t) => {
  if (fs.existsSync(path.join(__dirname, 'build', 'Release', 'asio_binder.node'))) return t.skip('already built here');
  assert.throws(() => { delete require.cache[require.resolve('./index.js')]; require('./index.js'); }, /npm install -g node-gyp/);
});

test('the built addon: listDrivers()/initializeDriver() behave as documented', (t) => {
  const built = path.join(__dirname, 'build', 'Release', 'asio_binder.node');
  if (!fs.existsSync(built)) return t.skip('not built here: npm install && npm run build first (needs node-gyp)');
  const asio = require(built);
  assert.strictEqual(asio.wired, false, 'stays false until a real integration wires it in (see README.md)');
  assert.throws(() => asio.initializeDriver(), process.platform === 'win32' ? /not wired into Audio Mixer/ : /Windows only/);
  if (process.platform !== 'win32') assert.throws(() => asio.listDrivers(), /Windows only/);
});
