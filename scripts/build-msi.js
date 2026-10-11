'use strict';
// Builds a Windows Installer package (.msi) from a staged install tree (see build-installer.js).
//   node scripts/build-msi.js [--arch x64|x86] [--scope machine|user]
//   machine (default): "C:\Program Files\Audio Mixer" for x64, "C:\Program Files (x86)\Audio Mixer" for x86; needs administrator rights, all users
//   user:              %LOCALAPPDATA%\Programs\AudioMixer, no administrator rights, current user
// Needs wixl (msitools): Linux "apt install wixl". Packages are signed by build-installers.js / sign.js.
// Install:  msiexec /i AudioMixer-1.4.0-x64.msi   (silent: /qn)   Uninstall: Settings > Apps > Audio Mixer, or msiexec /x <ProductCode>
// Features: ADDLOCAL=Main,Shortcuts,Tools,CommandLine,PluginHost,WinHelpers,Desktop  (default: all, the desktop shortcut too; the setup program has /nodesktop)
//   CommandLine: audio-mixer.exe on PATH (system PATH for the all-users package, the user's PATH for the per-user one)
//   PluginHost: native VST host (native/host), WinHelpers: native Windows audio device helpers (native/win); Tools: Start Menu shortcuts for license, plugins, drivers, update, doctor
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawnSync } = require('node:child_process');
const { buildInstaller } = require('./build-installer');

const ROOT = path.resolve(__dirname, '..');
const UPGRADE_CODES = {                       // fixed per flavour: lets a newer package replace an older one of the same flavour
  'x64-machine': '6F3C2B8E-5D41-4A7B-9C0E-2A1D7B64F3A9',
  'x86-machine': '2B9E1D47-8C3A-4F65-A1B0-7D5E3C9F8A12',
  'x64-user': '9A4D7E21-3B6C-4E58-8F0A-1C2D3E4F5A6B',
  'x86-user': '4C8F2A90-6D1E-47B3-B5A9-0E7D6C5B4A32',
};

const esc = s => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
// Stable GUID per seed (component GUIDs must not change between versions).
function guid(seed) {
  const h = crypto.createHash('sha1').update('audio-mixer-msi:' + seed).digest();
  h[6] = (h[6] & 0x0f) | 0x50; h[8] = (h[8] & 0x3f) | 0x80;
  const x = h.subarray(0, 16).toString('hex');
  return `${x.slice(0, 8)}-${x.slice(8, 12)}-${x.slice(12, 16)}-${x.slice(16, 20)}-${x.slice(20, 32)}`.toUpperCase();
}
const productCode = (arch, scope, version) => `{${guid(`product:${arch}:${scope}:${version}`)}}`;
const idOf = (prefix, rel) => prefix + crypto.createHash('sha1').update(rel).digest('hex').slice(0, 20) + (rel.replace(/[^A-Za-z0-9]/g, '_').slice(-24));

function rtf(text) {
  const body = String(text).replace(/\\/g, '\\\\').replace(/[{}]/g, m => '\\' + m).replace(/\r?\n/g, '\\par\n').replace(/[^\x00-\x7f]/g, c => '\\u' + c.charCodeAt(0) + '?');
  return '{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Courier New;}}\\f0\\fs18 ' + body + '}';
}

