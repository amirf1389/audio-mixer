// Visits every tab of the mixer page (light theme) and reports text that is hard to read: low contrast against the colour behind it.
// Agent tooling, not product code.  node .claude/skills/run-audio-mixer/pages-check.mjs
import { createRequire } from 'node:module';
import { execSync } from 'node:child_process';
import path from 'node:path';
import fs from 'node:fs';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const BASE = process.env.BRIDGE_URL || 'http://localhost:8765';
const SHOT_DIR = path.join(here, 'shots', 'pages');
fs.mkdirSync(SHOT_DIR, { recursive: true });

async function loadChromium() {
  try { return (await import('playwright')).chromium; } catch (e) { /* global */ }
  const global = execSync('npm root -g', { encoding: 'utf8' }).trim();
  return (await import(path.join(global, 'playwright', 'index.mjs'))).chromium;
}

const chromium = await loadChromium();
const browser = await chromium.launch({ args: ['--no-sandbox'] });
const page = await browser.newPage({ viewport: { width: 1280, height: 800 } });
await page.goto(BASE, { waitUntil: 'load' });
await page.waitForFunction(() => typeof window.switchTab === 'function', null, { timeout: 30000 });
await page.evaluate(() => { const s = document.getElementById('setup-wizard'); if (s) s.remove(); });
await page.waitForTimeout(9000);

// returns the ids of tabs and, for each, the hard-to-read text found on it
const ids = await page.evaluate(() => window.navTabs.map(t => t.id));
const report = [];
for (const id of ids) {
  await page.evaluate(t => document.getElementById("nav-" + t).click(), id);
  await page.waitForTimeout(500);
  const found = await page.evaluate(() => {
    const lum = c => { const [r, g, b] = c.map(v => { v /= 255; return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); }); return 0.2126 * r + 0.7152 * g + 0.0722 * b; };
    const parse = s => { const m = s.match(/rgba?\(([^)]+)\)/); if (!m) return null; const p = m[1].split(',').map(x => parseFloat(x)); return { c: p.slice(0, 3), a: p.length > 3 ? p[3] : 1 }; };
    const bgOf = el => { for (let e = el; e; e = e.parentElement) { const b = parse(getComputedStyle(e).backgroundColor); if (b && b.a > 0.5) return b.c; } return [255, 255, 255]; };
    const tab = document.querySelector('.tab-content:not(.tab-hidden)') || document.body;
    const out = [];
    const walker = document.createTreeWalker(tab, NodeFilter.SHOW_TEXT);
    const seen = new Set();
    while (walker.nextNode()) {
      const node = walker.currentNode; const text = node.textContent.trim();
      if (text.length < 2) continue;
      const el = node.parentElement; if (!el || seen.has(el)) continue; seen.add(el);
      const r = el.getBoundingClientRect(); if (r.width === 0 || r.height === 0) continue;
      if (getComputedStyle(el).visibility === 'hidden' || getComputedStyle(el).display === 'none') continue;
      const fg = parse(getComputedStyle(el).color); if (!fg) continue;
      const bg = bgOf(el);
      const L1 = Math.max(lum(fg.c), lum(bg)) + 0.05, L2 = Math.min(lum(fg.c), lum(bg)) + 0.05;
      const ratio = L1 / L2;
      if (ratio < 3) out.push({ text: text.slice(0, 40), ratio: Math.round(ratio * 10) / 10 });
    }
    return { count: out.length, sample: out.slice(0, 4) };
  });
  report.push({ id, ...found });
}
await browser.close();
for (const r of report) console.log(`${r.id.padEnd(11)} low-contrast text: ${String(r.count).padStart(3)}  ${r.sample.map(s => `"${s.text}" ${s.ratio}:1`).join(' | ')}`);
