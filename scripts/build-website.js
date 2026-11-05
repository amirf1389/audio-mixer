#!/usr/bin/env node
'use strict';
// Builds the AmF X1 public website (deploy/website/*.html, sitemap.xml, robots.txt) from one place, so every page shares the same header,
// footer, SEO metadata (title, description, canonical, Open Graph, Twitter card, JSON-LD) and the same strict, script-free design.
//   node scripts/build-website.js [--site https://amirfakhri.interactivelink.site] [--out deploy/website]
// The keys page (license.html, served at /keys) is hand-written and kept as it is. Served by nginx: deploy/nginx/keys-api.conf.
// Nothing here loads a third-party script, font or CDN file: the Content-Security-Policy can stay "self only".
const fs = require('node:fs');
const path = require('node:path');

const ROOT = path.resolve(__dirname, '..');
const pkg = require(path.join(ROOT, 'package.json'));
const label = require('./release-label');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i >= 0 ? process.argv[i + 1] : d; };
const SITE = String(arg('--site', 'https://amirfakhri.interactivelink.site')).replace(/\/+$/, '');
const OUT = path.resolve(ROOT, arg('--out', 'deploy/website'));
const VERSION = pkg.version, SHOWN = label.displayVersion(pkg.version).replace(/^v/, ''), YEAR = new Date().getFullYear();
const REPO = 'https://github.com/amirf1389/audio-mixer';
const CONTACT = { email: 'badriamirfakhri@gmail.com', whatsapp: 'https://wa.me/601120871185' };

const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
const NAV = [['/', 'Home'], ['/features', 'Features'], ['/developers', 'Developers'], ['/security', 'Security'], ['/faq', 'FAQ'], ['/keys', 'License']];

function page({ slug, title, description, h1, intro, crumbs = true, body, jsonld = [], hero = null, changefreq = 'monthly', priority = '0.7' }) {
  const url = SITE + (slug === '/' ? '/' : slug);
  const ld = [{ '@context': 'https://schema.org', '@type': 'WebPage', name: title, description, url, isPartOf: { '@type': 'WebSite', name: 'AmF X1 by AMFB Audio', url: SITE + '/' }, inLanguage: 'en' }];
  if (crumbs && slug !== '/') ld.push({ '@context': 'https://schema.org', '@type': 'BreadcrumbList', itemListElement: [{ '@type': 'ListItem', position: 1, name: 'Home', item: SITE + '/' }, { '@type': 'ListItem', position: 2, name: h1, item: url }] });
  const nav = NAV.map(([href, name]) => `<a href="${href}"${href === slug ? ' aria-current="page"' : ''}>${name}</a>`).join('') + '<a class="cta" href="/download">Download</a>';
  const head = hero || `<div class="page-head"><div class="wrap">${crumbs ? `<nav class="crumbs" aria-label="Breadcrumb"><a href="/">Home</a> / <span aria-current="page">${esc(h1)}</span></nav>` : ''}<h1>${esc(h1)}</h1>${intro ? `<p>${intro}</p>` : ''}</div></div>`;
  const html = `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${esc(title)}</title>
<meta name="description" content="${esc(description)}">
<link rel="canonical" href="${url}">
<meta name="robots" content="index, follow, max-image-preview:large">
<meta name="theme-color" content="#0f121a">
<meta property="og:type" content="website">
<meta property="og:site_name" content="AmF X1 by AMFB Audio">
<meta property="og:title" content="${esc(title)}">
<meta property="og:description" content="${esc(description)}">
<meta property="og:url" content="${url}">
<meta property="og:image" content="${SITE}/assets/icon-512.png">
<meta property="og:image:width" content="512"><meta property="og:image:height" content="512">
<meta property="og:image:alt" content="AmF X1 logo: three amber mixer faders">
<meta name="twitter:card" content="summary">
<meta name="twitter:title" content="${esc(title)}">
<meta name="twitter:description" content="${esc(description)}">
<link rel="icon" href="/assets/icon.svg" type="image/svg+xml">
<link rel="icon" href="/assets/icon-512.png" type="image/png" sizes="512x512">
<link rel="apple-touch-icon" href="/assets/icon-512.png">
<link rel="stylesheet" href="/assets/site.css?v=${VERSION}">
${[...ld, ...jsonld].map(o => `<script type="application/ld+json">${JSON.stringify(o).replace(/</g, '\\u003c')}</script>`).join('\n')}
</head>
<body>
<a class="skip" href="#main">Skip to content</a>
<header class="site-head"><div class="wrap"><a class="brand" href="/" aria-label="AmF X1 home"><img src="/assets/icon.svg" alt="" width="32" height="32">AMF X1 <b>BY AMFB</b></a><nav class="nav" aria-label="Main">${nav}</nav></div></header>
${head}
<main id="main">
${body}
</main>
<footer class="site-foot"><div class="wrap">
<div><h2>AmF X1</h2><ul><li><a href="/features">Features</a></li><li><a href="/download">Download ${esc(SHOWN)}</a></li><li><a href="/keys">License key</a></li><li><a href="/faq">FAQ</a></li></ul></div>
<div><h2>Developers</h2><ul><li><a href="/developers#frameworks">Frameworks</a></li><li><a href="/developers#api">Local API</a></li><li><a href="/developers#cdn">CDN &amp; hosting</a></li><li><a href="${REPO}" rel="noopener">Source on GitHub</a></li></ul></div>
<div><h2>Trust</h2><ul><li><a href="/security">Security</a></li><li><a href="/privacy">Privacy</a></li><li><a href="mailto:${CONTACT.email}">Report a problem</a></li></ul></div>
<div><h2>Contact</h2><ul><li><a href="${CONTACT.whatsapp}" rel="noopener">WhatsApp</a></li><li><a href="mailto:${CONTACT.email}">${CONTACT.email}</a></li></ul></div>
<p class="legal">&copy; ${YEAR} AMFB Audio. AmF X1 ${esc(SHOWN)}. Dolby, DTS, THX, ASIO and other names are trademarks of their owners; AmF X1 is not affiliated with them.</p>
</div></footer>
</body>
</html>
`;
  return { slug, url, html, changefreq, priority };
}

