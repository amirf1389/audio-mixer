'use strict';
// The Electron app's own "is a new version out" check: the same signed, checksummed OTA manifest check every
// other Audio Mixer app already uses (bridge/update.js's check(), the one behind client/cli.js's "update"
// command and the Windows/macOS/Linux/Android/iOS installers) - not a second, separate update mechanism.
// update-electron-app (main.js) still handles actually applying an update to this installed app via GitHub
// Releases; this only answers "is a newer version published", using this app's own version.
//
// Kept free of any `electron` or network dependency so it can be exercised by a plain Node test
// (platforms/electron/test.js) without installing Electron or reaching the network.

// Human-readable notification content for one check() result ({ ok, updateAvailable, current, latest, notes,
// ... }, or null when the check itself failed, e.g. no network). When notes (the manifest's own "what's new"
// list, already shown by the web UI's OTA card and the CLI's "update" command) are present, up to 3 are added
// as extra lines - matching how an OS notification actually renders, not the full list the other two UIs show.
function describeUpdateCheck(info) {
  if (!info || !info.ok) return { title: 'Audio Mixer', body: 'Could not check for updates right now.' };
  if (info.updateAvailable) {
    const notes = Array.isArray(info.notes) ? info.notes.slice(0, 3) : [];
    const body = `Version ${info.latest} is available (you have ${info.current}).` + (notes.length ? '\n' + notes.map(n => '• ' + n).join('\n') : '');
    return { title: 'Audio Mixer update available', body };
  }
  return { title: 'Audio Mixer', body: `You are up to date (${info.current}).` };
}

// Runs the real check (bridge/update.js's check(), injected as `checkImpl` so tests can supply a fake one)
// against this app's own installed version. Never throws: a failed check (no network, bad manifest, ...) comes
// back as null, exactly like describeUpdateCheck expects.
async function checkForUpdates({ current, checkImpl, log = () => {} } = {}) {
  try {
    const info = await checkImpl({ current });
    return info && info.ok ? info : null;
  } catch (e) {
    log('update check failed: ' + (e && e.message || e));
    return null;
  }
}

module.exports = { describeUpdateCheck, checkForUpdates };
