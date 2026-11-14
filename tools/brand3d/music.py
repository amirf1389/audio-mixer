#!/usr/bin/env python3
"""AmF X1 brand film soundtrack (original, synthesized): 46 s, 120 BPM, A minor.
Writes music.wav and features.json (per video frame: kick pulse, level, 32 spectrum bands) so the picture moves with the sound.
Timeline (s): 0-4 swell + riser, 4 logo hit, 8-32 groove (feature shots every 4 s), 32-38 drop (spectrum), 38 final hit, 38-46 tail."""
import json, sys
import numpy as np

SR, DUR, BPM, FPS = 48000, 46.0, 120, 30
BEAT = 60 / BPM
N = int(SR * DUR)
t = np.arange(N) / SR
rng = np.random.default_rng(7)
L = np.zeros(N); R = np.zeros(N)


def env_exp(n, dec):
    return np.exp(-np.arange(n) / SR * dec)


def put(sig, start, gain=1.0, pan=0.0):
    i = int(start * SR); j = min(N, i + len(sig))
    if i >= N: return
    s = sig[: j - i] * gain
    L[i:j] += s * np.sqrt(0.5 * (1 - pan)) * 1.414; R[i:j] += s * np.sqrt(0.5 * (1 + pan)) * 1.414


def midi(m):
    return 440.0 * 2 ** ((m - 69) / 12)


def kick(level=1.0):
    n = int(0.45 * SR); tt = np.arange(n) / SR
    f = 42 + 110 * np.exp(-tt * 32); ph = 2 * np.pi * np.cumsum(f) / SR
    click = rng.standard_normal(n) * np.exp(-tt * 400) * 0.25
    return level * (np.sin(ph) * np.exp(-tt * 6.5) + click)


def hat(level=0.18, dec=70):
    n = int(0.12 * SR); x = rng.standard_normal(n)
    x = np.diff(np.concatenate([[0], x]))  # brighter
    return level * x * env_exp(n, dec)


def clap(level=0.35):
    n = int(0.3 * SR); x = rng.standard_normal(n); e = np.zeros(n)
    for k, d in enumerate([0, 0.011, 0.022]):
        i = int(d * SR); e[i:] += np.exp(-np.arange(n - i) / SR * (90 if k < 2 else 16))
    x = np.convolve(x, [1, -0.6], "same")
    return level * x * e


def saw(freq, n, detune=0.0):
    tt = np.arange(n) / SR; out = np.zeros(n)
    for d in (-detune, 0, detune):
        out += 2 * ((tt * freq * (1 + d)) % 1.0) - 1
    return out / 3


def lp(x, cutoff):
    """one-pole low-pass (vectorized via cumulative filter in blocks is overkill: lfilter-equivalent with numpy)"""
    a = np.exp(-2 * np.pi * cutoff / SR); y = np.empty_like(x); acc = 0.0
    # chunked for speed
    for i in range(0, len(x), 4096):
        seg = x[i:i + 4096]; out = np.empty_like(seg)
        for k in range(len(seg)):
            acc = (1 - a) * seg[k] + a * acc; out[k] = acc
        y[i:i + 4096] = out
    return y


def pad_chord(notes, start, length, level=0.09):
    n = int(length * SR); tt = np.arange(n) / SR; s = np.zeros(n)
    for m in notes:
        f = midi(m)
        s += np.sin(2 * np.pi * f * tt) + 0.5 * np.sin(2 * np.pi * f * 2.003 * tt) + 0.25 * np.sin(2 * np.pi * f * 0.5 * tt)
    a = np.minimum(1, tt / 1.2) * np.minimum(1, (length - tt) / 1.0)
    s *= a * (0.75 + 0.25 * np.sin(2 * np.pi * 0.21 * tt))
    put(s, start, level, -0.25); put(np.roll(s, 331), start, level, 0.25)


def pluck(m, start, level=0.16, pan=0.0):
    n = int(0.6 * SR); tt = np.arange(n) / SR
    x = saw(midi(m), n, 0.004) * np.exp(-tt * 7)
    x = x * (0.4 + 0.6 * np.exp(-tt * 18))
    put(x, start, level, pan)


def bass(m, start, length, level=0.32):
    n = int(length * SR); tt = np.arange(n) / SR
    x = np.sin(2 * np.pi * midi(m) * tt) + 0.3 * np.sign(np.sin(2 * np.pi * midi(m) * tt)) * 0.5
    x *= np.minimum(1, tt / 0.01) * np.exp(-tt * 2.2) * np.minimum(1, (length - tt) / 0.02)
    put(x, start, level)


def boom(start, level=1.0):
    n = int(3.5 * SR); tt = np.arange(n) / SR
    f = 30 + 70 * np.exp(-tt * 9); ph = 2 * np.pi * np.cumsum(f) / SR
    x = np.sin(ph) * np.exp(-tt * 1.6) + rng.standard_normal(n) * np.exp(-tt * 5) * 0.35
    put(x, start, level)


def riser(start, length, level=0.12):
    n = int(length * SR); tt = np.arange(n) / SR
    noise = rng.standard_normal(n); k = np.linspace(0, 1, n) ** 2
    tone = np.sin(2 * np.pi * np.cumsum(200 + 1400 * k) / SR) * 0.3
    put((noise * 0.6 + tone) * k, start, level)


