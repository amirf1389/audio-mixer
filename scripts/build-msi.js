'use strict';
// Builds a per-user Windows Installer package (.msi) from the same staged files as the NSIS .exe (see build-installer.js).
//   npm run build:msi            -> dist/AudioMixer-<version>.msi   (needs wixl: Linux "apt install wixl", from msitools)
// Not code-signed. Install:  msiexec /i AudioMixer-1.3.0.msi   (silent: /qn, no administrator rights)
// Optional features: ADDLOCAL=Main,Shortcuts,Autostart,Desktop  (default: Main,Shortcuts,Autostart)
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawnSync } = require('node:child_process');
const { buildInstaller, version4 } = require('./build-installer');

const ROOT = path.resolve(__dirname, '..');
const UPGRADE_CODE = '6F3C2B8E-5D41-4A7B-9C0E-2A1D7B64F3A9';   // fixed: lets a newer .msi replace an older one

const esc = s => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
// Stable GUID per path (component GUIDs must not change between versions).
function guid(seed) {
  const h = crypto.createHash('sha1').update('audio-mixer-msi:' + seed).digest();
  h[6] = (h[6] & 0x0f) | 0x50; h[8] = (h[8] & 0x3f) | 0x80;
  const x = h.subarray(0, 16).toString('hex');
  return `${x.slice(0, 8)}-${x.slice(8, 12)}-${x.slice(12, 16)}-${x.slice(16, 20)}-${x.slice(20, 32)}`.toUpperCase();
}
const idOf = (prefix, rel) => prefix + crypto.createHash('sha1').update(rel).digest('hex').slice(0, 20) + (rel.replace(/[^A-Za-z0-9]/g, '_').slice(-24));

function rtf(text) {
  const body = String(text).replace(/\\/g, '\\\\').replace(/[{}]/g, m => '\\' + m).replace(/\r?\n/g, '\\par\n').replace(/[^\x00-\x7f]/g, c => '\\u' + c.charCodeAt(0) + '?');
  return '{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Courier New;}}\\f0\\fs18 ' + body + '}';
}

// Directory tree + one component per file, all in feature Main.
function filesXml(stage) {
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
        out += `${indent}<Component Id="${cid}" Guid="${guid(r)}">\n${indent}  <File Id="${idOf('f', r)}" Name="${esc(e.name)}" Source="${esc(full)}" KeyPath="yes"/>\n${indent}</Component>\n`;
      }
    }
    return out;
  };
  return { xml: walk(stage, '', '          '), comps };
}

function wxs({ stage, version, vbs }) {
  const { xml, comps } = filesXml(stage);
  const sc = (id, name, args, desc) => `        <Shortcut Id="${id}" Name="${esc(name)}" Target="[INSTALLDIR]runtime\\node.exe" Arguments="${esc(args)}" WorkingDirectory="INSTALLDIR" Description="${esc(desc)}"/>\n`;
  return `<?xml version="1.0" encoding="UTF-8"?>
<Wix xmlns="http://schemas.microsoft.com/wix/2006/wi">
  <Product Id="*" Name="Audio Mixer Local Server" Language="1033" Version="${version}" Manufacturer="Audio Mixer" UpgradeCode="${UPGRADE_CODE}">
    <Package Description="Audio Mixer local system server" Manufacturer="Audio Mixer" InstallerVersion="200" Compressed="yes" InstallScope="perUser"/>
    <MajorUpgrade DowngradeErrorMessage="A newer version of Audio Mixer is already installed."/>
    <Media Id="1" Cabinet="audiomixer.cab" EmbedCab="yes"/>
    <Property Id="NSISINSTALL"><RegistrySearch Id="NsisKey" Root="HKCU" Key="Software\\AudioMixer" Name="InstallDir" Type="raw"/></Property>
    <Condition Message="Audio Mixer is already installed with the Setup .exe installer. Uninstall that first (Settings > Apps), then run this .msi.">NOT NSISINSTALL</Condition>

    <Directory Id="TARGETDIR" Name="SourceDir">
      <Directory Id="LocalAppDataFolder">
        <Directory Id="ProgramsDir" Name="Programs">
          <Directory Id="INSTALLDIR" Name="AudioMixer">
${xml}          </Directory>
        </Directory>
      </Directory>
      <Directory Id="ProgramMenuFolder">
        <Directory Id="MenuDir" Name="Audio Mixer"/>
      </Directory>
      <Directory Id="DesktopFolder"/>
      <Directory Id="PLUGINSDIR" Name="AudioMixerPlugins"/>
    </Directory>

    <DirectoryRef Id="INSTALLDIR">
      <Component Id="AutostartScript" Guid="${guid('autostart-vbs')}">
        <File Id="AutostartVbs" Name="start-server-hidden.vbs" Source="${esc(vbs)}" KeyPath="yes"/>
      </Component>
      <Component Id="AutostartRun" Guid="${guid('autostart-run')}">
        <RegistryValue Root="HKCU" Key="Software\\Microsoft\\Windows\\CurrentVersion\\Run" Name="AudioMixer" Type="string" Value="wscript.exe //B //Nologo &quot;[INSTALLDIR]start-server-hidden.vbs&quot;" KeyPath="yes"/>
      </Component>
      <Component Id="InstallKey" Guid="${guid('install-key')}">
        <RegistryValue Root="HKCU" Key="Software\\AudioMixer" Name="InstallDir" Type="string" Value="[INSTALLDIR]" KeyPath="yes"/>
      </Component>
    </DirectoryRef>

    <!-- VST3 / VST2 (.vst3, .dll) plugin folder in the user profile: outside the install folder, so plugins survive uninstall and upgrade -->
    <CustomAction Id="SetPluginsDir" Property="PLUGINSDIR" Value="[%USERPROFILE]\\AudioMixerPlugins"/>
    <InstallExecuteSequence><Custom Action="SetPluginsDir" Before="CostFinalize"/></InstallExecuteSequence>
    <InstallUISequence><Custom Action="SetPluginsDir" Before="CostFinalize"/></InstallUISequence>
    <DirectoryRef Id="PLUGINSDIR">
      <Component Id="PluginsFolder" Guid="${guid('plugins-folder')}">
        <CreateFolder/>
        <RegistryValue Root="HKCU" Key="Software\\AudioMixer" Name="PluginsFolder" Type="integer" Value="1" KeyPath="yes"/>
      </Component>
    </DirectoryRef>

    <DirectoryRef Id="MenuDir">
      <Component Id="MenuShortcuts" Guid="${guid('menu-shortcuts')}">
${sc('ScPc', 'Audio Mixer (PC mode)', '"[INSTALLDIR]client\\cli.js"', 'Start the local server and open the mixer')}${sc('ScServer', 'Audio Mixer local server only', '"[INSTALLDIR]bridge\\server.js"', 'Local server without opening the browser')}        <Shortcut Id="ScPlugins" Name="Plugins folder (VST3 and VST2)" Target="[PLUGINSDIR]" Description="Drop .vst3 and VST2 .dll plugins here"/>
${sc('ScVerify', 'Verify installation (security scan)', '"[INSTALLDIR]client\\cli.js" verify --scan --pause', 'Check the installed files and run a Defender scan')}        <RemoveFolder Id="RmMenu" On="uninstall"/>
        <RegistryValue Root="HKCU" Key="Software\\AudioMixer" Name="StartMenu" Type="integer" Value="1" KeyPath="yes"/>
      </Component>
    </DirectoryRef>

    <DirectoryRef Id="DesktopFolder">
      <Component Id="DesktopShortcut" Guid="${guid('desktop-shortcut')}">
${sc('ScDesk', 'Audio Mixer', '"[INSTALLDIR]client\\cli.js"', 'Start PC mode')}        <RegistryValue Root="HKCU" Key="Software\\AudioMixer" Name="Desktop" Type="integer" Value="1" KeyPath="yes"/>
      </Component>
    </DirectoryRef>

    <Feature Id="Main" Title="Audio Mixer and local server" Level="1" Absent="disallow">
${comps.map(c => `      <ComponentRef Id="${c}"/>\n`).join('')}      <ComponentRef Id="InstallKey"/><ComponentRef Id="PluginsFolder"/>
    </Feature>
    <Feature Id="Shortcuts" Title="Start Menu shortcuts" Level="1"><ComponentRef Id="MenuShortcuts"/></Feature>
    <Feature Id="Autostart" Title="Start the local server when I log in" Level="1"><ComponentRef Id="AutostartScript"/><ComponentRef Id="AutostartRun"/></Feature>
    <Feature Id="Desktop" Title="Desktop shortcut" Level="2"><ComponentRef Id="DesktopShortcut"/></Feature>

    <UIRef Id="WixUI_Minimal"/>
  </Product>
</Wix>
`;
}

