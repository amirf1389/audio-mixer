#!/usr/bin/env python3
"""AmF X1 teaser: title cards + product shots with slow push-ins, lower thirds, a synthesized music bed.
usage: make_teaser.py SCENES.json LOGO.png OUT.mp4"""
import json, os, subprocess, sys, tempfile

W, H, FPS = 1920, 1080, 30
BG, CYAN, AMBER, INK, MUTED = "0x040507", "0x00f2ff", "0xfbbf24", "0xf1f5f9", "0x94a3b8"
FONT_B = "/usr/share/fonts/opentype/inter/InterDisplay-Bold.otf"
FONT_M = "/usr/share/fonts/opentype/inter/InterDisplay-Medium.otf"
FONT_L = "/usr/share/fonts/opentype/inter/Inter-Light.otf"
MONO = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
XF = 0.6  # crossfade seconds


def sh(args):
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode:
        sys.exit("ffmpeg failed:\n" + r.stderr[-3000:])


def esc(t):
    return t.replace("\\", "\\\\").replace(":", "\\:").replace("'", "’").replace("%", "\\%").replace(",", "\\,")


def fade_text(text, font, size, color, x, y, t0, t1, dur_in=0.5):
    """text that slides up 24 px and fades in from t0, fades out before t1"""
    a = f"if(lt(t,{t0}),0,if(lt(t,{t0 + dur_in}),(t-{t0})/{dur_in},if(lt(t,{t1 - 0.4}),1,max(0,({t1}-t)/0.4))))"
    yy = f"{y}+24*max(0,1-(t-{t0})/{dur_in})"
    return (f"drawtext=fontfile={font}:text='{esc(text)}':fontsize={size}:fontcolor={color}:x={x}:y='{yy}':alpha='{a}'")


def card(path, dur, lines, logo=None):
    """a title card: dark field, a soft cyan glow, lines of text [(text, font, size, color, y, t0)]"""
    vf = [f"color=c={BG}:s={W}x{H}:r={FPS}:d={dur}[bg]",
          f"color=c={CYAN}@0.0:s={W}x{H}:r={FPS}:d={dur},format=rgba,"
          f"geq=r=0:g=242:b=255:a='26*exp(-((X-{W}/2)^2+(Y-{H}/2)^2)/(2*380^2))'[glow]",
          "[bg][glow]overlay[b0]"]
    last = "b0"
    if logo:
        vf.append(f"movie={logo}:loop=0,setpts=N/{FPS}/TB,scale=-1:150,format=rgba,fade=in:st=0.1:d=0.6:alpha=1[lg]")
        vf.append(f"[{last}][lg]overlay=(W-w)/2:{lines[0][4] - 190}[b1]"); last = "b1"
    txt = [fade_text(t, f, s, c, "(w-text_w)/2", y, t0, dur) for (t, f, s, c, y, t0) in lines]
    vf.append(f"[{last}]" + ",".join(txt) + ",format=yuv420p[v]")
    sh(["ffmpeg", "-y", "-loglevel", "error", "-filter_complex", ";".join(vf), "-map", "[v]", "-t", str(dur),
        "-c:v", "libx264", "-preset", "medium", "-crf", "16", "-r", str(FPS), path])


def shot(path, img, dur, kicker, title, sub, zoom_from=1.0, zoom_to=1.08, fx=0.5, fy=0.45):
    """a product shot: the screenshot on a dark stage with a thin border and shadow, a slow push-in towards (fx, fy),
    a lower third (kicker, title, subtitle) and a cyan accent bar that wipes in"""
    n = int(dur * FPS)
    SH = 790  # the stage: the screenshot fits in it, the caption sits below on a clean field
    z = f"{zoom_from}+({zoom_to}-{zoom_from})*on/{n}"
    vf = [
        f"movie={img},scale=w={int(W * 0.84)}:h={SH - 70}:force_original_aspect_ratio=decrease:flags=lanczos,format=rgba,"
        f"pad=iw+4:ih+4:2:2:color=0x1e293b[sc]",
        f"color=c={BG}:s={W}x{SH}:r={FPS}:d={dur},format=rgba,"
        f"geq=r=0:g=242:b=255:a='18*exp(-((X-{W}/2)^2+(Y-{SH}/2)^2)/(2*520^2))'[st0]",
        f"color=c={BG}:s={W}x{SH}:r={FPS}:d={dur}[st1]", "[st1][st0]overlay[st]",
        f"[st][sc]overlay=(W-w)/2:(H-h)/2+20,scale={W * 2}:{SH * 2}:flags=lanczos,"
        f"zoompan=z='{z}':x='(iw-iw/zoom)*{fx}':y='(ih-ih/zoom)*{fy}':d={n}:s={W}x{SH}:fps={FPS},"
        f"pad={W}:{H}:0:0:color={BG}[zp]",
        f"color=c={CYAN}:s=6x128:r={FPS}:d={dur}[bar]",
        f"[zp][bar]overlay=x=96:y='{SH + 22}+128*max(0,1-(t-0.25)/0.35)':enable='gte(t,0.25)'[z2]",
    ]
    txt = [fade_text(kicker.upper(), MONO, 24, CYAN, 126, SH + 22, 0.30, dur),
           fade_text(title, FONT_B, 60, INK, 124, SH + 54, 0.45, dur),
           fade_text(sub, FONT_L, 30, MUTED, 126, SH + 128, 0.65, dur)]
    vf.append("[z2]" + ",".join(txt) + ",format=yuv420p[v]")
    sh(["ffmpeg", "-y", "-loglevel", "error", "-filter_complex", ";".join(vf), "-map", "[v]", "-t", str(dur),
        "-c:v", "libx264", "-preset", "medium", "-crf", "16", "-r", str(FPS), path])