const card = (tag, h, p) => `<article class="card">${tag ? `<span class="tag">${tag}</span>` : ''}<h3>${h}</h3><p>${p}</p></article>`;
const SOFTWARE = {
  '@context': 'https://schema.org', '@type': 'SoftwareApplication', name: 'AmF X1', alternateName: 'AmF X1 by AMFB Audio', applicationCategory: 'MultimediaApplication',
  applicationSubCategory: 'Audio mixer', operatingSystem: 'Windows 10, Windows 11, Linux, macOS, Android', softwareVersion: SHOWN, url: SITE + '/', downloadUrl: SITE + '/download',
  image: SITE + '/assets/icon-512.png', publisher: { '@type': 'Organization', name: 'AMFB Audio', url: SITE + '/' },
  description: 'A digital audio mixer for Windows and other platforms: ASIO and WASAPI devices, 8 to 32 channels, EQ, dynamics, effects, phase and loudness meters, offline licensing.',
};

const pages = [];

// HOME
pages.push(page({
  slug: '/', title: 'AmF X1 — Digital Audio Mixer for Windows (ASIO & WASAPI)', h1: 'AmF X1',
  description: `AmF X1 by AMFB Audio: a digital audio mixer for Windows with ASIO and WASAPI, up to 32 channels, EQ, dynamics, effects and phase meters.`,
  crumbs: false, priority: '1.0', changefreq: 'weekly', jsonld: [SOFTWARE, { '@context': 'https://schema.org', '@type': 'Organization', name: 'AMFB Audio', url: SITE + '/', logo: SITE + '/assets/icon-512.png', email: CONTACT.email }],
  hero: `<section class="hero"><div class="wrap"><div>
<p class="eyebrow">AMFB Audio · <span class="badge beta">${esc(SHOWN)}</span></p>
<h1>A digital mixer for every audio interface on your PC</h1>
<p class="lead">AmF X1 mixes your microphones, USB interfaces, onboard sound and apps in one console: ASIO when your interface has it, Windows audio when it does not, with EQ, dynamics, effects and studio meters.</p>
<div class="btns"><a class="btn primary" href="/download">Download for Windows</a><a class="btn ghost" href="/features">See all features</a></div>
</div>
<div class="hero-art" role="img" aria-label="Illustration of AmF X1 input meters">
<div class="meter"><span>UM2 IN 1</span><span class="bar"><i class="w72"></i></span><span>-12 dB</span></div>
<div class="meter"><span>REALTEK</span><span class="bar"><i class="w48"></i></span><span>-24 dB</span></div>
<div class="meter"><span>USB DAC</span><span class="bar"><i class="w63"></i></span><span>-16 dB</span></div>
<div class="meter"><span>MASTER</span><span class="bar"><i class="w80"></i></span><span>-8 dB</span></div>
</div></div></section>`,
  body: `<section class="block"><div class="wrap">
<h2>Built for real audio setups</h2>
<p class="sub">From a single USB microphone interface to several devices at once, AmF X1 opens each one with the best driver it has and shows you what is happening.</p>
<div class="grid">
${card('Drivers', 'ASIO first, Windows audio as backup', 'Opens your interface through its own ASIO driver. If the driver is busy or missing a side, the same hardware is used through Windows (WASAPI) so you still have sound.')}
${card('Mixing', '8 to 32 channels', 'Faders, pan, solo, mute, sends, group faders and scenes, with a lean engine that keeps CPU use low while idle.')}
${card('Processing', 'EQ, dynamics and effects', 'Parametric EQ, compressor, noise gate, de-esser, mid/side width, real stereo polarity and an AI DSP rack with plugin store.')}
${card('Metering', 'Phase, loudness, spectrum', 'Correlation to three decimals with phase angle, LUFS loudness, dual FFT analyser and per-device levels.')}
${card('Languages', '22 languages', 'English, Bahasa Melayu, Bahasa Indonesia, Persian, Arabic, Urdu, Spanish, French, German, Japanese, Korean, Chinese and more, with accessibility options.')}
${card('Privacy', 'Works offline', 'Licenses are checked on your PC with a digital signature. Nothing about your audio leaves your computer.')}
</div></div></section>
<section class="block"><div class="wrap">
<h2>Editions</h2>
<p class="sub">Start with Basic. Upgrade when you need more channels and studio tools; existing keys can be checked before you activate them.</p>
<div class="editions">
<article class="card edition"><span class="tag">Basic</span><h3>8 channels</h3><p>The core mixer, EQ, dynamics and meters.</p></article>
<article class="card edition"><span class="tag">Pro</span><h3>16 channels</h3><p>Adds plugins and in-app updates.</p></article>
<article class="card edition"><span class="tag">Studio</span><h3>32 channels</h3><p>Adds the studio tool set.</p></article>
<article class="card edition"><span class="tag">Master</span><h3>All access</h3><p>Every feature and every channel.</p></article>
</div>
<p class="sub mt">Upgrade or ask a question on <a href="${CONTACT.whatsapp}" rel="noopener">WhatsApp</a>, or request a key on the <a href="/keys">license page</a>.</p>
</div></section>`,
}));