async function buildMsi({ out = path.join(ROOT, 'dist'), runWixl = true, stageOpts = {} } = {}) {
  const st = await buildInstaller({ out, stageOnly: true, ...stageOpts });
  const work = path.join(path.resolve(out), 'msi');
  fs.mkdirSync(work, { recursive: true });
  const vbs = path.join(work, 'start-server-hidden.vbs');
  fs.writeFileSync(vbs, `' Starts the Audio Mixer local server hidden at login (installed by the .msi "Start the local server when I log in" feature).\r\n` +
    `Set fso = CreateObject("Scripting.FileSystemObject")\r\nd = fso.GetParentFolderName(WScript.ScriptFullName)\r\n` +
    `CreateObject("WScript.Shell").Run """" & d & "\\runtime\\node.exe"" """ & d & "\\bridge\\server.js""", 0, False\r\n`);
  const licenseRtf = path.join(work, 'License.rtf');
  fs.writeFileSync(licenseRtf, rtf(fs.readFileSync(path.join(st.stage, 'LICENSE'), 'utf8')));
  const version = st.version.replace(/[^\d.]/g, '').split('.').slice(0, 3).join('.');
  const wxsPath = path.join(work, 'audio-mixer.wxs');
  fs.writeFileSync(wxsPath, wxs({ stage: st.stage, version, vbs }));
  const msi = path.join(path.resolve(out), `AudioMixer-${st.version}.msi`);
  const result = { ...st, wxs: wxsPath, msi: null };
  if (!runWixl) return result;
  const r = spawnSync('wixl', ['-v', '--arch', 'x86', '--ext', 'ui', '-o', msi, wxsPath], { encoding: 'utf8', cwd: work });
  if (r.error && r.error.code === 'ENOENT') throw new Error('wixl not found. Install msitools/wixl (Linux: apt install wixl; macOS: brew install msitools); the WiX source is in ' + wxsPath);
  if (r.status !== 0) throw new Error('wixl failed:\n' + r.stdout + r.stderr);
  result.msi = msi;
  result.sha256 = crypto.createHash('sha256').update(fs.readFileSync(msi)).digest('hex');
  return result;
}

if (require.main === module) {
  buildMsi().then(r => {
    console.log(`MSI: ${r.msi}\nSHA-256: ${r.sha256}\nUnsigned: Windows SmartScreen will warn until it is code-signed.`);
  }).catch(e => { console.error('MSI build failed: ' + e.message); process.exit(1); });
}
module.exports = { buildMsi, wxs, guid, rtf, filesXml };
