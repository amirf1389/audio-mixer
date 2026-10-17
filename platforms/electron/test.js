'use strict';
// Tests for the Electron shell's own pure logic. These never require() the `electron` module itself (it only
// behaves like the real API inside a running Electron process - requiring it from plain Node returns a path
// string, not {app, BrowserWindow, ...}), so main.js's logic worth testing lives in tray-menu.js instead.
//   node --test test.js   (needs no `npm install` - tray-menu.js is dependency-free)
const test = require('node:test');
const assert = require('node:assert/strict');
const { trayMenuTemplate } = require('./tray-menu');

test('tray menu: server ON shows the right labels and wires the right callbacks', () => {
  const calls = { open: 0, toggle: 0, exit: 0 };
  const items = trayMenuTemplate(true, { onOpen: () => calls.open++, onToggle: () => calls.toggle++, onExit: () => calls.exit++ });
  assert.equal(items[0].label, 'Server: ON');
  assert.equal(items[0].enabled, false);
  assert.equal(items[1].label, 'Open Audio Mixer');
  assert.equal(items[2].label, 'Turn server OFF');
  assert.equal(items[3].type, 'separator');
  assert.equal(items[4].label, 'Exit');
  items[1].click(); items[2].click(); items[4].click();
  assert.deepEqual(calls, { open: 1, toggle: 1, exit: 1 });
});

test('tray menu: server OFF flips the status line and the toggle label, same structure', () => {
  const items = trayMenuTemplate(false, {});
  assert.equal(items[0].label, 'Server: OFF');
  assert.equal(items[2].label, 'Turn server ON');
  assert.equal(items.length, 5);
});

test('tray menu: never silently drops the Exit item (the one way out of the tray)', () => {
  for (const up of [true, false]) {
    const items = trayMenuTemplate(up, {});
    assert.ok(items.some(i => i.label === 'Exit'), 'Exit item missing when serverUp=' + up);
  }
});

test('generate-icons.js writes a real PNG and a real multi-size ICO using the project\'s own pure-JS encoders', () => {
  const fs = require('node:fs');
  const path = require('node:path');
  const { execFileSync } = require('node:child_process');
  const buildDir = path.join(__dirname, 'build');
  fs.rmSync(buildDir, { recursive: true, force: true });
  execFileSync(process.execPath, ['generate-icons.js'], { cwd: __dirname });
  const png = fs.readFileSync(path.join(buildDir, 'icon.png'));
  const ico = fs.readFileSync(path.join(buildDir, 'icon.ico'));
  assert.deepEqual(png.subarray(0, 8), Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), 'not a PNG signature');
  assert.equal(ico.readUInt16LE(0), 0, 'ICO reserved field must be 0');
  assert.equal(ico.readUInt16LE(2), 1, 'ICO type field must be 1 (icon)');
  assert.ok(ico.readUInt16LE(4) >= 1, 'ICO must list at least one image');
});
