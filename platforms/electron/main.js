'use strict';
// Audio Mixer as an Electron app: a new, optional way to run the mixer alongside the existing native launcher
// (platforms/windows/installer/launcher.c) and installers - neither of those is touched by this file.
//
// The main process starts the same bridge/server.js this project already ships (in-process, exactly like
// client/cli.js's "start" command does), opens a BrowserWindow on it instead of the system browser, and adds a
// tray icon with Open / turn the server on or off / Exit, plus a real OS notification when the server shuts down.
// Auto-updates follow Electron's own update guide (https://www.electronjs.org/docs/latest/tutorial/updates):
// update-electron-app wraps Electron's autoUpdater against GitHub Releases (via update.electronjs.org, the
// service that guide points at) using electron-builder's --publish github output (package.json's "build.publish").
// It only runs in a packaged build, never in `electron .` development - matching the guide: autoUpdater needs an
// installed, code-signed app with Squirrel.Windows / Squirrel.Mac to apply an update to.
//
//   cd platforms/electron && npm install && npm start      (development)
//   cd platforms/electron && npm run build                 (packaged app for this OS, via electron-builder)
const path = require('node:path');
const { app, BrowserWindow, Tray, Menu, Notification, nativeImage } = require('electron');

// Packaged build: bridge/ and index.html are copied next to the app by electron-builder's extraResources
// (see package.json), so bridge/server.js's own ROOT (one level above __dirname) still resolves to the right
// place. Development: this project's real bridge/, two levels up from this file.
const ROOT = app.isPackaged ? process.resourcesPath : path.join(__dirname, '..', '..');
const bridgeServer = require(path.join(ROOT, 'bridge', 'server'));
// generate-icons.js (run by "npm start" / "npm run build") writes this once per run; bundled as an ordinary app
// file (not excluded from "files"), so it reads the same way in development and inside the packaged app.
const ICON_FILE = path.join(__dirname, 'build', 'icon.png');

const PORT = Number(process.env.BRIDGE_PORT) || bridgeServer.PORT;
const REPO = 'amirf1389/audio-mixer';
const { trayMenuTemplate } = require(path.join(__dirname, 'tray-menu'));

let win = null, tray = null, serverUp = false, quitting = false;

async function startServer() {
  if (serverUp) return;
  await bridgeServer.start(PORT);
  serverUp = true;
}
function stopServer() {
  if (!serverUp) return;
  bridgeServer.server.close();
  serverUp = false;
}

function trayIcon() { return nativeImage.createFromPath(ICON_FILE); }

function createWindow() {
  win = new BrowserWindow({
    width: 1280, height: 800, title: 'Audio Mixer', icon: trayIcon(),
    webPreferences: { contextIsolation: true, sandbox: true, nodeIntegration: false },
  });
  win.loadURL(`http://localhost:${PORT}/`);
  win.on('close', e => { if (!quitting) { e.preventDefault(); win.hide(); } });
}

function refreshTray() {
  if (!tray) return;
  tray.setContextMenu(Menu.buildFromTemplate(trayMenuTemplate(serverUp, {
    onOpen: () => { if (win) { win.show(); win.focus(); } },
    onToggle: toggleServer,
    onExit: () => { quitting = true; app.quit(); },
  })));
}

async function toggleServer() {
  if (serverUp) stopServer(); else await startServer();
  refreshTray();
}

function createTray() {
  tray = new Tray(trayIcon());
  tray.setToolTip('Audio Mixer');
  tray.on('click', () => { if (win) win.isVisible() ? win.hide() : win.show(); });
  refreshTray();
}

function notifyShutdown() {
  try { if (Notification.isSupported()) new Notification({ title: 'Audio Mixer', body: 'The local server is shutting down.', icon: trayIcon() }).show(); } catch (_) { /* best-effort */ }
}

// https://www.electronjs.org/docs/latest/tutorial/updates - the official guide this follows: update-electron-app
// is its recommended wrapper around autoUpdater, checking GitHub Releases through update.electronjs.org.
function startAutoUpdates() {
  // Only in a packaged build: in development there is no installed app for Squirrel/autoUpdater to update, and
  // update-electron-app itself refuses to run unpackaged.
  if (!app.isPackaged) return;
  try { require('update-electron-app')({ repo: REPO, updateInterval: '1 hour', logger: console }); } catch (e) { console.error('auto-update not started: ' + e.message); }
}

app.on('before-quit', () => {
  quitting = true;
  if (serverUp) { notifyShutdown(); stopServer(); }
});

app.on('window-all-closed', () => { /* keep running in the tray */ });

// Electron always loads this file as the app's one entry point (there is no other consumer to guard against,
// unlike a CLI script); require.main === module does not reliably hold under Electron's own module loader, so
// the startup code runs unconditionally here.
app.whenReady().then(async () => {
  startAutoUpdates();
  await startServer();
  createWindow();
  createTray();
}).catch(e => { console.error('Audio Mixer failed to start: ' + (e && e.stack || e)); app.exit(1); });

module.exports = { trayMenuTemplate, PORT, REPO };
