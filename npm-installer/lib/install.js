'use strict';
// The own installation of each operating system, as a list of steps (a command and its arguments). Nothing here runs a command: bin/audio-mixer-install.js does, after
// the file passed the signature / SHA-256 check, and --dry-run only prints the steps.
const path = require('node:path');

const MSI_FEATURES = 'Main,Shortcuts,Tools,CommandLine,PluginHost,WinHelpers,Desktop';   // the default features of the package; AsioDriver (registers the ASIO driver) is added with --asio
const psq = s => "'" + String(s).replace(/'/g, "''") + "'";
const versionOf = name => (/(\d+(?:\.\d+){1,3})/.exec(String(name)) || [])[1] || '';

// plan({ det, key, file, yes, quiet, asio, uninstall, isRoot, hasSudo, work }) -> [{ label, cmd, args, stdin?, interactive? }]
function plan({ det, key, file, yes = false, quiet = false, asio = false, uninstall = false, isRoot = false, hasSudo = true, work = '/tmp' }) {
  const steps = [], name = path.basename(file || '');
  const sudo = c => (isRoot || !hasSudo ? c : ['sudo'].concat(c));
  if (det.os === 'windows') {
    if (key === 'win-x64-exe') {
      // the setup program asks for administrator rights itself (UAC): Start-Process hands it to the shell, which can show that prompt; a plain spawn would fail with "requires elevation"
      const flags = uninstall ? ['/uninstall'] : (quiet ? ['/quiet'] : yes ? ['/passive'] : []).concat(asio ? ['/asio'] : yes || quiet ? ['/noasio'] : []);
      const list = flags.length ? ' -ArgumentList @(' + flags.map(psq).join(',') + ')' : '';
      steps.push({ label: `${uninstall ? 'Remove' : 'Install'} Audio Mixer with ${name}${asio ? ' (and register the ASIO driver)' : ''}`, cmd: 'powershell.exe', args: ['-NoProfile', '-NonInteractive', '-Command', `$p = Start-Process -FilePath ${psq(file)}${list} -Wait -PassThru; exit $p.ExitCode`] });
    } else {   // the 32-bit package (Program Files (x86)): msiexec elevates by itself
      const args = ['/i', file, quiet ? '/qn' : '/passive'];
      if (asio) args.push('ADDLOCAL=' + MSI_FEATURES + ',AsioDriver');
      steps.push({ label: `Install Audio Mixer (32-bit) from ${name}${asio ? ' and register the ASIO driver' : ''}`, cmd: 'msiexec.exe', args });
    }
    return steps;
  }
  if (det.os === 'macos') {
    const dir = path.join(work, 'AudioMixer');
    steps.push({ label: 'Unpack ' + name, cmd: 'tar', args: ['xzf', file, '-C', work] });
    steps.push({ label: 'Run install.command (copies Audio Mixer.app to /Applications, or ~/Applications; installs Node.js 18+ when it is missing)', cmd: 'sh', args: [path.join(dir, uninstall ? 'uninstall.command' : 'install.command')], cwd: dir, interactive: !yes, stdin: yes ? 'n\n' : undefined });
    return steps;
  }
  if (det.os === 'linux') {
    if (key === 'linux-deb') {
      const c = uninstall ? sudo(['apt', 'remove'].concat(yes ? ['-y'] : [], ['audio-mixer'])) : sudo(['apt', 'install'].concat(yes ? ['-y'] : [], [file]));
      steps.push({ label: uninstall ? 'Remove the package audio-mixer' : 'Install ' + name + ' with apt (/opt/audio-mixer, the audio-mixer command, a menu entry)', cmd: c[0], args: c.slice(1), interactive: !yes });
      return steps;
    }
    const dir = path.join(work, 'audio-mixer-' + versionOf(name));
    steps.push({ label: 'Unpack ' + name, cmd: 'tar', args: ['xzf', file, '-C', work] });
    steps.push({ label: `Run ${uninstall ? 'uninstall' : 'install'}.sh (${isRoot ? '/opt/audio-mixer + /usr/local/bin' : '~/.local/share/audio-mixer-app + ~/.local/bin'}; downloads Node.js 18+ when it is missing)`, cmd: 'sh', args: [path.join(dir, uninstall ? 'uninstall.sh' : 'install.sh')].concat(yes && !uninstall ? ['--yes'] : []), cwd: dir, interactive: !yes });
    return steps;
  }
  return steps;   // android / ios: the file is only downloaded (see howTo)
}

const howTo = { android: 'Copy the .apk to the phone and open it (allow installs from this source when Android asks).', ios: 'iOS apps are built with Xcode on a Mac: unpack the archive, run xcodegen generate, open the project, choose your team and run it on the device.' };

module.exports = { plan, howTo, versionOf, MSI_FEATURES };
