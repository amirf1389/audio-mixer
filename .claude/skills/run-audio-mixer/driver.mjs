// Drives the Audio Mixer page in headless Chromium: opens it from the local bridge server,
// switches tabs the way the UI does (window.switchTab), reads state, takes screenshots,
// and reports console errors. Agent tooling, not product code.
//
//   node .claude/skills/run-audio-mixer/driver.mjs [command] [args]   (run from the repo root)
//
//   open                 load the page, print the page title and console errors
//   tab <id> [out.png]   switch to a tab (mixer, asio, dsp, io, sys, ...), screenshot it
//   asio [out.png]       open the Drivers tab, print the ASIO status panel, screenshot the panel
//   eval <js>            run JS in the page and print the JSON result
//
// Env: BRIDGE_URL (default http://localhost:8765), SHOT_DIR (default ./.claude/skills/run-audio-mixer/shots)

import { createRequire } from 'node:module';
import { execSync } from 'node:child_process';
import path from 'node:path';
import fs from 'node:fs';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const BASE = process.env.BRIDGE_URL || 'http://localhost:8765';
const SHOT_DIR = process.env.SHOT_DIR || path.join(here, 'shots');
fs.mkdirSync(SHOT_DIR, { recursive: true });

// Playwright is preinstalled globally in this container; fall back to the global npm root.
async function loadChromium() {
  try { return (await import('playwright')).chromium; } catch (e) { /* not in the local tree */ }
  const global = execSync('npm root -g', { encoding: 'utf8' }).trim();
  return (await import(path.join(global, 'playwright', 'index.mjs'))).chromium;
}

const [cmd = 'open', ...args] = process.argv.slice(2);
const chromium = await loadChromium();
const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
const errors = [];
page.on('pageerror', e => errors.push('PAGEERROR ' + e.message));
page.on('console', m => { if (m.type() === 'error' && !/ERR_CONNECTION_REFUSED|404/.test(m.text())) errors.push('CONSOLE ' + m.text()); });

// The first-run "Factory Hardware Setup" overlay sits on top of everything; hide it for driving.
async function openPage() {
  await page.goto(BASE + '/', { waitUntil: 'load' });
  await page.waitForFunction(() => !!window.switchTab, null, { timeout: 30000 });
  await page.waitForTimeout(8000); // boot sequence
  await page.evaluate(() => { const w = document.getElementById('setup-wizard'); if (w) w.style.display = 'none'; });
}

function shot(name) {
  const file = path.join(SHOT_DIR, name);
  return page.screenshot({ path: file }).then(() => file);
}

try {
  await openPage();
  if (cmd === 'open') {
    console.log(JSON.stringify({ title: await page.title(), url: page.url() }));
    console.log('screenshot:', await shot('open.png'));
  } else if (cmd === 'tab') {
    const [id, out = `tab-${args[0]}.png`] = [args[0], args[1]];
    await page.evaluate(t => window.switchTab(t), id);
    await page.waitForTimeout(800);
    console.log('screenshot:', await shot(out));
  } else if (cmd === 'asio') {
    await page.evaluate(() => window.switchTab('asio'));
    await page.waitForTimeout(1500);
    const hero = await page.evaluate(() => ({
      state: document.getElementById('asio-hero-state').textContent,
      hint: document.getElementById('asio-hero-hint').textContent,
      action: document.getElementById('asio-hero-action').textContent,
      device: document.getElementById('asio-hero-device').textContent,
      buffer: document.getElementById('asio-hero-buffer').textContent,
      autoOn: document.getElementById('asio-auto-toggle').checked,
    }));
    console.log(JSON.stringify(hero, null, 1));
    console.log('screenshot:', await page.locator('#asio-hero').screenshot({ path: path.join(SHOT_DIR, args[0] || 'asio-hero.png') }).then(() => path.join(SHOT_DIR, args[0] || 'asio-hero.png')));
  } else if (cmd === 'eval') {
    console.log(JSON.stringify(await page.evaluate(args.join(' ')), null, 1));
  } else {
    throw new Error('unknown command: ' + cmd);
  }
  console.log('errors:', errors.length ? '\n' + errors.slice(0, 20).join('\n') : 'none');
} finally {
  await browser.close();
}