// Directory tree + one component per file, all in feature Main.
// Component ids whose relative path starts with one of the prefixes go to `groups[name]` instead of Main.
const FEATURE_PATHS = { PluginHost: 'native/host/', WinHelpers: 'native/win/' };
function filesXml(stage, win64 = false) {
  const comps = [], groups = { PluginHost: [], WinHelpers: [] };
  const walk = (dir, rel, indent) => {
    let out = '';
    const entries = fs.readdirSync(dir, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name));
    for (const e of entries) {
      const r = rel ? rel + '/' + e.name : e.name, full = path.join(dir, e.name);
      if (e.isDirectory()) {
        out += `${indent}<Directory Id="${idOf('d', r)}" Name="${esc(e.name)}">\n${walk(full, r, indent + '  ')}${indent}</Directory>\n`;
      } else {
        const cid = idOf('c', r);
        const g = Object.keys(FEATURE_PATHS).find(k => r.startsWith(FEATURE_PATHS[k]));
        (g ? groups[g] : comps).push(cid);
        out += `${indent}<Component Id="${cid}" Guid="${guid(r)}"${win64 ? ' Win64="yes"' : ''}>\n${indent}  <File Id="${idOf('f', r)}" Name="${esc(e.name)}" Source="${esc(full)}" KeyPath="yes"/>\n${indent}</Component>\n`;
      }
    }
    return out;
  };
  return { xml: walk(stage, '', '            '), comps, groups };
}

