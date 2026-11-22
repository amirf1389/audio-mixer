// usage: [W=3840 H=2160 FPS=60] node render.js FIRST LAST|list OUTDIR   (CAP = the capture folder: <clip>/f0000.png + cursor.json)
const http = require("http"), fs = require("fs"), path = require("path");
const { chromium } = require("playwright");
const W = Number(process.env.W || 3840), H = Number(process.env.H || 2160), FPS = Number(process.env.FPS || 60);
const CAP = process.env.CAP || path.join(__dirname, "cap"), THREE = process.env.THREE_DIR || path.join(__dirname, "../brand3d/node_modules/three");
const list = process.argv[2].includes(",") ? process.argv[2].split(",").map(Number) : null;
const first = Number(process.argv[2]), last = Number(process.argv[3]), outDir = process.argv[4];
// the clip index, in the order the script filmed them
const order = (process.env.CLIPS || "mix,eq,amp").split(",");
const clips = order.map((name) => { const c = JSON.parse(fs.readFileSync(path.join(CAP, name, "cursor.json"))); return { name, frames: c.cursor.length, cursor: c.cursor, w: c.w, h: c.h }; });
const roots = { "/three/": THREE + "/", "/cap/": CAP + "/", "/fonts/": "/usr/share/fonts/opentype/inter/" };
const types = { ".html": "text/html", ".js": "text/javascript", ".json": "application/json", ".png": "image/png", ".otf": "font/otf" };
const server = http.createServer((req, res) => {
  let u; try { u = decodeURIComponent(req.url.split("?")[0]); } catch (_) { res.writeHead(400); res.end(); return; }
  if (u === "/cap/clips.json") { res.writeHead(200, { "Content-Type": "application/json" }); res.end(JSON.stringify(clips)); return; }
  let file = null, base = __dirname; for (const [p, d] of Object.entries(roots)) if (u.startsWith(p)) { base = d; file = path.join(d, u.slice(p.length)); }
  if (!file) file = path.join(__dirname, u === "/" ? "hero.html" : u);
  // only files inside the folder the prefix maps to ("/fonts/../../etc/passwd" is refused)
  const inside = path.relative(path.resolve(base), path.resolve(file)); if (!inside || inside.startsWith("..") || path.isAbsolute(inside)) { res.writeHead(404); res.end(); return; }
  fs.readFile(file, (e, b) => { if (e) { res.writeHead(404); res.end(); return; } res.writeHead(200, { "Content-Type": types[path.extname(file)] || "application/octet-stream" }); res.end(b); });
});
server.listen(0, "127.0.0.1", async () => {   // this computer only: the page is only for the headless browser below
  const port = server.address().port; fs.mkdirSync(outDir, { recursive: true });
  const browser = await chromium.launch({ args: ["--use-angle=swiftshader", "--enable-unsafe-swiftshader", "--ignore-gpu-blocklist"] });
  const page = await browser.newPage({ viewport: { width: W, height: H }, deviceScaleFactor: 1 });
  page.on("pageerror", (e) => console.error("page error:", e.message));
  page.on("console", (m) => { if (m.type() === "error") console.error("console:", m.text()); });
  await page.goto(`http://127.0.0.1:${port}/?w=${W}&h=${H}&fps=${FPS}${process.env.OFF ? "&off=" + process.env.OFF : ""}`);
  await page.waitForFunction("window.ready === true", null, { timeout: 180000 });
  const total = await page.evaluate("window.totalFrames"); const t0 = Date.now(); let n = 0;
  const todo = list || Array.from({ length: Math.min(last, total - 1) - first + 1 }, (_, k) => first + k);
  for (const f of todo) {
    const out = path.join(outDir, `f${String(f).padStart(5, "0")}.png`); if (fs.existsSync(out)) continue;
    const ta = Date.now(); await page.evaluate((k) => window.renderAt(k), f); const tb = Date.now();
    await page.screenshot({ path: out + ".tmp.png", type: process.env.JPG ? "jpeg" : "png", quality: process.env.JPG ? 98 : undefined }); if (process.env.PROF) console.log(`render ${tb - ta} ms, screenshot ${Date.now() - tb} ms`); fs.renameSync(out + ".tmp.png", out); ++n;
    if (process.env.MAXN && n >= Number(process.env.MAXN)) { console.log(`batch of ${n} frames done, restarting the browser`); await browser.close(); server.close(); process.exit(3); }
    if (n % 20 === 0) console.log(`frames ${first}-${last}: ${f} done, ${((Date.now() - t0) / n / 1000).toFixed(2)} s/frame (of ${total})`);
  }
  console.log(`done ${first}-${last}: ${n} frames in ${((Date.now() - t0) / 1000).toFixed(0)} s`);
  await browser.close(); server.close();
});