def music(path, dur, bpm=112):
    """an original bed: sub pulse on the beat, a soft hat on the off-beats, a slow minor pad; fades at both ends"""
    b = 60.0 / bpm
    kick = f"0.55*sin(2*PI*(46+90*exp(-mod(t,{b})*28))*mod(t,{b}))*exp(-mod(t,{b})*7)"
    hat = f"0.05*(random(0)*2-1)*exp(-mod(t+{b / 2},{b})*60)"
    pad = ("0.07*(sin(2*PI*110*t)+0.8*sin(2*PI*130.81*t)+0.7*sin(2*PI*164.81*t)+0.5*sin(2*PI*220.5*t))"
           "*(0.6+0.4*sin(2*PI*0.25*t))")
    riser = f"0.04*sin(2*PI*(200+40*t)*t)*min(1,max(0,(t-{dur - 9})/8))"
    expr = f"({kick})*gte(t,2.2)+({hat})*gte(t,4.4)+{pad}+{riser}"
    sh(["ffmpeg", "-y", "-loglevel", "error", "-f", "lavfi", "-i", f"aevalsrc='{expr}|{expr}':s=48000:d={dur}",
        "-af", f"aecho=0.6:0.5:{int(b * 750)}:0.25,highpass=f=30,lowpass=f=12000,afade=t=in:d=1.5,"
               f"afade=t=out:st={dur - 2.5}:d=2.5,alimiter=limit=0.89,loudnorm=I=-16:TP=-1.5:LRA=9",
        "-ar", "48000", "-c:a", "aac", "-b:a", "256k", path])


def main():
    scenes = json.load(open(sys.argv[1])); logo = sys.argv[2]; out = sys.argv[3]
    tmp = tempfile.mkdtemp(prefix="teaser-")
    clips, durs = [], []
    for i, s in enumerate(scenes):
        p = os.path.join(tmp, f"c{i:02d}.mp4")
        if s["type"] == "card":
            card(p, s["dur"], [tuple(l) for l in s["lines"]], logo if s.get("logo") else None)
        else:
            shot(p, s["img"], s["dur"], s["kicker"], s["title"], s["sub"], s.get("z0", 1.0), s.get("z1", 1.08), s.get("fx", 0.5), s.get("fy", 0.45))
        clips.append(p); durs.append(s["dur"]); print(f"scene {i + 1}/{len(scenes)}", flush=True)
    # the cut: crossfades between every scene
    inputs = sum([["-i", c] for c in clips], [])
    fc, last, off = [], "0:v", 0.0
    for i in range(1, len(clips)):
        off += durs[i - 1] - XF
        fc.append(f"[{last}][{i}:v]xfade=transition={'fade' if i % 3 else 'smoothleft'}:duration={XF}:offset={off:.3f}[x{i}]")
        last = f"x{i}"
    total = sum(durs) - XF * (len(clips) - 1)
    video = os.path.join(tmp, "video.mp4"); audio = os.path.join(tmp, "music.m4a")
    sh(["ffmpeg", "-y", "-loglevel", "error", *inputs, "-filter_complex", ";".join(fc), "-map", f"[{last}]",
        "-c:v", "libx264", "-preset", "slow", "-crf", "18", "-pix_fmt", "yuv420p", video])
    music(audio, total)
    sh(["ffmpeg", "-y", "-loglevel", "error", "-i", video, "-i", audio, "-c:v", "copy", "-c:a", "copy", "-shortest",
        "-movflags", "+faststart", out])
    print(f"{out}: {total:.1f} s")


if __name__ == "__main__":
    main()
