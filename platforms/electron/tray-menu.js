'use strict';
// The Electron tray's menu, as plain data with no dependency on the `electron` module itself, so it can be
// exercised by a plain Node test (platforms/electron/test.js) without an Electron runtime installed.
function trayMenuTemplate(serverUp, { onOpen, onToggle, onExit } = {}) {
  return [
    { label: serverUp ? 'Server: ON' : 'Server: OFF', enabled: false },
    { label: 'Open Audio Mixer', click: onOpen },
    { label: serverUp ? 'Turn server OFF' : 'Turn server ON', click: onToggle },
    { type: 'separator' },
    { label: 'Exit', click: onExit },
  ];
}
module.exports = { trayMenuTemplate };