# ---- arrangement ----
chords = [[57, 60, 64], [53, 57, 60], [48, 52, 55, 60], [55, 59, 62]]  # Am F C G
roots = [45, 41, 48, 43]
# intro swell
pad_chord([45, 52, 57, 60, 64], 0.0, 8.5, 0.07)
riser(0.6, 3.4, 0.16)
boom(4.0, 0.9); put(clap(0.5), 4.0)
# groove 8..38
for bar in range(15):
    b0 = 8 + bar * 2 * BEAT * 2  # a bar = 4 beats = 2 s
    ch = chords[bar % 4]; rt = roots[bar % 4]
    pad_chord(ch, b0, 2.1, 0.05)
    drop = b0 >= 32
    for k in range(4):
        bt = b0 + k * BEAT
        put(kick(1.0 if drop else 0.85), bt)
        put(hat(0.16 if drop else 0.12), bt + BEAT / 2, pan=0.35)
        if drop: put(hat(0.07, 160), bt + BEAT / 4, pan=-0.35); put(hat(0.07, 160), bt + 3 * BEAT / 4, pan=-0.35)
        if k in (1, 3): put(clap(0.32 if drop else 0.26), bt, pan=0.05)
        bass(rt if k % 2 == 0 else rt + 12, bt, BEAT * 0.9, 0.30)
        bass(rt + 7, bt + BEAT / 2, BEAT * 0.4, 0.18)
    # a sparse pluck motif, busier in the drop
    motif = [ch[-1] + 12, ch[1] + 12, ch[0] + 24, ch[1] + 12] if bar % 2 else [ch[0] + 12, ch[-1] + 12]
    step = BEAT / 2 if drop else BEAT
    for i, m in enumerate(motif * (2 if drop else 1)):
        pluck(m, b0 + i * step, 0.13, -0.4 + 0.8 * (i % 2))
riser(35.0, 3.0, 0.18)
boom(38.0, 1.0); put(clap(0.45), 38.0)
pad_chord([45, 52, 57, 60, 64, 69], 38.0, 8.0, 0.08)

# ---- mix: a stereo convolution reverb (synthetic decaying noise), then glue ----
ir_n = int(2.4 * SR); irt = np.arange(ir_n) / SR
irL = rng.standard_normal(ir_n) * np.exp(-irt * 2.6); irR = rng.standard_normal(ir_n) * np.exp(-irt * 2.6)
irL[: int(0.012 * SR)] = 0; irR[: int(0.017 * SR)] = 0


def conv(x, ir):
    m = len(x) + len(ir) - 1; nfft = 1 << (m - 1).bit_length()
    y = np.fft.irfft(np.fft.rfft(x, nfft) * np.fft.rfft(ir, nfft), nfft)[: len(x)]
    return y / np.sqrt(np.sum(ir ** 2))


wetL, wetR = conv(L, irL), conv(R, irR)
mixL, mixR = L + 0.22 * wetL, R + 0.22 * wetR
fade_in = np.minimum(1, t / 0.8); fade_out = np.clip((DUR - t) / 3.0, 0, 1)
mixL *= fade_in * fade_out; mixR *= fade_in * fade_out
# soft clip glue, then peak-normalize to -1 dBFS
mixL, mixR = np.tanh(mixL * 0.9), np.tanh(mixR * 0.9)
pk = max(np.abs(mixL).max(), np.abs(mixR).max()); g = 10 ** (-1 / 20) / pk
mixL *= g; mixR *= g
pcm = (np.stack([mixL, mixR], 1) * 32767).round().astype("<i2")

import wave
with wave.open("music.wav", "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR); w.writeframes(pcm.tobytes())

# ---- features for the picture ----
mono = 0.5 * (mixL + mixR); hop = SR // FPS; win = 8192; frames = int(DUR * FPS)
edges = np.geomspace(60, 16000, 33); freqs = np.fft.rfftfreq(win, 1 / SR); hann = np.hanning(win)
bands, level = [], []
for f in range(frames):
    c = f * hop; seg = mono[max(0, c - win // 2): c + win // 2]
    if len(seg) < win: seg = np.pad(seg, (0, win - len(seg)))
    sp = np.abs(np.fft.rfft(seg * hann))
    b = [float(np.sqrt(np.mean(sp[(freqs >= edges[k]) & (freqs < edges[k + 1])] ** 2) + 1e-12)) for k in range(32)]
    bands.append(b); level.append(float(np.sqrt(np.mean(seg ** 2))))
B = np.log10(np.array(bands) + 1e-6); lo = np.percentile(B, 20, axis=0); hi = np.percentile(B, 99.5, axis=0); B = np.clip((B - lo) / (hi - lo), 0, 1) ** 1.4   # per band: every band uses its own range
lev = np.array(level); lev = np.clip(lev / np.percentile(lev, 99), 0, 1)
beats = [8 + k * BEAT for k in range(60)]
pulse = []
for f in range(frames):
    tt = f / FPS; d = min([tt - b for b in beats if b <= tt] or [9])
    pulse.append(float(np.exp(-d * 9)) if tt >= 8 and tt < 38.2 else 0.0)
json.dump({"fps": FPS, "dur": DUR, "bands": [[round(x, 3) for x in r] for r in B.tolist()], "level": [round(x, 3) for x in lev.tolist()],
           "pulse": [round(x, 3) for x in pulse], "hits": [4.0, 38.0]}, open("features.json", "w"))
print("music.wav and features.json:", frames, "frames")
