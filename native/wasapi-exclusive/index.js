'use strict';
// EXPERIMENTAL, UNWIRED: see README.md. Nothing in bridge/, client/ or engine/cpp requires this file.
//   const wasapi = require('./native/wasapi-exclusive');
//   wasapi.listEndpoints();                                      // Windows only: read-only, safe alongside the server's own streams
//   wasapi.probeExclusiveFormat(id, 48000, 2, 24);                // Windows only: read-only, safe alongside the server's own streams
//   wasapi.openExclusive();                                       // throws until a real integration wires it through bridge/audify.js
function load() {
  try {
    return require('./build/Release/wasapi_exclusive.node');
  } catch (e) {
    throw new Error(
      'wasapi_exclusive is not built. In this folder: npm install -g node-gyp (once, needs Python 3 and, on Windows, ' +
      'the Visual Studio Build Tools C++ workload), then npm install && npm run build. Original error: ' + e.message
    );
  }
}
module.exports = load();
