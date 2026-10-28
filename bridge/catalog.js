'use strict';
// Catalog of official audio drivers / audio stacks per OS, with install detection. Nothing is downloaded or executed by this module (the in-app
// driver downloader was removed in 1.5.2.0): every entry links to the vendor's official site or shows the package-manager command.

// kind: built-in | asio | sdk | vendor | virtual | package | native
const CATALOG = [
  // ── Windows ──
  { id: 'wasapi', os: ['win32'], kind: 'built-in', name: 'WASAPI (Shared / Exclusive / Loopback)', vendor: 'Microsoft Windows Core Audio',
    note: 'Part of Windows. Exclusive mode and event-driven buffers give low latency without extra drivers.',
    url: 'https://learn.microsoft.com/windows/win32/coreaudio/wasapi', detect: { drivers: ['wasapi-shared', 'wasapi-excl'] } },
  { id: 'directsound', os: ['win32'], kind: 'built-in', name: 'DirectSound / WDM-KS / MME', vendor: 'Microsoft Windows',
    note: 'Legacy and kernel-streaming host APIs, installed with Windows.',
    url: 'https://learn.microsoft.com/windows-hardware/drivers/audio/', detect: { drivers: ['directsound', 'wdmks', 'mme'] } },
  { id: 'flexasio', os: ['win32'], kind: 'asio', name: 'FlexASIO', vendor: 'Etienne Dechamps (open source)',
    note: 'ASIO driver on top of WASAPI / PortAudio: ASIO for any sound card (MIT). Get it from the project\'s official GitHub release page, or install it with: node scripts/install-flexasio.js (verified download, silent install).',
    url: 'https://github.com/dechamps/FlexASIO/releases', detect: { drivers: ['flexasio'], asio: /flexasio/i } },
  { id: 'asio4all', os: ['win32'], kind: 'asio', name: 'ASIO4ALL (Windows ASIO driver)', vendor: 'Michael Tippach',
    note: 'Universal ASIO driver for WDM hardware, found through the Windows ASIO registry (HKLM\\SOFTWARE\\ASIO) and opened by the native ASIO path. Detection uses the registry; the official site (asio4all.org) is linked for installing it.',
    url: 'https://www.asio4all.org/', detect: { drivers: ['asio4all'], asio: /asio4all/i } },
  { id: 'steinberg-asio', os: ['win32', 'darwin'], kind: 'sdk', name: 'Steinberg ASIO (SDK and generic driver)', vendor: 'Steinberg Media Technologies',
    note: 'ASIO is a Steinberg specification; drivers are written by the hardware vendor.',
    url: 'https://www.steinberg.net/developers/', detect: { asioAny: true } },
  { id: 'focusrite', os: ['win32', 'darwin'], kind: 'vendor', name: 'Focusrite USB ASIO / Control', vendor: 'Focusrite',
    note: 'Scarlett / Clarett / Vocaster drivers.', url: 'https://focusrite.com/downloads', detect: { asio: /focusrite|scarlett|clarett/i } },
  { id: 'rme', os: ['win32', 'darwin'], kind: 'vendor', name: 'RME Fireface / Babyface / HDSP drivers', vendor: 'RME Audio',
    note: 'ASIO and Core Audio drivers.', url: 'https://rme-audio.de/downloads.html', detect: { asio: /rme|fireface|babyface|hdsp|hammerfall/i } },
  { id: 'motu', os: ['win32', 'darwin'], kind: 'vendor', name: 'MOTU Pro Audio drivers', vendor: 'MOTU',
    note: 'Interface installer (ASIO and Core Audio).', url: 'https://motu.com/download', detect: { asio: /motu/i } },
  { id: 'uad', os: ['win32', 'darwin'], kind: 'vendor', name: 'Universal Audio Apollo / Volt', vendor: 'Universal Audio',
    note: 'Console / UAD software including drivers.', url: 'https://www.uaudio.com/downloads', detect: { asio: /apollo|universal audio|uad/i } },
  { id: 'behringer', os: ['win32', 'darwin'], kind: 'vendor', name: 'Behringer / Music Tribe WING and X-USB', vendor: 'Music Tribe',
    note: 'WING / X-USB / UMC drivers. Search the product on the site.', url: 'https://www.behringer.com/', detect: { asio: /wing|behringer|x-?usb|umc/i } },
  { id: 'harman', os: ['win32', 'darwin'], kind: 'vendor', name: 'Harman audio interface drivers', vendor: 'Harman',
    note: 'Interface drivers (ASIO and Core Audio). Find the product on the official site.', url: 'https://www.harman.com/', detect: { asio: /harman|jbl|akg|dbx|lexicon/i } },
  { id: 'roland', os: ['win32', 'darwin'], kind: 'vendor', name: 'Roland / BOSS audio interface drivers', vendor: 'Roland',
    note: 'UA / Rubix / Quad-Capture drivers (ASIO and Core Audio). Find the product on the official site.', url: 'https://www.roland.com/', detect: { asio: /roland|boss|rubix|quad.?capture|\bUA-/i } },
  { id: 'yamaha', os: ['win32', 'darwin'], kind: 'vendor', name: 'Yamaha audio interface drivers', vendor: 'Yamaha',
    note: 'Interface drivers (ASIO and Core Audio). Find the product on the official site.', url: 'https://www.yamaha.com/', detect: { asio: /yamaha|\bAG\d|\bZG\d/i } },
  { id: 'steinberg-ui', os: ['win32', 'darwin'], kind: 'vendor', name: 'Steinberg UR audio interface drivers', vendor: 'Steinberg',
    note: 'Interface drivers (ASIO and Core Audio). Find the product on the official site.', url: 'https://www.steinberg.net/', detect: { asio: /steinberg|\bUR\d{1,2}\b|\bUR-?(RT|C)\b/i } },
  { id: 'presonus', os: ['win32', 'darwin'], kind: 'vendor', name: 'PreSonus audio interface drivers', vendor: 'PreSonus',
    note: 'Interface drivers (ASIO and Core Audio). Find the product on the official site.', url: 'https://www.presonus.com/', detect: { asio: /presonus|studio\s?\d+usb|quantum|audiobox/i } },
  { id: 'tascam', os: ['win32', 'darwin'], kind: 'vendor', name: 'TASCAM audio interface drivers', vendor: 'TASCAM',
    note: 'Interface drivers (ASIO and Core Audio). Find the product on the official site.', url: 'https://tascam.com/', detect: { asio: /tascam|\bUS-\d/i } },
  { id: 'zoom', os: ['win32', 'darwin'], kind: 'vendor', name: 'Zoom audio interface drivers', vendor: 'Zoom',
    note: 'Interface drivers (ASIO and Core Audio). Find the product on the official site.', url: 'https://zoomcorp.com/', detect: { asio: /\bzoom\b|\bUAC-|\bU-\d{2}\b/i } },
  { id: 'm-audio', os: ['win32', 'darwin'], kind: 'vendor', name: 'M-Audio interface drivers', vendor: 'M-Audio',
    note: 'Interface drivers (ASIO and Core Audio). Find the product on the official site.', url: 'https://m-audio.com/', detect: { asio: /m-?audio|\bM-Track|\bFast ?Track|\bHDH|\bAir ?192/i } },
  { id: 'native-instruments', os: ['win32', 'darwin'], kind: 'vendor', name: 'Native Instruments Komplete Audio drivers', vendor: 'Native Instruments',
    note: 'Interface drivers (ASIO and Core Audio). Find the product on the official site.', url: 'https://www.native-instruments.com/', detect: { asio: /native instruments|komplete audio|\bKA\d/i } },
  { id: 'apogee', os: ['win32', 'darwin'], kind: 'vendor', name: 'Apogee interface drivers', vendor: 'Apogee',
    note: 'Interface drivers (ASIO and Core Audio). Find the product on the official site.', url: 'https://apogeedigital.com/', detect: { asio: /apogee|\bSymphony|\bDuet\b|\bOne\b.*apogee/i } },
  { id: 'audient', os: ['win32', 'darwin'], kind: 'vendor', name: 'Audient interface drivers', vendor: 'Audient',
    note: 'Interface drivers (ASIO and Core Audio). Find the product on the official site.', url: 'https://audient.com/', detect: { asio: /audient|\bid\d{1,2}\b|\bevo\b/i } },
  { id: 'arturia', os: ['win32', 'darwin'], kind: 'vendor', name: 'Arturia audio interface drivers', vendor: 'Arturia',
    note: 'Interface drivers (ASIO and Core Audio). Find the product on the official site.', url: 'https://www.arturia.com/', detect: { asio: /arturia|\bMiniFuse/i } },
  { id: 'ssl', os: ['win32', 'darwin'], kind: 'vendor', name: 'Solid State Logic SSL 2 / SSL 12', vendor: 'Solid State Logic',
    note: 'USB audio interface drivers.', url: 'https://solidstatelogic.com/', detect: { asio: /ssl|solid state/i } },
  { id: 'vb-cable', os: ['win32'], kind: 'virtual', name: 'VB-CABLE virtual audio device', vendor: 'VB-Audio',
    note: 'Virtual cable for routing between applications.', url: 'https://vb-audio.com/Cable/', detect: {} },
  { id: 'voicemeeter', os: ['win32'], kind: 'virtual', name: 'Voicemeeter (virtual mixer, ASIO)', vendor: 'VB-Audio',
    note: 'Virtual mixer with ASIO and WDM devices.', url: 'https://vb-audio.com/Voicemeeter/', detect: { asio: /voicemeeter/i } },
  // ── macOS ──
  { id: 'coreaudio', os: ['darwin'], kind: 'built-in', name: 'Core Audio HAL', vendor: 'Apple',
    note: 'Part of macOS. Class-compliant interfaces need no driver; AudioDriverKit drivers come from the hardware vendor.',
    url: 'https://developer.apple.com/documentation/audiodriverkit', detect: { drivers: ['coreaudio'] } },
  { id: 'blackhole', os: ['darwin'], kind: 'virtual', name: 'BlackHole virtual audio loopback', vendor: 'Existential Audio',
    note: 'Open source virtual driver for routing audio between applications.', url: 'https://existential.audio/blackhole/', detect: {} },
  // ── Linux ──
  { id: 'alsa', os: ['linux'], kind: 'package', name: 'ALSA (kernel drivers and utilities)', vendor: 'ALSA Project',
    note: 'The Linux kernel audio layer; install the utilities to inspect devices.', url: 'https://www.alsa-project.org/',
    detect: { drivers: ['alsa'] }, install: { apt: 'sudo apt install alsa-utils', dnf: 'sudo dnf install alsa-utils', pacman: 'sudo pacman -S alsa-utils' } },
  { id: 'pipewire', os: ['linux'], kind: 'package', name: 'PipeWire (PulseAudio and JACK compatible)', vendor: 'PipeWire Project',
    note: 'Modern low-latency audio server with PulseAudio, JACK and ALSA client compatibility.', url: 'https://pipewire.org/',
    detect: { drivers: ['pipewire'] }, install: { apt: 'sudo apt install pipewire pipewire-pulse pipewire-jack wireplumber', dnf: 'sudo dnf install pipewire pipewire-pulseaudio pipewire-jack-audio-connection-kit wireplumber', pacman: 'sudo pacman -S pipewire pipewire-pulse pipewire-jack wireplumber' } },
  { id: 'jack', os: ['linux', 'darwin'], kind: 'package', name: 'JACK Audio Connection Kit', vendor: 'JACK Audio',
    note: 'Professional low-latency audio server.', url: 'https://jackaudio.org/',
    detect: { drivers: ['jack'] }, install: { apt: 'sudo apt install jackd2', dnf: 'sudo dnf install jack-audio-connection-kit', pacman: 'sudo pacman -S jack2' } },
  // ── Android / iOS ──
  { id: 'aaudio', os: ['android'], kind: 'built-in', name: 'AAudio / Oboe', vendor: 'Google Android',
    note: 'Part of Android (NDK). Low-latency "exclusive" streams where the device supports them.',
    url: 'https://developer.android.com/ndk/guides/audio/aaudio/aaudio', detect: {} },
  { id: 'avaudiosession', os: ['ios'], kind: 'built-in', name: 'AVAudioSession / Core Audio', vendor: 'Apple',
    note: 'Part of iOS. No third-party audio drivers.', url: 'https://developer.apple.com/documentation/avfaudio/avaudiosession', detect: {} },
  // ── The bridge's own native audio module ──
  { id: 'naudiodon2', os: ['win32', 'darwin', 'linux'], kind: 'native', name: 'PortAudio native module (naudiodon2)', vendor: 'npm',
    note: 'Lets the local server open ASIO / WASAPI / Core Audio / ALSA devices. Install it once in the bridge folder (needs a C++ build toolchain).',
    url: 'https://www.npmjs.com/package/naudiodon2', detect: { portaudio: true }, install: { npm: 'cd bridge && npm install' } },
];