// FEATURES
pages.push(page({
  slug: '/features', title: 'Features — AmF X1 Digital Audio Mixer', h1: 'Features',
  description: 'AmF X1 features: ASIO and WASAPI with automatic fallback, multi-device mixing, EQ, dynamics, effects, phase and loudness meters, 22 languages.',
  intro: 'Everything in AmF X1 ' + esc(SHOWN) + ', grouped by what you do with it.', jsonld: [SOFTWARE],
  body: `<section class="block"><div class="wrap"><h2>Devices and drivers</h2><div class="grid">
${card('', 'Every interface in one list', 'USB interfaces, onboard sound, DACs, Bluetooth headsets and loopback devices are grouped by hardware, with read / write switches and a live level for each.')}
${card('', 'Same-hardware fallback', 'When an ASIO driver is held by another program, AmF X1 reads and writes the same device through its Windows driver instead of failing.')}
${card('', 'Low-latency ASIO', 'Your interface\'s own ASIO driver is used first, with automatic activation when the audio engine starts.')}
</div></div></section>
<section class="block"><div class="wrap"><h2>Mixing and processing</h2><div class="grid">
${card('', 'Console', 'Faders, pan, mute, solo, sends, group faders, scenes and presets, with smooth paging up to 32 channels.')}
${card('', 'Tone', 'Parametric EQ with an analyser overlay, mic EQ, dynamic EQ, auto-volume and a treble guard.')}
${card('', 'Dynamics and effects', 'Compressor, noise gate, limiter, de-esser, anti-feedback, chorus, lo-fi, auto-pan, harmony and vocal enhance.')}
${card('', 'Stereo image', 'Mid/side level and width plus left / right polarity that act on the real master signal.')}
</div></div></section>
<section class="block"><div class="wrap"><h2>Meters</h2><div class="grid">
${card('', 'Phase correlation', 'Goniometer and correlation meter to three decimals with the equivalent phase angle and a 10-step scale.')}
${card('', 'Loudness', 'Short-term and integrated loudness with true-peak for streaming targets.')}
${card('', 'Spectrum', 'A 20 Hz to 20 kHz dual FFT analyser for the master and any source.')}
</div></div></section>
<section class="block"><div class="wrap"><h2>Everyone</h2><div class="grid">
${card('', '22 languages', 'Switch the interface language in Settings or with Alt+L; Persian, Arabic and Urdu switch to right-to-left.')}
${card('', 'Accessibility', 'Text size, high contrast, reduced motion, focus outlines and a screen-reader live region for every notification.')}
${card('', 'Light on your PC', 'Meters run at 30 frames per second (15 in low-power mode), hidden pages are not drawn, and low-power mode turns on by itself on battery.')}
</div></div></section>`,
}));

