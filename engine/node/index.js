'use strict';
// Loads the native addon when it was built (node scripts/build-engine.js --node); otherwise reports that it is not available and the callers use JavaScript.
//   const native = require('./engine/node');   native.available -> boolean;  native.Meter, native.Loudness, native.Mixer, native.scope  (classes of the C++ engine)
// AUDIO_MIXER_NATIVE=0 turns it off.
const path = require('node:path');
function load() {
  if (process.env.AUDIO_MIXER_NATIVE === '0') return null;
  for (const p of [path.join(__dirname, 'build', 'Release', 'am_engine.node'), path.join(__dirname, 'am_engine.node')]) { try { return require(p); } catch (_) { /* not built here */ } }
  return null;
}
const addon = load();
module.exports = addon ? { available: true, ...addon } : { available: false };
