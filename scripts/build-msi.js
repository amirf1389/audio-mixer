'use strict';
// Builds a Windows Installer package (.msi) from a staged install tree (see build-installer.js).
//   node scripts/build-msi.js [--arch x64|x86] [--scope machine|user]
//   machine (default): "C:\Program Files\Audio Mixer" for x64, "C:\Program Files (x86)\Audio Mixer" for x86; needs administrator rights, all users
//   user:              %LOCALAPPDATA%\Programs\AudioMixer, no administrator rights, current user
// Needs wixl (msitools): Linux "apt install wixl". Packages are signed by build-installers.js / sign.js.
// Install:  msiexec /i AudioMixer-1.4.0-x64.msi   (silent: /qn)   Uninstall: Settings > Apps > Audio Mixer, or msiexec /x <ProductCode>
// Features: ADDLOCAL=Main,Shortcuts,Autostart,Desktop  (default: Main,Shortcuts,Autostart)
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
function filesXml(stage, win64 = false) {
  const comps = [];
  const walk = (dir, rel, indent) => {
    let out = '';
    const entries = fs.readdirSync(dir, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name));
    for (const e of entries) {
      const r = rel ? rel + '/' + e.name : e.name, full = path.join(dir, e.name);
      if (e.isDirectory()) {
        out += `${indent}<Directory Id="${idOf('d', r)}" Name="${esc(e.name)}">\n${walk(full, r, indent + '  ')}${indent}</Directory>\n`;
      } else {
        const cid = idOf('c', r);
        comps.push(cid);
        out += `${indent}<Component Id="${cid}" Guid="${guid(r)}"${win64 ? ' Win64="yes"' : ''}>\n${indent}  <File Id="${idOf('f', r)}" Name="${esc(e.name)}" Source="${esc(full)}" KeyPath="yes"/>\n${indent}</Component>\n`;
      }
    }
    return out;
  };
  return { xml: walk(stage, '', '            '), comps };
}

