'use strict';
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');
const { detect, keysFor } = require('./detect');
const mf = require('./manifest');
const { plan, howTo } = require('./install');

const HELP = `audio-mixer-install: installs Audio Mixer the way this operating system does it

  npx audio-mixer-install [options]            install (asks before it runs anything)
  npx audio-mixer-install uninstall [options]  remove it again

  Windows 64-bit  the signed setup (.exe) -> C:\\Program Files\\Audio Mixer       Windows 32-bit  the .msi -> C:\\Program Files (x86)\\Audio Mixer
  macOS           the archive: install.command -> /Applications                  Debian / Ubuntu  the .deb with apt          other Linux  the archive: install.sh
  Android / iOS   only downloaded (.apk / Xcode project): they are not installed from a computer's npm

Options
  --yes, -y         no questions (Windows: /passive, macOS: no start-at-login, Linux: apt -y / install.sh --yes)
  --quiet           Windows: silent install
  --asio            Windows: also register the Audio Mixer ASIO driver (its files are always installed; the default is not to register it)
  --dry-run         show what would be downloaded and run, change nothing
  --download-only   download and verify the file into --out (default: the current folder), do not run it
  --server URL      signed manifest (default: the project's releases/update.json on GitHub); an Audio Mixer OTA server: https://host/update.json
  --host NAME       one more host that the manifest and the files may come from (the host of --server is always allowed)
  --dir FOLDER      no download: the newest file of this system in a releases folder (for example <checkout>/windows/releases)
  --file PATH       no download: install this file (kind taken from its name)
  --os NAME --arch NAME --distro NAME   choose the system by hand (windows | macos | linux | android | ios; x64 | x86; debian | other)
  --out FOLDER      where --download-only (and android / ios) save the file
  --allow-http      accept http for localhost (testing)
  --json            print the plan as JSON (with --dry-run)

A downloaded manifest must be signed by the Audio Mixer publisher and a file must match its SHA-256 before anything runs.`;

const FLAGS = new Set(['--yes', '-y', '--quiet', '--asio', '--dry-run', '--download-only', '--allow-http', '--json', '--help', '-h']);
function parseArgs(argv) {
  const o = { hosts: [] }; const rest = [];
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (FLAGS.has(a)) o[a.replace(/^-+/, '').replace(/-(\w)/g, (_, c) => c.toUpperCase())] = true;
    else if (['--server', '--host', '--dir', '--file', '--os', '--arch', '--distro', '--out'].includes(a)) { if (i + 1 >= argv.length) throw new Error(a + ' needs a value'); const v = argv[++i]; if (a === '--host') o.hosts.push(v); else o[a.slice(2)] = v; }
    else if (a.startsWith('-')) throw new Error('unknown option ' + a + ' (see --help)');
    else rest.push(a);
  }
  if (o.y) o.yes = true; if (o.h) o.help = true;
  o.command = rest[0] || 'install';
  if (!['install', 'uninstall'].includes(o.command) || rest.length > 1) throw new Error('unknown command ' + rest.join(' ') + ' (install | uninstall)');
  return o;
}

const ask = q => new Promise(res => { const rl = require('node:readline').createInterface({ input: process.stdin, output: process.stdout }); rl.question(q, a => { rl.close(); res(/^y(es)?$/i.test(a.trim())); }); });
const which = n => (process.env.PATH || '').split(path.delimiter).some(d => d && fs.existsSync(path.join(d, n + (process.platform === 'win32' ? '.exe' : ''))));

// main(argv, { spawn, log, env }) -> exit code; the spawn / log hooks are what the tests replace
async function main(argv, io = {}) {
  const st = {};
  try { return await run(argv, io, st); } finally { if (st.work) fs.rmSync(st.work, { recursive: true, force: true }); }   // the downloaded file and the unpacked archive
}

