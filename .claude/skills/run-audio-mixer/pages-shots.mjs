// Screenshots every tab of the mixer page (light theme) into shots/pages/<id>.png. Agent tooling, not product code.
import { execSync } from 'node:child_process';
import path from 'node:path';
import fs from 'node:fs';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const BASE = process.env.BRIDGE_URL || 'http://localhost:8765';
const OUT = path.join(here, 'shots', 'pages');
fs.mkdirSync(OUT, { recursive: true });
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
const ids = await page.evaluate(() => window.navTabs.map(t => t.id));
for (const id of ids) {
  await page.evaluate(t => document.getElementById('nav-' + t).click(), id);
  await page.waitForTimeout(700);
  await page.screenshot({ path: path.join(OUT, id + '.png') });
}
await browser.close();
console.log(`${ids.length} pages -> ${OUT}`);