function wxs({ stage, version, vbs, arch = 'x64', scope = 'machine' }) {
  if (!['x64', 'x86'].includes(arch) || !['machine', 'user'].includes(scope)) throw new Error('bad arch / scope');
  const win64 = arch === 'x64', machine = scope === 'machine', root = machine ? 'HKLM' : 'HKCU';
  const { xml, comps } = filesXml(stage, win64);
  const w64 = win64 ? ' Win64="yes"' : '';
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
    <Property Id="ARPCOMMENTS" Value="Virtual mixing console with a local Node.js server for ASIO / WASAPI audio"/>
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
      <Component Id="AutostartScript" Guid="${guid('autostart-vbs')}"${w64}>
        <File Id="AutostartVbs" Name="start-server-hidden.vbs" Source="${esc(vbs)}" KeyPath="yes"/>
      </Component>
      <Component Id="AutostartRun" Guid="${guid('autostart-run:' + scope)}"${w64}>
        <RegistryValue Root="${root}" Key="Software\\Microsoft\\Windows\\CurrentVersion\\Run" Name="AudioMixer" Type="string" Value="wscript.exe //B //Nologo &quot;[INSTALLDIR]start-server-hidden.vbs&quot;" KeyPath="yes"/>
      </Component>
      <Component Id="InstallKey" Guid="${guid('install-key:' + scope)}"${w64}>
        <RegistryValue Root="${root}" Key="Software\\Audio Mixer" Name="InstallDir" Type="string" Value="[INSTALLDIR]" KeyPath="yes"/>
      </Component>
    </DirectoryRef>

    <DirectoryRef Id="MenuDir">
      <Component Id="MenuShortcuts" Guid="${guid('menu-shortcuts:' + scope)}"${w64}>
${sc('ScPc', 'Audio Mixer (PC mode)', '"[INSTALLDIR]client\\cli.js"', 'Start the local server and open the mixer')}${sc('ScServer', 'Audio Mixer local server only', '"[INSTALLDIR]bridge\\server.js"', 'Local server without opening the browser')}${sc('ScVerify', 'Verify installation (security scan)', '"[INSTALLDIR]client\\cli.js" verify --scan --pause', 'Check the installed files and run a Defender scan')}        <!-- VST3 / VST2 (.vst3, .dll) plugins live in the user's own folder: the shortcut creates it on first use -->
        <Shortcut Id="ScPlugins" Name="Plugins folder (VST3 and VST2)" Target="[SystemFolder]cmd.exe" Arguments="/c if not exist &quot;%USERPROFILE%\\AudioMixerPlugins&quot; mkdir &quot;%USERPROFILE%\\AudioMixerPlugins&quot; &amp; start &quot;&quot; &quot;%USERPROFILE%\\AudioMixerPlugins&quot;" Description="Drop .vst3 and VST2 .dll plugins here"/>
        <Shortcut Id="ScUninstall" Name="Uninstall Audio Mixer" Target="[SystemFolder]msiexec.exe" Arguments="/x ${code}" Description="Remove Audio Mixer (also in Settings > Apps)"/>
        <RemoveFolder Id="RmMenu" On="uninstall"/>
        <RegistryValue Root="${root}" Key="Software\\Audio Mixer" Name="StartMenu" Type="integer" Value="1" KeyPath="yes"/>
      </Component>
    </DirectoryRef>

    <DirectoryRef Id="DesktopFolder">
      <Component Id="DesktopShortcut" Guid="${guid('desktop-shortcut:' + scope)}"${w64}>
${sc('ScDesk', 'Audio Mixer', '"[INSTALLDIR]client\\cli.js"', 'Start PC mode')}        <RegistryValue Root="${root}" Key="Software\\Audio Mixer" Name="Desktop" Type="integer" Value="1" KeyPath="yes"/>
      </Component>
    </DirectoryRef>

    <Feature Id="Main" Title="Audio Mixer and local server" Level="1" Absent="disallow">
${comps.map(c => `      <ComponentRef Id="${c}"/>\n`).join('')}      <ComponentRef Id="InstallKey"/>
    </Feature>
    <Feature Id="Shortcuts" Title="Start Menu shortcuts" Level="1"><ComponentRef Id="MenuShortcuts"/></Feature>
    <Feature Id="Autostart" Title="Start the local server when I log in" Level="1"><ComponentRef Id="AutostartScript"/><ComponentRef Id="AutostartRun"/></Feature>
    <Feature Id="Desktop" Title="Desktop shortcut" Level="2"><ComponentRef Id="DesktopShortcut"/></Feature>

    <UIRef Id="WixUI_Minimal"/>
  </Product>
</Wix>
`;
}

// `staged` = result of buildInstaller (so several installers can share one staging folder)
async function buildMsi({ out = path.join(ROOT, 'dist'), arch = 'x64', scope = 'machine', runWixl = true, stageOpts = {}, staged = null } = {}) {
  const st = staged || await buildInstaller({ out, arch, ...stageOpts });
  const work = path.join(path.resolve(out), 'msi', `${arch}-${scope}`);
  fs.mkdirSync(work, { recursive: true });
  const vbs = path.join(work, 'start-server-hidden.vbs');
  fs.writeFileSync(vbs, `' Starts the Audio Mixer local server hidden at login (installed by the .msi "Start the local server when I log in" feature).\r\n` +
    `Set fso = CreateObject("Scripting.FileSystemObject")\r\nd = fso.GetParentFolderName(WScript.ScriptFullName)\r\n` +
    `CreateObject("WScript.Shell").Run """" & d & "\\runtime\\node.exe"" """ & d & "\\bridge\\server.js""", 0, False\r\n`);
  fs.writeFileSync(path.join(work, 'License.rtf'), rtf(fs.readFileSync(path.join(st.stage, 'LICENSE'), 'utf8')));   // the wixl UI reads License.rtf from the working directory
  const version = st.version.replace(/[^\d.]/g, '').split('.').slice(0, 3).join('.');
  const wxsPath = path.join(work, 'audio-mixer.wxs');
  fs.writeFileSync(wxsPath, wxs({ stage: st.stage, version, vbs, arch, scope }));
  const msi = path.join(path.resolve(out), `AudioMixer-${st.version}-${arch}${scope === 'user' ? '-user' : ''}.msi`);
  const result = { ...st, wxs: wxsPath, msi: null, scope, productCode: productCode(arch, scope, version) };
  if (!runWixl) return result;
  const r = spawnSync('wixl', ['-v', '--arch', arch, '--ext', 'ui', '-o', msi, wxsPath], { encoding: 'utf8', cwd: work });
  if (r.error && r.error.code === 'ENOENT') throw new Error('wixl not found. Install msitools/wixl (Linux: apt install wixl; macOS: brew install msitools); the WiX source is in ' + wxsPath);
  if (r.status !== 0) throw new Error('wixl failed:\n' + r.stdout + r.stderr);
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
module.exports = { buildMsi, wxs, guid, rtf, filesXml, productCode, UPGRADE_CODES };
