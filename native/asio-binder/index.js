'use strict';
// EXPERIMENTAL, UNWIRED: see README.md. Nothing in bridge/, client/ or engine/cpp requires this file.
//   const asio = require('./native/asio-binder');
//   asio.listDrivers();       // Windows only: one registry view, read-only, safe today
//   asio.initializeDriver();  // throws until a real integration wires it through bridge/asio-lock.js
function load() {
  try {
    return require('./build/Release/asio_binder.node');
  } catch (e) {
    throw new Error(
      'asio_binder is not built. In this folder: npm install -g node-gyp (once, needs Python 3 and, on Windows, ' +
      'the Visual Studio Build Tools C++ workload), then npm install && npm run build. Original error: ' + e.message
    );
  }
}
module.exports = load();
