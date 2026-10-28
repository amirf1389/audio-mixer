'use strict';
// Read-only ASIO driver listing (Windows). Nothing in bridge/, client/ or engine/cpp requires this file, and it never opens a driver:
// real ASIO detection stays in bridge/detect.js and streams open only through bridge/audify.js.
//   const asio = require('./native/asio-binder');
//   asio.listDrivers();       // Windows only: one registry view, read-only
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