// DOWNLOAD
pages.push(page({
  slug: '/download', title: `Download AmF X1 ${SHOWN} for Windows`, h1: 'Download', priority: '0.9', changefreq: 'weekly',
  description: `Download AmF X1 ${SHOWN} for Windows (64-bit setup and MSI, 32-bit MSI). Also available for Linux and macOS. Free Basic edition, upgrade any time.`,
  intro: `AmF X1 ${esc(SHOWN)}. Beta builds are for testers: please report anything that does not work.`, jsonld: [SOFTWARE],
  body: `<section class="block"><div class="wrap">
<h2>Windows</h2>
<p class="sub">Windows 10 or 11. The setup includes everything it needs; no separate Node.js install.</p>
<div class="table-wrap"><table><thead><tr><th>File</th><th>For</th></tr></thead><tbody>
<tr><td><code>AmF X1-${esc(label.fileVersion(VERSION))}.exe</code></td><td>Setup for 64-bit Windows (recommended)</td></tr>
<tr><td><code>AudioMixer-${esc(label.fileVersion(VERSION))}-x64.msi</code></td><td>64-bit Windows Installer package (managed PCs)</td></tr>
<tr><td><code>AudioMixer-${esc(label.fileVersion(VERSION))}-x86.msi</code></td><td>32-bit Windows</td></tr>
</tbody></table></div>
<p class="btns mt"><a class="btn dark" href="${REPO}/releases" rel="noopener">Get the files on GitHub Releases</a></p>
<p class="note warn mt"><strong>"Unknown publisher" warning:</strong> the installers are not yet signed with a commercial code-signing certificate, so Windows SmartScreen may warn you. Only download from this site or the GitHub page above. Updates inside the app are always checked against the AMFB Audio signing key before they install.</p>
</div></section>
<section class="block"><div class="wrap"><h2>Linux and macOS</h2>
<p class="sub">A <code>.deb</code> package and a <code>.tar.gz</code> for Linux, and a <code>.tar.gz</code> for macOS, on the same <a href="${REPO}/releases" rel="noopener">releases page</a>.</p>
<h2 class="mt">After installing</h2>
<ol><li>Start AmF X1 and choose your audio interface on the DAC &amp; OS sources page.</li><li>Basic works right away. For Pro, Studio or Master, request a key on the <a href="/keys">license page</a> or message us on <a href="${CONTACT.whatsapp}" rel="noopener">WhatsApp</a>.</li><li>Use CHECK KEY to see what a key unlocks before you activate it.</li></ol>
</div></section>`,
}));