function wxs({ stage, version, arch = 'x64', scope = 'machine' }) {
  if (!['x64', 'x86'].includes(arch) || !['machine', 'user'].includes(scope)) throw new Error('bad arch / scope');
  const win64 = arch === 'x64', machine = scope === 'machine', root = machine ? 'HKLM' : 'HKCU';
  const { xml, comps, groups } = filesXml(stage, win64);
  const w64 = win64 ? ' Win64="yes"' : '';
  // start-up screen: the signed native launcher shows the boot animation while the server starts, then opens the mixer in the browser
  const hasIcon = fs.existsSync(path.join(stage, 'AudioMixerServer.exe'));   // the launcher carries the application icon
  const scBoot = (id, name, desc) => `        <Shortcut Id="${id}" Name="${esc(name)}" Target="[INSTALLDIR]AudioMixerServer.exe" Arguments="/open" WorkingDirectory="INSTALLDIR" Description="${esc(desc)}"${hasIcon ? ' Icon="AudioMixer.exe"' : ''}/>\n`;
  const sc = (id, name, args, desc) => `        <Shortcut Id="${id}" Name="${esc(name)}" Target="[INSTALLDIR]runtime\\node.exe" Arguments="${esc(args)}" WorkingDirectory="INSTALLDIR" Description="${esc(desc)}"/>\n`;
  const code = productCode(arch, scope, version);
  // Program Files (64-bit) for x64, Program Files (x86) for x86; or the user's own folder for the no-administrator flavour
  const installDirs = machine
    ? `      <Directory Id="${win64 ? 'ProgramFiles64Folder' : 'ProgramFilesFolder'}">\n        <Directory Id="INSTALLDIR" Name="Audio Mixer">\n${xml}        </Directory>\n      </Directory>`
    : `      <Directory Id="LocalAppDataFolder">\n        <Directory Id="ProgramsDir" Name="Programs">\n          <Directory Id="INSTALLDIR" Name="AudioMixer">\n${xml}          </Directory>\n        </Directory>\n      </Directory>`;
  return `<?xml version="1.0" encoding="UTF-8"?>
<Wix xmlns="http://schemas.microsoft.com/wix/2006/wi">
  <Product Id="${code}" Name="Audio Mixer" Language="1033" Version="${version}" Manufacturer="Audio Mixer" UpgradeCode="${UPGRADE_CODES[arch + '-' + scope]}">
    <Package Description="Audio Mixer local system server and mixer" Manufacturer="Audio Mixer" InstallerVersion="200" Compressed="yes" InstallScope="${machine ? 'perMachine' : 'perUser'}"/>
    <MajorUpgrade DowngradeErrorMessage="A newer version of Audio Mixer is already installed."/>
    <Media Id="1" Cabinet="audiomixer.cab" EmbedCab="yes"/>
    <!-- Settings > Apps (Add or remove programs) entry: name, version, publisher and a working Uninstall button come with the package -->
${hasIcon ? `    <Icon Id="AudioMixer.exe" SourceFile="${esc(path.join(stage, 'AudioMixerServer.exe'))}"/>\n    <Property Id="ARPPRODUCTICON" Value="AudioMixer.exe"/>   <!-- the icon of the entry in Settings > Apps -->\n` : ''}    <Property Id="ARPCOMMENTS" Value="Virtual mixing console with a local Node.js server for ASIO / WASAPI audio"/>
    <Property Id="ARPURLINFOABOUT" Value="https://github.com/amirf1389/audio-mixer"/>
    <Property Id="ARPHELPLINK" Value="https://github.com/amirf1389/audio-mixer/issues"/>

    <Directory Id="TARGETDIR" Name="SourceDir">
${installDirs}
      <Directory Id="ProgramMenuFolder">
        <Directory Id="MenuDir" Name="Audio Mixer"/>
      </Directory>
      <Directory Id="DesktopFolder"/>
    </Directory>

    <DirectoryRef Id="INSTALLDIR">
      <Component Id="InstallKey" Guid="${guid('install-key:' + scope)}"${w64}>
        <RegistryValue Root="${root}" Key="Software\\Audio Mixer" Name="InstallDir" Type="string" Value="[INSTALLDIR]" KeyPath="yes"/>
        <RegistryValue Root="${root}" Key="Software\\Audio Mixer" Name="UninstallCode" Type="string" Value="[ProductCode]"/>   <!-- "audio-mixer uninstall" finds the package with it -->
      </Component>
      <!-- the "audio-mixer" command (audio-mixer.exe in the install folder) on PATH: the Environment table row is added after wixl (see addPathEntry) -->
      <Component Id="CommandPath" Guid="${guid('command-path:' + scope)}"${w64}>
        <RegistryValue Root="${root}" Key="Software\\Audio Mixer" Name="CommandLine" Type="integer" Value="1" KeyPath="yes"/>
      </Component>
    </DirectoryRef>

    <DirectoryRef Id="MenuDir">
      <Component Id="MenuShortcuts" Guid="${guid('menu-shortcuts:' + scope)}"${w64}>
${scBoot('ScPc', 'Audio Mixer (PC mode)', 'Start the local server (boot screen) and open the mixer')}${sc('ScServer', 'Audio Mixer local server only', '"[INSTALLDIR]bridge\\server.js"', 'Local server without opening the browser')}${sc('ScVerify', 'Verify installation (file check)', '"[INSTALLDIR]client\\cli.js" verify --pause', 'Check the installed files against their recorded SHA-256 hashes')}        <!-- VST3 / VST2 (.vst3, .dll) plugins live in the user's own folder: the launcher creates and opens it -->
        <Shortcut Id="ScPlugins" Name="Plugins folder (VST3 and VST2)" Target="[INSTALLDIR]AudioMixerServer.exe" Arguments="/plugins" Description="Drop .vst3 and VST2 .dll plugins here"/>
        <Shortcut Id="ScUninstall" Name="Uninstall Audio Mixer" Target="[SystemFolder]msiexec.exe" Arguments="/x ${code}" Description="Remove Audio Mixer (also in Settings > Apps)"/>
        <RemoveFolder Id="RmMenu" On="uninstall"/>
        <RegistryValue Root="${root}" Key="Software\\Audio Mixer" Name="StartMenu" Type="integer" Value="1" KeyPath="yes"/>
      </Component>
    </DirectoryRef>

    <!-- "Audio Mixer" itself in the Start Menu list and in the Start search (the folder below holds the tools) -->
    <DirectoryRef Id="ProgramMenuFolder">
      <Component Id="StartRootShortcut" Guid="${guid('start-root:' + scope)}"${w64}>
${scBoot('ScStart', 'Audio Mixer', 'Start Audio Mixer (boot screen, then the mixer in your browser)')}        <RegistryValue Root="${root}" Key="Software\\Audio Mixer" Name="StartRoot" Type="integer" Value="1" KeyPath="yes"/>
      </Component>
    </DirectoryRef>

    <DirectoryRef Id="MenuDir">
      <Component Id="ToolShortcuts" Guid="${guid('tool-shortcuts:' + scope)}"${w64}>
${sc('ScLicense', 'License key and machine ID', '"[INSTALLDIR]client\\cli.js" license --pause', 'Show the license plan and this PC\'s machine ID; activate with: license activate KEY')}${sc('ScPluginList', 'List installed plugins', '"[INSTALLDIR]client\\cli.js" plugins --pause', 'List the VST3 and VST2 plugins the mixer finds')}${sc('ScDrivers', 'Audio drivers (ASIO, WASAPI)', '"[INSTALLDIR]client\\cli.js" drivers --pause', 'List the official audio drivers for this PC')}${sc('ScUpdate', 'Check for updates', '"[INSTALLDIR]client\\cli.js" update --pause', 'Check the signed update manifest (nothing is installed automatically)')}${sc('ScDoctor', 'Audio Mixer diagnostics', '"[INSTALLDIR]client\\cli.js" doctor --pause', 'Check Node.js, ports, audio engine and ASIO drivers')}        <RemoveFolder Id="RmMenuTools" On="uninstall"/>
        <RegistryValue Root="${root}" Key="Software\\Audio Mixer" Name="ToolShortcuts" Type="integer" Value="1" KeyPath="yes"/>
      </Component>
    </DirectoryRef>

    <DirectoryRef Id="DesktopFolder">
      <Component Id="DesktopShortcut" Guid="${guid('desktop-shortcut:' + scope)}"${w64}>
${scBoot('ScDesk', 'Audio Mixer', 'Start PC mode (boot screen)')}        <RegistryValue Root="${root}" Key="Software\\Audio Mixer" Name="Desktop" Type="integer" Value="1" KeyPath="yes"/>
      </Component>
    </DirectoryRef>

    <Feature Id="Main" Title="Audio Mixer and local server" Level="1" Absent="disallow">
${comps.map(c => `      <ComponentRef Id="${c}"/>\n`).join('')}      <ComponentRef Id="InstallKey"/>
    </Feature>
    <Feature Id="Shortcuts" Title="Start Menu shortcuts" Level="1"><ComponentRef Id="MenuShortcuts"/><ComponentRef Id="StartRootShortcut"/></Feature>
    <Feature Id="Tools" Title="Start Menu tools (license, plugins, drivers, update, diagnostics)" Level="1"><ComponentRef Id="ToolShortcuts"/></Feature>
    <Feature Id="PluginHost" Title="VST3 / VST2 plugin host (insert effects)" Level="1">
${groups.PluginHost.map(c => `      <ComponentRef Id="${c}"/>\n`).join('')}    </Feature>
    <Feature Id="WinHelpers" Title="Native Windows audio device helpers" Level="1">
${groups.WinHelpers.map(c => `      <ComponentRef Id="${c}"/>\n`).join('')}    </Feature>
    <Feature Id="CommandLine" Title="audio-mixer command on PATH (doctor, npm, setup, uninstall ...)" Level="1"><ComponentRef Id="CommandPath"/></Feature>
    <Feature Id="Desktop" Title="Desktop shortcut" Level="1"><ComponentRef Id="DesktopShortcut"/></Feature>

    <UIRef Id="WixUI_Minimal"/>
  </Product>
</Wix>
`;
}

