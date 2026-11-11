#!/usr/bin/env python3
# Renders the textures of the plug-in editor (native/amixos/skin/*.png): wood, the aged faceplate, the knob body and its pointer, the
# selector knob, a screw, the meter dial and its glass, the toggle, the lamp glow. Everything is generated here (noise, gradients,
# lighting): no photograph and no other maker's artwork. Re-run after changing a look:  python3 scripts/make-plugin-skin.py
import os, math
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'native', 'amixos', 'skin'); os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(1401)
def save(img, name): img.save(os.path.join(OUT, name), optimize=True)
def noise(w, h, scale=1.0, blur=0.0):
    a = rng.normal(0, 1, (max(2, int(h / scale)), max(2, int(w / scale)))); im = Image.fromarray(np.uint8(np.clip(a * 40 + 128, 0, 255))).resize((w, h), Image.BICUBIC)
    if blur: im = im.filter(ImageFilter.GaussianBlur(blur))
    return (np.asarray(im, dtype=np.float32) - 128) / 128
def rgb(arr): return Image.fromarray(np.uint8(np.clip(arr, 0, 255)), 'RGB')
def font(size, bold=True):
    try: return ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf' % ('-Bold' if bold else ''), size)
    except Exception: return ImageFont.load_default()

# ---- wood: long grain along x, pores, darker edges
W, H = 1400, 420
x = np.linspace(0, 1, W)[None, :]; y = np.linspace(0, 1, H)[:, None]
warp = noise(W, H, 90, 30) * 0.06 + noise(W, H, 300, 80) * 0.10
grain = np.sin((y + warp) * 150 + noise(W, H, 40, 12) * 2.2) * 0.5 + np.sin((y + warp * 1.7) * 37) * 0.5
fine = np.asarray(Image.fromarray(np.uint8(rng.uniform(0, 255, (H, W // 14)))).resize((W, H), Image.BICUBIC), dtype=np.float32) / 255 - 0.5
base = np.array([146, 96, 48], dtype=np.float32)
v = 1 + grain * 0.11 + fine * 0.16 + noise(W, H, 2) * 0.03
edge = np.minimum(np.minimum(x, 1 - x) * 26, np.minimum(y, 1 - y) * 9).clip(0, 1) * 0.28 + 0.72
wood = base[None, None, :] * (v * edge)[:, :, None]; wood[:, :, 2] *= 0.92
save(rgb(wood), 'wood.png')

# ---- faceplate: cream enamel, uneven, speckled, scratched, a soft sheen from above
W, H = 1400, 380
x = np.linspace(0, 1, W)[None, :]; y = np.linspace(0, 1, H)[:, None]
tone = 1 + noise(W, H, 160, 60) * 0.045 + noise(W, H, 30, 8) * 0.018 + noise(W, H, 1.3) * 0.022
sheen = 1.04 - y * 0.10
edge = np.minimum(np.minimum(x, 1 - x) * 60, np.minimum(y, 1 - y) * 16).clip(0, 1) * 0.10 + 0.90
plate = np.array([233, 227, 208], dtype=np.float32)[None, None, :] * (tone * sheen * edge)[:, :, None]
plate[:, :, 2] *= 1 - (noise(W, H, 220, 90) * 0.03 + 0.02)          # yellowing in patches
im = rgb(plate); d = ImageDraw.Draw(im, 'RGBA')
for _ in range(260):                                               # specks of dirt and chipped paint
    px, py, r = rng.integers(0, W), rng.integers(0, H), rng.choice([0.6, 0.8, 1.0, 1.4, 2.2], p=[.4, .3, .18, .09, .03]); a = int(rng.integers(30, 110))
    d.ellipse([px - r, py - r, px + r, py + r], fill=(70, 60, 45, a))
for _ in range(46):                                                # hairline scratches
    px, py, ln, ang = rng.integers(0, W), rng.integers(0, H), rng.integers(20, 160), rng.uniform(-0.35, 0.35) + (0 if rng.random() < .7 else 1.3)
    col = (255, 252, 240, int(rng.integers(40, 90))) if rng.random() < .55 else (90, 80, 60, int(rng.integers(25, 60)))
    d.line([px, py, px + math.cos(ang) * ln, py + math.sin(ang) * ln], fill=col, width=1)
save(im.filter(ImageFilter.GaussianBlur(0.35)), 'plate.png')

# ---- knob: a bakelite skirt with a knurled edge, a domed cap, light from the upper left, its shadow on the plate
S = 4; N = 132 * S; c = N / 2
yy, xx = np.mgrid[0:N, 0:N].astype(np.float32); dx, dy = xx - c, yy - c; rad = np.hypot(dx, dy); ang = np.arctan2(dy, dx)
img = np.zeros((N, N, 4), dtype=np.float32)
R = 50 * S; Rc = 37 * S
sh = Image.new('L', (N, N), 0); ImageDraw.Draw(sh).ellipse([c - R + 5 * S, c - R + 9 * S, c + R + 5 * S, c + R + 9 * S], fill=150); sh = np.asarray(sh.filter(ImageFilter.GaussianBlur(5 * S)), dtype=np.float32)
img[:, :, 3] = sh * 0.8
light = (-(dx * 0.6 + dy * 0.8) / (R)).clip(-1, 1)                  # +1 where the light comes from
skirt = (rad <= R)
knurl = (np.sin(ang * 46) * 0.5 + 0.5) ** 2 * ((rad > R * 0.80) & skirt)
tone = 20 + light * 14 + knurl * 30 * (0.5 + 0.5 * light) + ((rad > R * 0.96) & skirt) * (18 * (light + 1))
slope = ((rad - Rc) / (R * 0.80 - Rc)).clip(0, 1)                   # the skirt falls away from the cap
tone += (1 - slope) * 10 * ((rad > Rc) & skirt)
for k in range(3): img[:, :, k] = np.where(skirt, tone, img[:, :, k])
img[:, :, 3] = np.where(skirt, 255, img[:, :, 3])
cap = rad <= Rc
dome = np.sqrt((1 - (rad / Rc) ** 2).clip(0, 1))
ctone = 30 + light * 26 * (1 - dome * 0.5) + dome * 16
spec = np.exp(-(((dx + Rc * 0.38) ** 2 + (dy + Rc * 0.46) ** 2) / (2 * (Rc * 0.26) ** 2))) * 150 + np.exp(-(((dx + Rc * 0.30) ** 2 + (dy + Rc * 0.36) ** 2) / (2 * (Rc * 0.08) ** 2))) * 90
ring = ((rad > Rc * 0.93) & cap) * (10 + 26 * (light + 1) / 2) - ((rad > Rc - 1.2 * S) & (rad <= Rc + 1.4 * S)) * 16
for k in range(3): img[:, :, k] = np.where(cap, ctone + spec + ring, img[:, :, k] + ((rad > Rc) & (rad <= Rc + 1.4 * S)) * -14)
img[:, :, :3] += rng.normal(0, 2.2, (N, N, 1))
save(Image.fromarray(np.uint8(img.clip(0, 255)), 'RGBA').resize((N // S, N // S), Image.LANCZOS), 'knob.png')
ptr = Image.new('RGBA', (N, N), (0, 0, 0, 0)); d = ImageDraw.Draw(ptr)
d.rounded_rectangle([c - 2.4 * S + S, c - Rc * 0.96 + S, c + 2.4 * S + S, c - Rc * 0.30 + S], 2 * S, fill=(0, 0, 0, 110))
d.rounded_rectangle([c - 2.2 * S, c - Rc * 0.96, c + 2.2 * S, c - Rc * 0.30], 2 * S, fill=(244, 242, 232, 255))
d.rounded_rectangle([c - 1.4 * S, c - R * 0.985, c + 1.4 * S, c - R * 0.80], S, fill=(236, 234, 224, 235))
save(ptr.filter(ImageFilter.GaussianBlur(0.5 * S)).resize((N // S, N // S), Image.LANCZOS), 'knob-pointer.png')

# ---- selector ("chicken head") knob: a round base and a long pointed bar, rotated by the editor
N = 150 * S; c = N / 2; im = Image.new('RGBA', (N, N), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
bar = [(c - 13 * S, c + 34 * S), (c + 13 * S, c + 34 * S), (c + 9 * S, c - 30 * S), (c + 3.5 * S, c - 62 * S), (c - 3.5 * S, c - 62 * S), (c - 9 * S, c - 30 * S)]
shd = Image.new('RGBA', (N, N), (0, 0, 0, 0)); ds = ImageDraw.Draw(shd); ds.ellipse([c - 36 * S + 4 * S, c - 36 * S + 8 * S, c + 36 * S + 4 * S, c + 36 * S + 8 * S], fill=(0, 0, 0, 130)); ds.polygon([(px + 5 * S, py + 9 * S) for px, py in bar], fill=(0, 0, 0, 120)); im.alpha_composite(shd.filter(ImageFilter.GaussianBlur(5 * S)))
d = ImageDraw.Draw(im); d.ellipse([c - 36 * S, c - 36 * S, c + 36 * S, c + 36 * S], fill=(22, 22, 22, 255), outline=(60, 60, 60, 255), width=S)
d.polygon(bar, fill=(30, 30, 30, 255)); d.line([bar[5], bar[4]], fill=(110, 110, 110, 255), width=2 * S); d.line([bar[0], bar[5]], fill=(95, 95, 95, 255), width=2 * S); d.line([bar[1], bar[2], bar[3]], fill=(8, 8, 8, 255), width=2 * S)
d.rounded_rectangle([c - 1.6 * S, c - 58 * S, c + 1.6 * S, c - 26 * S], S, fill=(244, 242, 232, 255))
save(im.filter(ImageFilter.GaussianBlur(0.4 * S)).resize((N // S, N // S), Image.LANCZOS), 'selector.png')

# ---- screw: a slotted steel head
N = 26 * S; c = N / 2; yy, xx = np.mgrid[0:N, 0:N].astype(np.float32); dx, dy = xx - c, yy - c; rad = np.hypot(dx, dy); R = 9.5 * S
a = np.zeros((N, N, 4), dtype=np.float32); sh = Image.new('L', (N, N), 0); ImageDraw.Draw(sh).ellipse([c - R + S, c - R + 2 * S, c + R + S, c + R + 2 * S], fill=170); a[:, :, 3] = np.asarray(sh.filter(ImageFilter.GaussianBlur(1.6 * S)), dtype=np.float32)
head = rad <= R; t = 150 + (-(dx * 0.6 + dy * 0.8) / R) * 55 + np.sin(np.arctan2(dy, dx) * 2 + 0.7) * 16 - (rad / R) ** 3 * 40 + rng.normal(0, 5, (N, N))
for k in range(3): a[:, :, k] = np.where(head, t * [1.0, 0.99, 0.95][k], 0)
a[:, :, 3] = np.where(head, 255, a[:, :, 3]); im = Image.fromarray(np.uint8(a.clip(0, 255)), 'RGBA'); d = ImageDraw.Draw(im)
d.line([c - R * 0.8, c - R * 0.34, c + R * 0.8, c + R * 0.34], fill=(38, 36, 32, 255), width=int(2.2 * S)); d.line([c - R * 0.8, c - R * 0.34 + 1.6 * S, c + R * 0.8, c + R * 0.34 + 1.6 * S], fill=(225, 222, 212, 150), width=S // 2 + 1)
save(im.resize((N // S, N // S), Image.LANCZOS), 'screw.png')

# ---- meter: an aged dial lit from behind, a printed scale, and the glass over it
W, H, S2 = 300, 190, 3; im = Image.new('RGB', (W * S2, H * S2)); px = np.zeros((H * S2, W * S2, 3), dtype=np.float32)
yy, xx = np.mgrid[0:H * S2, 0:W * S2].astype(np.float32); glow = np.exp(-(((xx - W * S2 * 0.5) / (W * S2 * 0.55)) ** 2 + ((yy - H * S2 * 0.80) / (H * S2 * 0.75)) ** 2))
px[:] = np.array([250, 226, 150], dtype=np.float32) * (0.50 + 0.56 * glow)[:, :, None]; px *= (1 + noise(W * S2, H * S2, 50, 20) * 0.05 + noise(W * S2, H * S2, 1.5) * 0.02)[:, :, None]
im = rgb(px); d = ImageDraw.Draw(im); cx, cy, R = W * S2 / 2, H * S2 * 1.18, H * S2 * 0.90; a0, a1 = 225, 315
def at(db): return a0 + (a1 - a0) * (max(-20, min(3, db)) + 20) / 23
ink = (34, 30, 22)
d.arc([cx - R, cy - R, cx + R, cy + R], at(-20), at(0), fill=ink, width=2 * S2); d.arc([cx - R, cy - R, cx + R, cy + R], at(0), at(3), fill=(170, 26, 22), width=5 * S2)
f = font(9 * S2); fs = font(7 * S2, False)
for db in [-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3]:
    t = math.radians(at(db)); l = 10 * S2 if db in (-20, -10, -5, 0, 3) else 6 * S2; col = (170, 26, 22) if db > 0 else ink
    d.line([cx + math.cos(t) * (R - l), cy + math.sin(t) * (R - l), cx + math.cos(t) * (R + 2 * S2), cy + math.sin(t) * (R + 2 * S2)], fill=col, width=int(1.4 * S2))
    if db in (-20, -10, -5, -3, 0, 3): s = ('+' if db > 0 else '') + str(abs(db) if db < 0 else db); tw = d.textlength(s, font=f); d.text((cx + math.cos(t) * (R - 21 * S2) - tw / 2, cy + math.sin(t) * (R - 21 * S2) - 6 * S2), s, font=f, fill=col)
d.text((cx - d.textlength('VU', font=font(13 * S2)) / 2, H * S2 * 0.60), 'VU', font=font(13 * S2), fill=ink); d.text((cx - d.textlength('AMFB AUDIO', font=fs) / 2, H * S2 * 0.74), 'AMFB AUDIO', font=fs, fill=ink)
d.rectangle([0, H * S2 * 0.87, W * S2, H * S2], fill=(26, 24, 20))
save(im.filter(ImageFilter.GaussianBlur(0.5 * S2)).resize((W, H), Image.LANCZOS), 'meter.png')
g = np.zeros((H, W, 4), dtype=np.float32); yy, xx = np.mgrid[0:H, 0:W].astype(np.float32); band = np.exp(-(((xx * 0.75 + yy * 1.4) - 150) / 34) ** 2) * 60 + np.exp(-(((xx * 0.75 + yy * 1.4) - 250) / 12) ** 2) * 34
g[:, :, :3] = 255; g[:, :, 3] = band + (yy < 5) * 60; inner = np.minimum(np.minimum(xx, W - 1 - xx), np.minimum(yy, H - 1 - yy)); dark = np.zeros((H, W, 4), dtype=np.float32); dark[:, :, 3] = (1 - (inner / 16).clip(0, 1)) ** 2 * 150
gl = Image.alpha_composite(Image.fromarray(np.uint8(dark.clip(0, 255)), 'RGBA'), Image.fromarray(np.uint8(g.clip(0, 255)), 'RGBA')); save(gl, 'meter-glass.png')

# ---- toggle: a chromed bat lever in its nut (the lever points right = ON; the editor mirrors it for OFF)
N = 64 * S; c = N / 2; im = Image.new('RGBA', (N, N), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
shd = Image.new('RGBA', (N, N), (0, 0, 0, 0)); ds = ImageDraw.Draw(shd); ds.ellipse([c - 17 * S + 2 * S, c - 17 * S + 4 * S, c + 17 * S + 2 * S, c + 17 * S + 4 * S], fill=(0, 0, 0, 140)); ds.line([c + 3 * S, c + 5 * S, c + 27 * S, c + 1 * S], fill=(0, 0, 0, 120), width=9 * S); im.alpha_composite(shd.filter(ImageFilter.GaussianBlur(3 * S))); d = ImageDraw.Draw(im)
hexp = [(c + math.cos(math.radians(a)) * 17 * S, c + math.sin(math.radians(a)) * 17 * S) for a in range(0, 360, 60)]; d.polygon(hexp, fill=(150, 150, 146, 255), outline=(70, 70, 66, 255)); d.line([hexp[3], hexp[4], hexp[5]], fill=(232, 232, 226, 255), width=2 * S)
d.ellipse([c - 10 * S, c - 10 * S, c + 10 * S, c + 10 * S], fill=(96, 96, 92, 255), outline=(40, 40, 38, 255), width=S); d.ellipse([c - 7 * S, c - 7 * S, c + 7 * S, c + 7 * S], fill=(30, 30, 28, 255))
d.line([c, c, c + 22 * S, c - 6 * S], fill=(120, 120, 116, 255), width=8 * S); d.line([c + S, c - 2 * S, c + 22 * S, c - 8 * S], fill=(244, 244, 238, 255), width=3 * S); d.ellipse([c + 16 * S, c - 13 * S, c + 30 * S, c + 1 * S], fill=(176, 176, 170, 255), outline=(84, 84, 80, 255), width=S); d.ellipse([c + 19 * S, c - 11 * S, c + 24 * S, c - 6 * S], fill=(255, 255, 250, 235))
save(im.filter(ImageFilter.GaussianBlur(0.45 * S)).resize((N // S, N // S), Image.LANCZOS), 'toggle.png')

# ---- lamp: the jewel (lit and dark) with its glow
for name, lit in (('lamp-on.png', True), ('lamp-off.png', False)):
    N = 84; c = N / 2; yy, xx = np.mgrid[0:N, 0:N].astype(np.float32); dx, dy = xx - c, yy - c; rad = np.hypot(dx, dy); a = np.zeros((N, N, 4), dtype=np.float32)
    if lit: a[:, :, 0] = 255; a[:, :, 1] = 60; a[:, :, 2] = 40; a[:, :, 3] = np.exp(-(rad / 24) ** 2) * 150
    bezel = (rad <= 17) & (rad > 13); t = 150 + (-(dx * 0.6 + dy * 0.8) / 17) * 70
    for k in range(3): a[:, :, k] = np.where(bezel, t, a[:, :, k])
    a[:, :, 3] = np.where(bezel, 255, a[:, :, 3]); jewel = rad <= 13; core = np.exp(-(rad / 9) ** 2)
    col = (np.array([255, 70, 50]) if lit else np.array([96, 18, 16])); hi = np.exp(-(((dx + 4) ** 2 + (dy + 5) ** 2) / 14)) * (150 if lit else 90)
    for k in range(3): a[:, :, k] = np.where(jewel, col[k] * (0.62 + 0.38 * core) + (120 * core if lit and k > 0 else 0) + hi, a[:, :, k])
    a[:, :, 3] = np.where(jewel, 255, a[:, :, 3]); save(Image.fromarray(np.uint8(a.clip(0, 255)), 'RGBA'), name)
print('skin written to', os.path.normpath(OUT), sorted(os.listdir(OUT)))
