'use strict';
// The release channel of this build ("beta" for 1.22.1.0 Beta, empty for a normal release), from package.json "releaseChannel".
// Version STRINGS stay numeric everywhere code compares them (MSI ProductVersion, the OTA cmpVersion, npm); only file names and the
// label shown to people carry the channel: "AmF X1-1.22.1.0-beta.exe", "v1.22.1.0 Beta".
const path = require('node:path');
function channel(pkg) { try { pkg = pkg || require(path.join(__dirname, '..', 'package.json')); } catch (_) { return ''; } return String(pkg.releaseChannel || '').toLowerCase().replace(/[^a-z0-9]/g, ''); }
const fileVersion = (version, ch = channel()) => version + (ch ? '-' + ch : '');
const displayVersion = (version, ch = channel()) => 'v' + version + (ch ? ' ' + ch.charAt(0).toUpperCase() + ch.slice(1) : '');
module.exports = { channel, fileVersion, displayVersion };