// DEVELOPERS: frameworks, API, CDN
pages.push(page({
  slug: '/developers', title: 'Developers — Frameworks, Local API and CDN Hosting | AmF X1', h1: 'Developers',
  description: 'How AmF X1 is built (Node.js, Web Audio, PortAudio, ASIO, WASAPI), its local API and WebSockets, and secure CDN hosting for the website.',
  intro: 'The frameworks AmF X1 is built on, the local API its page uses, and how this website is hosted.',
  jsonld: [{ '@context': 'https://schema.org', '@type': 'TechArticle', headline: 'AmF X1 frameworks, local API and CDN hosting', author: { '@type': 'Organization', name: 'AMFB Audio' }, url: SITE + '/developers' }],
  body: `<section class="block" id="frameworks"><div class="wrap">
<h2>Frameworks and building blocks</h2>
<p class="sub">AmF X1 is one browser page (the console) served by a small local program (the bridge) that talks to your audio drivers.</p>
<div class="table-wrap"><table><thead><tr><th>Layer</th><th>Technology</th><th>Role</th></tr></thead><tbody>
<tr><td>Console</td><td>HTML, CSS, Web Audio API</td><td>Mixer, EQ, dynamics, effects and meters run in the page's audio graph.</td></tr>
<tr><td>Bridge</td><td>Node.js 22 LTS (bundled as a single executable)</td><td>Serves the page on 127.0.0.1, lists devices and streams audio over WebSockets. No runtime dependencies beyond the audio engines.</td></tr>
<tr><td>Audio engines</td><td>PortAudio (naudiodon2), RtAudio (Audify)</td><td>Open devices through ASIO, WASAPI, Core Audio, ALSA or JACK.</td></tr>
<tr><td>Native</td><td>C, C++ and Rust DSP cores, a Windows ASIO driver</td><td>Held to the same test vectors as the page's reference code.</td></tr>
<tr><td>Desktop shell (optional)</td><td>Electron</td><td>A native window and tray around the same page and bridge.</td></tr>
<tr><td>Mobile</td><td>Android (Java, Jetpack Compose), iOS (Swift)</td><td>The same page with an on-device engine.</td></tr>
</tbody></table></div></div></section>
<section class="block" id="api"><div class="wrap">
<h2>Local API (127.0.0.1 only)</h2>
<p class="sub">The bridge listens on <code>http://127.0.0.1:8765</code> on your own computer and is not reachable from the network. Actions need the app's own page: other websites can read status only, never change settings or open audio streams.</p>
<div class="table-wrap"><table><thead><tr><th>Method</th><th>Path</th><th>What it does</th></tr></thead><tbody>
<tr><td>GET</td><td><code>/api/status</code></td><td>Bridge name, version and open streams.</td></tr>
<tr><td>GET</td><td><code>/api/drivers</code></td><td>Host APIs, drivers and devices found on this computer.</td></tr>
<tr><td>GET</td><td><code>/api/interfaces</code></td><td>Devices grouped per physical interface, with read / write routes and fallbacks.</td></tr>
<tr><td>GET</td><td><code>/api/license</code></td><td>Current plan and machine code.</td></tr>
<tr><td>POST</td><td><code>/api/license/check</code></td><td>Checks a key (genuine, this PC, expiry, upgrade or downgrade) without storing it.</td></tr>
<tr><td>POST</td><td><code>/api/license/activate</code></td><td>Activates a key.</td></tr>
<tr><td>WS</td><td><code>/ws/input</code>, <code>/ws/output</code>, <code>/ws/duplex</code></td><td>16-bit PCM audio streams to and from a device.</td></tr>
</tbody></table></div>
<p class="sub mt">Every POST needs <code>Content-Type: application/json</code> and an <code>X-Mixer-Action</code> header naming the action:</p>
<pre><code>curl -s http://127.0.0.1:8765/api/status

curl -s -X POST http://127.0.0.1:8765/api/license/check \\
  -H "Content-Type: application/json" -H "X-Mixer-Action: license" \\
  -d '{"key":"AMIX1.…"}'</code></pre>
<h3 class="mt">Public update API</h3>
<p class="sub">The vendor server publishes <code>/update.json</code> (a signed list of installers with SHA-256 checksums) and <code>/releases/&lt;file&gt;</code>. The app installs an update only when the signature matches the AMFB Audio key built into it.</p>
</div></section>
<section class="block" id="cdn"><div class="wrap">
<h2>CDN and hosting</h2>
<p class="sub">This website is static HTML and one stylesheet. It loads no third-party scripts, fonts or CDN libraries, so there is nothing external to trust and the security policy stays strict.</p>
<div class="grid">
${card('Origin', 'nginx with HTTPS', 'TLS 1.2 / 1.3, HSTS, a strict Content-Security-Policy, rate limits and request size limits (deploy/nginx/keys-api.conf).')}
${card('Edge', 'Any CDN in front', 'Proxy the domain through a CDN with "Full (strict)" TLS to the origin. Cache /assets/ for a year; revalidate HTML on every request.')}
${card('Integrity', 'Versioned assets', 'Stylesheet URLs carry the version (?v=), so a new release is picked up at once without purging the cache.')}
</div>
<pre class="mt"><code># origin cache headers (already in the nginx config)
/assets/*   Cache-Control: public, max-age=31536000, immutable
/*.html     Cache-Control: no-cache
/update.json, /keys, /request-license   never cached by the CDN</code></pre>
</div></section>`,
}));

