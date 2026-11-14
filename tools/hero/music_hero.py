#!/usr/bin/env python3
"""The hero film's soundtrack: the console's own master, recorded while it was filmed (so every hit is the one on its meters), in
order, plus an original bed under it (sub swell, air pad, a riser into the end card, the final hit) and the end card's tail.
usage: music_hero.py OUT.wav REC1.wav REC2.wav ... [--tail 4]"""
import sys, wave
import numpy as np

SR = 48000
args = [a for a in sys.argv[1:] if not a.startswith("--")]
out, recs = args[0], args[1:]
tail = 4.0


def read(path):
    w = wave.open(path); n = w.getnframes(); ch = w.getnchannels(); sw = w.getsampwidth(); assert w.getframerate() == SR, path
    raw = w.readframes(n)
    if sw == 3:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3).astype(np.int32); x = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16); x = np.where(x >= 1 << 23, x - (1 << 24), x) / 8388608.0
    elif sw == 2: x = np.frombuffer(raw, "<i2") / 32768.0
    else: x = np.frombuffer(raw, "<f4").astype(np.float64)
    x = x.reshape(-1, ch)
    return x if ch == 2 else np.repeat(x, 2, axis=1)


con = np.concatenate([read(p) for p in recs])
foot = len(con) / SR; total = foot + tail
N = int(total * SR); t = np.arange(N) / SR
mix = np.zeros((N, 2)); mix[: len(con)] += con * 0.9
rng = np.random.default_rng(3)


def midi(m): return 440 * 2 ** ((m - 69) / 12)


def put(x, at, g=1.0, pan=0.0):
    i = int(at * SR); j = min(N, i + len(x)); x = x[: j - i] * g
    mix[i:j, 0] += x * np.sqrt(0.5 * (1 - pan)) * 1.414; mix[i:j, 1] += x * np.sqrt(0.5 * (1 + pan)) * 1.414


# the air pad: A minor, slow, wide, under everything
def pad(notes, at, length, g):
    n = int(length * SR); tt = np.arange(n) / SR; s = np.zeros(n)
    for m in notes:
        f = midi(m); s += np.sin(2 * np.pi * f * tt) + 0.35 * np.sin(2 * np.pi * f * 2.001 * tt + 1.3)
    s *= np.minimum(1, tt / 1.6) * np.minimum(1, (length - tt) / 1.4) * (0.8 + 0.2 * np.sin(2 * np.pi * 0.15 * tt))
    put(s, at, g, -0.4); put(np.roll(s, 613), at, g, 0.4)


pad([57, 64, 69, 72], 0.0, foot + 0.4, 0.018)
pad([57, 60, 64, 69, 76], foot - 0.05, tail + 0.05, 0.03)
# sub swell under the first bars
n = int(3.0 * SR); tt = np.arange(n) / SR; put(np.sin(2 * np.pi * 55 * tt) * np.minimum(1, tt / 2.0) * np.exp(-np.maximum(0, tt - 2.0) * 3), 0.0, 0.12)
# the riser into the end card, and its hit
n = int(3.0 * SR); k = np.linspace(0, 1, n) ** 2
put((rng.standard_normal(n) * 0.5 + np.sin(2 * np.pi * np.cumsum(220 + 1800 * k) / SR) * 0.25) * k, foot - 3.0, 0.08)
n = int(3.8 * SR); tt = np.arange(n) / SR; f = 32 + 80 * np.exp(-tt * 9)
put(np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-tt * 1.4) + rng.standard_normal(n) * np.exp(-tt * 6) * 0.3, foot, 0.75)
# a soft bell on the logo
for d, m in [(0.45, 81), (0.85, 76), (1.25, 88)]:
    n = int(2.2 * SR); tt = np.arange(n) / SR; put((np.sin(2 * np.pi * midi(m) * tt) + 0.3 * np.sin(2 * np.pi * midi(m) * 3.01 * tt)) * np.exp(-tt * 2.2), foot + d, 0.05, 0.3 * (d - 0.8))

# room: a short stereo convolution reverb on the whole mix
irn = int(1.6 * SR); it = np.arange(irn) / SR
def conv(x, ir):
    m = len(x) + len(ir) - 1; nf = 1 << (m - 1).bit_length()
    return np.fft.irfft(np.fft.rfft(x, nf) * np.fft.rfft(ir, nf), nf)[: len(x)] / np.sqrt(np.sum(ir ** 2))
irL = rng.standard_normal(irn) * np.exp(-it * 3.4); irR = rng.standard_normal(irn) * np.exp(-it * 3.4); irL[:600] = 0; irR[:830] = 0
mix[:, 0] += 0.14 * conv(mix[:, 0], irL); mix[:, 1] += 0.14 * conv(mix[:, 1], irR)
mix *= np.minimum(1, t / 0.25)[:, None] * np.clip((total - t) / 1.2, 0, 1)[:, None]
mix = np.tanh(mix * 1.05); mix *= 10 ** (-1 / 20) / np.max(np.abs(mix))
with wave.open(out, "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR); w.writeframes((mix * 32767).round().astype("<i2").tobytes())
print(f"{out}: {total:.2f} s (console {foot:.2f} s + end {tail:.1f} s)")