async function run(argv, io, st) {
  const log = io.log || (s => console.log(s)), spawn = io.spawn || ((c, a, o) => cp.spawnSync(c, a, o));
  const o = parseArgs(argv);
  if (o.help) { log(HELP); return 0; }
  const det = detect({ os: o.os, arch: o.arch, distro: o.distro, platform: io.platform, env: io.env, debian: io.debian });
  if (!det.os) throw new Error('this system is not known; choose one with --os windows|macos|linux|android|ios');
  const uninstall = o.command === 'uninstall';
  const hasApt = io.hasApt !== undefined ? io.hasApt : which('apt');
  const keys = keysFor(det, { hasApt });
  if (!keys.length) throw new Error('nothing to install for this system');

  // 1. which file
  let file, source;
  if (o.file) {
    const keyOf = [['win-x64-exe', /\.exe$/i], ['win-x64-msi', /x64\.msi$/i], ['win-x86-msi', /\.msi$/i], ['linux-deb', /\.deb$/i], ['android-apk', /\.apk$/i], ['ios-project', /ios.*\.tar\.gz$/i], ['macos', /macos.*\.tar\.gz$/i], ['linux-tar', /\.tar\.gz$/i]];
    const hit = keyOf.find(([, re]) => re.test(o.file));
    if (!hit || !fs.existsSync(o.file)) throw new Error('--file: no such Audio Mixer file: ' + o.file);
    file = { key: hit[0], name: path.basename(o.file), path: path.resolve(o.file) }; source = 'the file you gave';
  } else if (o.dir) {
    const f = mf.fromFolder(o.dir, keys);
    if (!f) throw new Error('no Audio Mixer file for ' + det.os + (det.arch ? ' ' + det.arch : '') + ' in ' + o.dir);
    file = { ...f, path: path.resolve(f.path) }; source = 'the folder ' + o.dir;
  } else {
    const server = o.server || process.env.AUDIO_MIXER_UPDATE_URL || mf.DEFAULT_URL;
    let host; try { host = new URL(server).hostname; } catch (_) { throw new Error('bad --server url'); }
    const net = { hosts: mf.HOSTS.concat(host, o.hosts), allowHttp: !!o.allowHttp, jwk: io.jwk };
    const m = await mf.fetchManifest(server, net);
    const pick = mf.pickFile(m, keys);
    if (!pick) throw new Error(`version ${m.version} lists no file for ${det.os}${det.arch ? ' ' + det.arch : ''} (${keys.join(', ')})`);
    file = { ...pick, version: m.version, net }; source = server + ' (signed, version ' + m.version + ')';
  }

  const isRoot = typeof process.getuid === 'function' && process.getuid() === 0, hasSudo = which('sudo');
  const work = o.dryRun ? path.join(require('node:os').tmpdir(), 'audio-mixer-install-<random>') : (st.work = (io.tmp || mf.tmpDir)());
  const steps = plan({ det, key: file.key, file: file.path || path.join(o.downloadOnly || det.os === 'android' || det.os === 'ios' ? path.resolve(o.out || '.') : work, path.basename(file.name)), yes: !!o.yes, quiet: !!o.quiet, asio: !!o.asio, uninstall, isRoot, hasSudo, work });
  if (o.asio && det.os !== 'windows') log('Note: --asio only applies to Windows.');
  if (o.json && o.dryRun) { log(JSON.stringify({ system: det, source, file: { key: file.key, name: file.name, url: file.url || null, sha256: file.sha256 || null }, steps }, null, 2)); return 0; }
  log(`System:  ${det.os}${det.arch ? ' ' + det.arch : ''}${det.distro ? ' (' + det.distro + ')' : ''}\nSource:  ${source}\nFile:    ${file.name}`);
  const portable = det.os === 'android' || det.os === 'ios';
  if (!portable) steps.forEach((s, i) => log(`Step ${i + 1}:  ${s.label}\n         ${[s.cmd].concat(s.args).map(a => (/\s/.test(a) ? JSON.stringify(a) : a)).join(' ')}`));
  else log(howTo[det.os]);
  if (o.dryRun) { log('Dry run: nothing was downloaded or run.'); return 0; }

  // 2. the file: download + check, unless it is local
  if (!file.path) {
    const dest = o.downloadOnly || portable ? path.resolve(o.out || '.') : work;
    file.path = await mf.download(file, dest, file.net);
    log('Downloaded and verified (SHA-256 matches the signed manifest): ' + file.path);
    steps.splice(0, steps.length, ...plan({ det, key: file.key, file: file.path, yes: !!o.yes, quiet: !!o.quiet, asio: !!o.asio, uninstall, isRoot, hasSudo, work }));
  }
  if (o.downloadOnly || portable) { if (portable) log(howTo[det.os]); return 0; }

  // 3. the system's own installation
  if (!o.yes && !o.quiet && !(await (io.ask || ask)(`Run ${steps.length > 1 ? 'these steps' : 'this step'} now? [y/N] `))) { log('Nothing was run.'); return 0; }
  for (const s of steps) {
    log('> ' + s.label);
    const r = spawn(s.cmd, s.args, { stdio: s.stdin ? ['pipe', 'inherit', 'inherit'] : 'inherit', input: s.stdin, cwd: s.cwd });
    if (r.error) throw new Error(`${s.cmd} could not be started: ${r.error.message}`);
    if (r.status !== 0 && r.status !== 3010) { log(`${s.cmd} ended with code ${r.status}.`); return r.status || 1; }   // 3010: Windows Installer asks for a restart
  }
  log(uninstall ? 'Audio Mixer was removed.' : 'Audio Mixer is installed.');
  return 0;
}

module.exports = { main, parseArgs, HELP };