// SECURITY
pages.push(page({
  slug: '/security', title: 'Security — How AmF X1 Protects Your PC and Data', h1: 'Security',
  description: 'How AmF X1 keeps you safe: a local-only bridge, signed updates and licenses, encrypted key requests and a strict security policy.',
  intro: 'What AmF X1 does to protect your computer, your audio and your data.',
  body: `<section class="block"><div class="wrap"><div class="grid">
${card('Local only', 'The bridge stays on your PC', 'It listens on 127.0.0.1 and checks the host name and the calling page. Other websites cannot change settings, restart your PC or open your microphone.')}
${card('Signed', 'Updates and licenses', 'Updates and license keys are checked with an ECDSA P-256 signature against the AMFB Audio key built into the app. A changed file is refused.')}
${card('Encrypted', 'Key requests', 'Names, e-mail addresses and machine codes in key requests are encrypted to the vendor key (ECIES with AES-256-GCM) and can only be read with the vendor\'s PIN.')}
${card('Hardened', 'Runtimes and website', 'Node.js 22 security releases, Electron security updates, and a website with HTTPS, HSTS and a strict content security policy.')}
</div>
<h2 class="mt">Reporting a security problem</h2>
<p>E-mail <a href="mailto:${CONTACT.email}">${CONTACT.email}</a> with the steps to reproduce it. Please give us time to fix it before telling others. There is no bug bounty.</p>
</div></section>`,
}));

// FAQ (FAQPage structured data from the same questions)
const FAQ = [
  ['Which Windows versions does AmF X1 support?', 'Windows 10 and Windows 11, 64-bit, with a 32-bit MSI for older PCs. Linux and macOS builds are also available.'],
  ['Do I need an ASIO driver?', 'No. AmF X1 uses your interface\'s own ASIO driver when it has one for the lowest latency, and Windows audio (WASAPI) otherwise. Onboard sound such as Realtek works through Windows audio.'],
  ['Does it work with the Behringer UM2 and similar USB interfaces?', 'Yes, through its own ASIO driver (BEHRINGER USB AUDIO) or through Windows audio (USB Audio CODEC). The UM2 microphone is on input 1 only. If you get no signal, close other audio programs that may hold the ASIO driver, or read the device through Windows audio.'],
  ['What does Beta mean?', `Version ${SHOWN} is a test version with new features. It may still have problems; please report them so they can be fixed before the final release.`],
  ['How do I activate a license?', 'Request a key on the license page or on WhatsApp, paste it on the LICENSE page of AmF X1, press CHECK KEY to see what it unlocks, then ACTIVATE.'],
  ['Does AmF X1 send my audio or data anywhere?', 'No. Audio stays on your computer. The app only goes online to check for signed updates, and key requests are sent only when you choose to.'],
  ['Why does Windows say "unknown publisher"?', 'The installers are not yet signed with a commercial code-signing certificate. Download only from this site or the official GitHub releases page.'],
];
pages.push(page({
  slug: '/faq', title: 'FAQ — AmF X1 Digital Audio Mixer', h1: 'Frequently asked questions',
  description: 'AmF X1 answers: Windows versions, ASIO and WASAPI, Behringer UM2 support, the Beta, activating a license, privacy and the publisher warning.',
  jsonld: [{ '@context': 'https://schema.org', '@type': 'FAQPage', mainEntity: FAQ.map(([q, a]) => ({ '@type': 'Question', name: q, acceptedAnswer: { '@type': 'Answer', text: a } })) }],
  body: `<section class="block"><div class="wrap">${FAQ.map(([q, a], i) => `<details${i === 0 ? ' open' : ''}><summary>${esc(q)}</summary><p>${esc(a)}</p></details>`).join('\n')}</div></section>`,
}));

