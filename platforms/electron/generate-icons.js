#!/usr/bin/env node
'use strict';
// Writes this package's own build/icon.png (tray + Linux/macOS) and build/icon.ico (Windows), reusing the same
// pure-JS icon generator the Android build already uses (scripts/build-apk.js: no image library, no native deps).
//   node generate-icons.js
const fs = require('node:fs');
const path = require('node:path');
const { png, iconPixel } = require('../../scripts/build-apk');
const { icoFile } = require('../../scripts/build-exe');

const OUT = path.join(__dirname, 'build');
fs.mkdirSync(OUT, { recursive: true });
fs.writeFileSync(path.join(OUT, 'icon.png'), png(512, iconPixel));
fs.writeFileSync(path.join(OUT, 'icon.ico'), icoFile());
console.log('Icons written to ' + OUT);