const PUBLIC_FIELDS = ['id', 'os', 'kind', 'name', 'vendor', 'note', 'url', 'install'];

function isInstalled(item, info) {
  const d = item.detect || {};
  if (!info) return null;
  if (d.portaudio) return !!info.portaudio;
  if (d.asioAny) return info.platform === 'win32' ? (info.asio || []).length > 0 : null;
  if (d.drivers && d.drivers.some(x => (info.drivers || []).includes(x))) return true;
  if (d.asio && (info.asio || []).some(n => d.asio.test(n))) return true;
  if (item.kind === 'built-in' && item.os.includes(info.platform)) return true;
  return (d.drivers || d.asio) ? false : null;   // null = cannot be detected
}

function listCatalog(info, dir) {
  const platform = info && info.platform;
  return CATALOG.map(it => {
    const out = {};
    PUBLIC_FIELDS.forEach(k => { if (it[k] !== undefined) out[k] = it[k]; });
    out.forThisPc = !platform || it.os.includes(platform);
    out.installed = out.forThisPc ? isInstalled(it, info) : null;   // drivers for other systems cannot be detected here
    // discovery: which registry ASIO names this entry matched (the Windows ASIO registry, read by detect.js), so the page can show the match
    if ((it.kind === 'asio' || it.kind === 'vendor') && it.detect && it.detect.asio && info && info.platform === 'win32') out.registeredAs = (info.asio || []).filter(n => it.detect.asio.test(n));
    return out;
  });
}

module.exports = { CATALOG, listCatalog, isInstalled };