// wixl has no <Environment>: the PATH entry for audio-mixer.exe is written into the finished package (Environment table, owned by component
// CommandPath, so it follows the CommandLine feature; "=*" = set on install, remove on uninstall; "[~];dir" appends to the existing PATH) together with the
// two standard actions that apply it. Needs msidump / msibuild (msitools, installed with wixl).
function addPathEntry(msi, work, component = 'CommandPath') {
  const dir = path.join(work, 'env'); fs.rmSync(dir, { recursive: true, force: true }); fs.mkdirSync(dir, { recursive: true });
  const run = (cmd, args) => { const r = spawnSync(cmd, args, { encoding: 'utf8', cwd: dir }); if (r.error && r.error.code === 'ENOENT') throw new Error(cmd + ' not found (msitools)'); if (r.status !== 0) throw new Error(cmd + ' failed:\n' + r.stdout + r.stderr); };
  run('msidump', ['-t', msi]);
  const seqFile = path.join(dir, 'InstallExecuteSequence.idt');
  let seq = fs.readFileSync(seqFile, 'utf8').replace(/\r?\n$/, '');
  if (!/^WriteEnvironmentStrings\t/m.test(seq)) seq += '\nRemoveEnvironmentStrings\t\t3300\nWriteEnvironmentStrings\t\t5200';
  fs.writeFileSync(seqFile, seq + '\n');
  fs.writeFileSync(path.join(dir, 'Environment.idt'), 'Environment\tName\tValue\tComponent_\ns72\tl255\tL255\ts72\nEnvironment\tEnvironment\nPathAudioMixer\t=*PATH\t[~];[INSTALLDIR]\t' + component + '\n');
  run('msibuild', [msi, '-i', 'InstallExecuteSequence.idt', 'Environment.idt']);
}

