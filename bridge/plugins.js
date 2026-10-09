'use strict';
// Plugin system: finds VST3 (.vst3), VST2 (.dll / .vst) plugins in the standard folders and in the app's own plugin folder,
// and checks each Windows binary (PE header + export table) so the mixer can tell a real, loadable VST from any other DLL.
// This lists and validates plugins; it does not run them (a VST host process is a separate piece of work).
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');

const MACHINE = { 0x14c: 'x86', 0x8664: 'x64', 0xaa64: 'arm64', 0x1c4: 'arm' };
const HOST_ARCH = { x64: 'x64', ia32: 'x86', arm64: 'arm64', arm: 'arm' };
const VST2_ENTRIES = ['VSTPluginMain', 'main', 'main_macho'];
const VST3_ENTRIES = ['GetPluginFactory'];

function appPluginDir(env = process.env, home = os.homedir()) { return env.AUDIO_MIXER_PLUGINS || path.join(home, 'AudioMixerPlugins'); }

function pluginDirs({ platform = process.platform, env = process.env, home = os.homedir() } = {}) {
  const app = appPluginDir(env, home);
  if (platform === 'win32') {
    const pf = env.ProgramFiles || 'C:\\Program Files';
    const cf = env.CommonProgramFiles || path.join(pf, 'Common Files');
    return [app, path.join(cf, 'VST3'), path.join(pf, 'VSTPlugins'), path.join(pf, 'Steinberg', 'VSTPlugins'), path.join(cf, 'VST2')];
  }
  if (platform === 'darwin') return [app, '/Library/Audio/Plug-Ins/VST3', path.join(home, 'Library/Audio/Plug-Ins/VST3'), '/Library/Audio/Plug-Ins/VST', path.join(home, 'Library/Audio/Plug-Ins/VST')];
  return [app, path.join(home, '.vst3'), '/usr/lib/vst3', '/usr/local/lib/vst3', path.join(home, '.vst'), '/usr/lib/vst', '/usr/local/lib/vst'];
}

// ── PE reading (reads only the headers and the export names, never the whole plugin) ──
function readAt(fd, pos, len) { const b = Buffer.alloc(len); const n = fs.readSync(fd, b, 0, len, pos); return n === len ? b : b.subarray(0, n); }

function inspectPe(file) {
  let fd;
  try { fd = fs.openSync(file, 'r'); } catch (_) { return null; }
  try {
    const dos = readAt(fd, 0, 64);
    if (dos.length < 64 || dos.readUInt16LE(0) !== 0x5a4d) return null;
    const pe = dos.readUInt32LE(0x3c);
    const hdr = readAt(fd, pe, 24 + 240);
    if (hdr.length < 24 || hdr.readUInt32LE(0) !== 0x4550) return null;
    const machine = hdr.readUInt16LE(4), nSec = hdr.readUInt16LE(6), optSize = hdr.readUInt16LE(20), characteristics = hdr.readUInt16LE(22);
    const magic = hdr.readUInt16LE(24), plus = magic === 0x20b;
    const ddOff = 24 + (plus ? 112 : 96);
    const out = { arch: MACHINE[machine] || 'unknown', isDll: !!(characteristics & 0x2000), exports: [] };
    if (hdr.length < ddOff + 8) return out;
    const expRva = hdr.readUInt32LE(ddOff);
    if (!expRva) return out;
    const secs = readAt(fd, pe + 24 + optSize, nSec * 40);
    const toOff = rva => { for (let i = 0; i + 40 <= secs.length; i += 40) { const va = secs.readUInt32LE(i + 12), raw = secs.readUInt32LE(i + 20), sz = Math.max(secs.readUInt32LE(i + 8), secs.readUInt32LE(i + 16)); if (rva >= va && rva < va + sz) return rva - va + raw; } return -1; };
    const eo = toOff(expRva); if (eo < 0) return out;
    const ed = readAt(fd, eo, 40); if (ed.length < 40) return out;
    const nNames = Math.min(ed.readUInt32LE(24), 4000), namesOff = toOff(ed.readUInt32LE(32));
    if (namesOff < 0 || !nNames) return out;
    const ptrs = readAt(fd, namesOff, nNames * 4);
    for (let i = 0; i + 4 <= ptrs.length; i += 4) {
      const no = toOff(ptrs.readUInt32LE(i)); if (no < 0) continue;
      const s = readAt(fd, no, 64), z = s.indexOf(0);
      out.exports.push(s.subarray(0, z < 0 ? s.length : z).toString('latin1'));
    }
    return out;
  } catch (_) { return null; } finally { try { fs.closeSync(fd); } catch (_) { /* closed */ } }
}

