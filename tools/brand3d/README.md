# AmF X1 brand film (3D)

A 46-second 3D film rendered frame by frame with Three.js in headless Chromium, with an original synthesized soundtrack. The picture follows the music: every frame reads the soundtrack's level, beat and 32 spectrum bands.

1. Capture the console screenshots: `amixos --shots screenshots-audiofiles/teaser-src`.
2. In this folder, run `npm install`, then `python3 music.py`. This writes `music.wav` and `features.json`.
3. Render the frames, in two halves in parallel:
   `node render.js 0 689 frames & node render.js 690 1379 frames`
   Already-rendered frames are skipped, so an interrupted render resumes where it stopped.
4. Encode:
   `ffmpeg -framerate 30 -i frames/f%05d.png -i music.wav -c:v libx264 -crf 16 -preset slow -pix_fmt yuv420p -c:a aac -b:a 256k -shortest -movflags +faststart AmF-X1-brand-3d.mp4`

For the vertical 9:16 version (1080×1920: a wider camera, stacked stats and captions laid out for phones), add `VERT=1` and render into another folder:
`VERT=1 node render.js 0 689 framesV & VERT=1 node render.js 690 1379 framesV`
Then encode `framesV` the same way.

To render chosen frames for a check, pass a comma list: `node render.js 150,330,1290 0 keys`.

Timeline in seconds:
- 0–4: the AmF X1 mark is built in 3D.
- 4: the hit.
- 8–32: six console screens in a 3D gallery, one every 4 s.
- 32–38: a spectrum sculpture driven by the soundtrack, with the measured figures.
- 38–46: the end lockup.