// `staged` = result of buildInstaller (so several installers can share one staging folder)
async function buildMsi({ out = path.join(ROOT, 'dist'), arch = 'x64', scope = 'machine', runWixl = true, stageOpts = {}, staged = null } = {}) {
  const st = staged || await buildInstaller({ out, arch, ...stageOpts });
  const work = path.join(path.resolve(out), 'msi', `${arch}-${scope}`);
  fs.mkdirSync(work, { recursive: true });
  fs.writeFileSync(path.join(work, 'License.rtf'), rtf(fs.readFileSync(path.join(st.stage, 'LICENSE'), 'utf8')));   // the wixl UI reads License.rtf from the working directory
  const version = st.version.replace(/[^\d.]/g, '').split('.').slice(0, 3).join('.');
  const wxsPath = path.join(work, 'audio-mixer.wxs');
  fs.writeFileSync(wxsPath, wxs({ stage: st.stage, version, arch, scope }));
  const msi = path.join(path.resolve(out), `AudioMixer-${st.version}-${arch}${scope === 'user' ? '-user' : ''}.msi`);
  const result = { ...st, wxs: wxsPath, msi: null, scope, productCode: productCode(arch, scope, version) };
  if (!runWixl) return result;
  const r = spawnSync('wixl', ['-v', '--arch', arch, '--ext', 'ui', '-o', msi, wxsPath], { encoding: 'utf8', cwd: work });
  if (r.error && r.error.code === 'ENOENT') throw new Error('wixl not found. Install msitools/wixl (Linux: apt install wixl; macOS: brew install msitools); the WiX source is in ' + wxsPath);
  if (r.status !== 0) throw new Error('wixl failed:\n' + r.stdout + r.stderr);
  addPathEntry(msi, work);
  result.msi = msi;
  result.sha256 = crypto.createHash('sha256').update(fs.readFileSync(msi)).digest('hex');
  return result;
}

if (require.main === module) {
  const a = process.argv.slice(2), arg = (n, d) => { const i = a.indexOf(n); return i >= 0 ? a[i + 1] : d; };
  buildMsi({ arch: arg('--arch', 'x64'), scope: arg('--scope', 'machine') }).then(r => {
    console.log(`MSI: ${r.msi}\nProduct code: ${r.productCode}\nSHA-256: ${r.sha256}\nUnsigned (see build-installers.js for signing).`);
  }).catch(e => { console.error('MSI build failed: ' + e.message); process.exit(1); });
}
module.exports = { addPathEntry, buildMsi, wxs, guid, rtf, filesXml, productCode, UPGRADE_CODES };