function classify(file, ext, platform, hostArch) {
  const base = path.basename(file), name = base.slice(0, -ext.length);
  const info = { name, format: ext === '.vst3' ? 'VST3' : 'VST2', file, ext: ext.slice(1), arch: null, entry: null, valid: true, compatible: true, reason: '' };
  let st; try { st = fs.statSync(file); } catch (_) { return null; }
  if (st.isDirectory()) return { ...info, bundle: true };                          // macOS / Linux bundle: counted, not parsed
  info.size = st.size;
  const pe = inspectPe(file);
  if (!pe) {
    if (platform === 'win32' && ext !== '.vst') { info.valid = false; info.compatible = false; info.reason = 'not a Windows (PE) binary'; }
    return info;
  }
  info.arch = pe.arch;
  const v3 = pe.exports.find(e => VST3_ENTRIES.includes(e)), v2 = pe.exports.find(e => VST2_ENTRIES.includes(e));
  info.entry = v3 || v2 || null;
  if (ext === '.vst3') info.format = 'VST3';
  else info.format = 'VST2';
  if (!info.entry) { info.valid = false; info.compatible = false; info.reason = ext === '.vst3' ? 'no GetPluginFactory export: not a VST3' : 'no VSTPluginMain / main export: a plain DLL, not a VST2 plugin'; }
  else if (info.arch !== (HOST_ARCH[hostArch] || hostArch)) { info.compatible = false; info.reason = `${info.arch} plugin cannot be loaded by a ${HOST_ARCH[hostArch] || hostArch} host`; }
  return info;
}

function walk(dir, exts, max, depth = 0, out = []) {
  let entries = [];
  try { entries = fs.readdirSync(dir, { withFileTypes: true }); } catch (_) { return out; }
  for (const e of entries) {
    if (out.length >= max) return out;
    const p = path.join(dir, e.name), ext = path.extname(e.name).toLowerCase();
    if (exts.includes(ext)) out.push({ file: p, ext });
    else if (e.isDirectory() && depth < 2) walk(p, exts, max, depth + 1, out);
  }
  return out;
}

function scan({ platform = process.platform, env = process.env, home = os.homedir(), hostArch = process.arch, max = 500, hash = false } = {}) {
  const dirs = pluginDirs({ platform, env, home });
  const exts = platform === 'win32' ? ['.vst3', '.dll'] : ['.vst3', '.vst'];
  const seen = new Set(), plugins = [];
  for (const dir of dirs) {
    const isApp = dir === appPluginDir(env, home);
    for (const { file, ext } of walk(dir, exts, max)) {
      if (seen.has(file) || plugins.length >= max) continue;
      seen.add(file);
      // outside the app folder, a bare .dll is only a VST2 candidate when it lives in a VST folder (every dir here is one)
      const p = classify(file, ext, platform, hostArch);
      if (!p) continue;
      p.source = isApp ? 'app' : 'system';
      if (hash && isApp && p.size && p.size < 256 * 1024 * 1024 && !p.bundle) p.sha256 = crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
      plugins.push(p);
    }
  }
  return {
    appDir: appPluginDir(env, home), appDirExists: fs.existsSync(appPluginDir(env, home)),
    dirs: dirs.map(d => ({ path: d, exists: fs.existsSync(d) })),
    plugins, counts: { total: plugins.length, vst3: plugins.filter(p => p.format === 'VST3').length, vst2: plugins.filter(p => p.format === 'VST2').length, loadable: plugins.filter(p => p.valid && p.compatible).length, rejected: plugins.filter(p => !p.valid || !p.compatible).length },
  };
}

module.exports = { scan, inspectPe, classify, pluginDirs, appPluginDir };
