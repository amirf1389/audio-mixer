'use strict';
// The one place PortAudio (naudiodon2) is loaded. With the DEVICE SIMULATOR on (AMFX_SIM=1, or SIMULATED DEVICES on the page) it returns the
// simulator instead (bridge/sim), which has the same API, so inputs, outputs, duplex streams and device detection all run against it.
function load() {
  const sim = require('./sim');
  if (sim.enabled()) return sim.portaudio();
  return require('naudiodon2');
}
module.exports = { load };