// PRIVACY
pages.push(page({
  slug: '/privacy', title: 'Privacy — AmF X1 by AMFB Audio', h1: 'Privacy', priority: '0.4', changefreq: 'yearly',
  description: 'What data AmF X1 and this website use: no tracking, no analytics, no cookies; key requests are encrypted and used only to send your license.',
  intro: 'Short version: no tracking, no analytics, no cookies.',
  body: `<section class="block"><div class="wrap">
<h2>This website</h2><p>No cookies, no analytics, no third-party scripts or fonts. The web server keeps standard access logs (IP address, time, page) for security and removes them regularly.</p>
<h2 class="mt">Key requests</h2><p>When you request a key you send your name, e-mail address, the plan and your computer's machine code. They are stored encrypted and used only to make and send your license key. Ask us at <a href="mailto:${CONTACT.email}">${CONTACT.email}</a> to see or delete them.</p>
<h2 class="mt">The app</h2><p>Your audio never leaves your computer. The app checks for signed updates and, if you choose, opens WhatsApp or e-mail with a prepared message; nothing is sent without you pressing send.</p>
</div></section>`,
}));

// 404
const notFound = page({ slug: '/404', title: 'Page not found — AmF X1', h1: 'Page not found', description: 'This page does not exist.', intro: 'The page you asked for does not exist.', crumbs: false,
  body: `<section class="block"><div class="wrap"><p><a href="/">Go to the home page</a> or <a href="/download">download AmF X1</a>.</p></div></section>` });
notFound.html = notFound.html.replace('<meta name="robots" content="index, follow, max-image-preview:large">', '<meta name="robots" content="noindex">').replace(/<link rel="canonical"[^>]*>\n/, '');

function build() {
  fs.mkdirSync(OUT, { recursive: true });
  const written = [];
  for (const p of pages) { const f = path.join(OUT, p.slug === '/' ? 'index.html' : p.slug.slice(1) + '.html'); fs.writeFileSync(f, p.html); written.push(f); }
  fs.writeFileSync(path.join(OUT, '404.html'), notFound.html); written.push(path.join(OUT, '404.html'));
  const today = new Date().toISOString().slice(0, 10);
  const urls = [...pages.map(p => ({ loc: p.url, changefreq: p.changefreq, priority: p.priority })), { loc: SITE + '/keys', changefreq: 'monthly', priority: '0.8' }];
  fs.writeFileSync(path.join(OUT, 'sitemap.xml'), `<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n${urls.map(u => `  <url><loc>${u.loc}</loc><lastmod>${today}</lastmod><changefreq>${u.changefreq}</changefreq><priority>${u.priority}</priority></url>`).join('\n')}\n</urlset>\n`);
  fs.writeFileSync(path.join(OUT, 'robots.txt'), `User-agent: *\nAllow: /\nDisallow: /request-license\nDisallow: /releases/\n\nSitemap: ${SITE}/sitemap.xml\n`);
  written.push(path.join(OUT, 'sitemap.xml'), path.join(OUT, 'robots.txt'));
  return written;
}

if (require.main === module) for (const f of build()) console.log(path.relative(ROOT, f));
module.exports = { build, pages, FAQ, SITE };
