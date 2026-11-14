// usage: node render.js FIRST LAST OUTDIR [SHOTSDIR]  -> OUTDIR/f00000.png ... (skips frames already rendered)
const http = require("http"), fs = require("fs"), path = require("path");
const { chromium } = require("playwright");
const list = process.argv[2].includes(",") ? process.argv[2].split(",").map(Number) : null;
const [first, last, outDir, shots = path.resolve(__dirname, "../../screenshots-audiofiles/teaser-src")] = [Number(process.argv[2]), Number(process.argv[3]), process.argv[4], process.argv[5]];
const roots = { "/three/": path.join(__dirname, "node_modules/three/"), "/shots/": shots + "/", "/fonts/": "/usr/share/fonts/opentype/inter/", "/mono/": "/usr/share/fonts/truetype/dejavu/" };
const types = { ".html": "text/html", ".js": "text/javascript", ".json": "application/json", ".png": "image/png", ".otf": "font/otf", ".ttf": "font/ttf" };
const server = http.createServer((req, res) => {
  const u = decodeURIComponent(req.url.split("?")[0]); let file = null;
  for (const [p, d] of Object.entries(roots)) if (u.startsWith(p)) file = path.join(d, u.slice(p.length));
  if (!file) file = path.join(__dirname, u === "/" ? "scene.html" : u);
  fs.readFile(file, (e, b) => { if (e) { res.writeHead(404); res.end(); return; } res.writeHead(200, { "Content-Type": types[path.extname(file)] || "application/octet-stream" }); res.end(b); });
});
server.listen(0, async () => {
  const port = server.address().port; fs.mkdirSync(outDir, { recursive: true });
  const browser = await chromium.launch({ args: ["--use-angle=swiftshader", "--enable-unsafe-swiftshader", "--ignore-gpu-blocklist"] });
  const page = await browser.newPage({ viewport: { width: 1920, height: 1080 }, deviceScaleFactor: 1 });
  page.on("pageerror", (e) => console.error("page error:", e.message));
  page.on("console", (m) => { if (m.type() === "error") console.error("console:", m.text()); });
  await page.goto(`http://127.0.0.1:${port}/`);
  await page.waitForFunction("window.ready === true", null, { timeout: 120000 });
  const t0 = Date.now(); let n = 0;
  const todo = list || Array.from({ length: last - first + 1 }, (_, k) => first + k);
  for (const f of todo) {
    const out = path.join(outDir, `f${String(f).padStart(5, "0")}.png`);
    if (fs.existsSync(out)) continue;
    await page.evaluate((k) => window.renderAt(k), f);
    await page.screenshot({ path: out + ".tmp.png", type: "png" }); fs.renameSync(out + ".tmp.png", out); ++n;
    if (n % 30 === 0) console.log(`frames ${first}-${last}: ${f} done, ${((Date.now() - t0) / n / 1000).toFixed(2)} s/frame`);
  }
  console.log(`done ${first}-${last}: ${n} frames in ${((Date.now() - t0) / 1000).toFixed(0)} s`);
  await browser.close(); server.close();
});
