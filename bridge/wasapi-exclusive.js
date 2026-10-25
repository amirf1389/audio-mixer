'use strict';
// Honest status for the EXPERIMENTAL, unwired native/wasapi-exclusive addon (see its own README.md). RtAudio
// (bridge/audify.js) opens WASAPI in shared mode only - it has no exclusive-mode path at all, so this module never
// claims one exists through audify. It only reports whether the addon is present/built here, the same way
// bridge/audify.js's loadProblem() reports why the audify module itself did not load, instead of silently
// pretending exclusive mode is available when it is not.
const path = require('node:path');

function load() {
  try { return require(path.join(__dirname, '..', 'native', 'wasapi-exclusive', 'build', 'Release', 'wasapi_exclusive.node')); }
  catch (e) { return null; }
}

// Data for GET /api/wasapi-exclusive. Never throws: an unbuilt or non-Windows addon is a normal, reportable state,
// not an error - the same shape bridge/audify.js's describe() uses for "not installed".
function status() {
  if (process.platform !== 'win32') return { available: false, wired: false, reason: 'Windows only' };
  const addon = load();
  if (!addon) return { available: false, wired: false, reason: 'native/wasapi-exclusive is not built here: see native/wasapi-exclusive/README.md ("npm install && npm run build" in that folder)' };
  return { available: true, wired: !!addon.wired, reason: addon.wired ? null : 'listEndpoints() / probeExclusiveFormat() work; openExclusive() is a deliberate stub until wired in for real (see native/wasapi-exclusive/README.md)' };
}

// Every active render + capture endpoint (read-only, harmless - see native/wasapi-exclusive/README.md). Throws the
// addon's own "not built" / "Windows only" error on failure; callers decide how to surface that (see server.js).
function listEndpoints() { return loadOrThrow().listEndpoints(); }

// Whether `endpointId` would accept this exact PCM format in exclusive mode (read-only probe, no stream opened).
function probeExclusiveFormat(endpointId, sampleRate, channels, bitsPerSample) {
  return loadOrThrow().probeExclusiveFormat(endpointId, sampleRate, channels, bitsPerSample);
}

function loadOrThrow() {
  const addon = load();
  if (!addon) throw new Error('native/wasapi-exclusive is not built here: see native/wasapi-exclusive/README.md');
  return addon;
}

module.exports = { status, listEndpoints, probeExclusiveFormat };
