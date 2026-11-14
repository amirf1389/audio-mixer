# AmF X1 hero film: the console on a laptop, live

A 24-second product shot at 3840×2160, 60 fps:
- A modern laptop sits on a dark studio desk under blue and amber rim light.
- On its screen, the real AmF X1 console plays a beat. A cursor drags a fader, sweeps an EQ band and turns the tube amp's knobs.
- The camera makes one cinematic macro push-in with depth of field.
- An end card closes the film.

Nothing on the screen is mocked up: the console is filmed running, with real mouse input.

1. **Film the console.** Replace `REC_DIR` in `hero.json` with an absolute folder for the master recordings, then run from the console's build folder:
   `QT_SCALE_FACTOR=2 QT_QPA_PLATFORM=offscreen ./amixos --lang en --film tools/hero/hero.json CAPDIR`
   - This writes `CAPDIR/<clip>/f0000.png ...` at 3200×2000 and a `cursor.json` per clip.
   - It also writes one master WAV per clip, exactly the clip's length. The film clock keeps sound and picture in step.
2. **Mix the soundtrack:** `python3 music_hero.py hero_music.wav REC_DIR/*-master.wav`. Pass the WAVs in filming order: mix, eq, amp.
3. **Render the frames** (Three.js from `../brand3d/node_modules`, after `npm install` there):
   `CAP=CAPDIR node render.js 0 479 frames & CAP=CAPDIR node render.js 480 959 frames & CAP=CAPDIR node render.js 960 1439 frames`
   - Set `W=1920 H=1080` for a quick preview.
4. **Encode:**
   `ffmpeg -framerate 60 -i frames/f%05d.png -i hero_music.wav -c:v libx264 -preset slow -crf 14 -pix_fmt yuv420p -c:a aac -b:a 320k -af loudnorm=I=-14:TP=-1 -shortest -movflags +faststart AmF-X1-hero-4k60.mp4`
