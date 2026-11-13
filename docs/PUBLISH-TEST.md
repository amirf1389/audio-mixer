# AmF X1 — test before you publish

Do these on a **real Windows PC** with the installer from the build folder. Tick each line. Anything that fails: note the step, what you saw, and send a screenshot.

## 1. Install and first start (10 min)
- [ ] Run the installer on a PC that never had AmF X1. It installs without warnings other than SmartScreen.
- [ ] First start shows the factory layout: CH1 DIGITAL AUDIO, CH2 MIC, CH3-4 LIVE, CH5-7 RESERVE.
- [ ] Licence: activate a test key, then deactivate it. Restart: the edition is remembered.
- [ ] Uninstall, reinstall: settings come back clean (factory reset on first start).

## 2. Sound in and out (15 min)
- [ ] Each driver opens and plays: **ASIO** (your interface), **WASAPI**, **DirectSound**, **Bluetooth** headphones.
- [ ] CH1 and CH2 show signal from inputs 1-2; master meters move; no crackle at buffer 256.
- [ ] Change the sample rate 44.1 → 48 → 96 kHz: sound continues, no crash.
- [ ] Unplug and replug the interface while playing: the app recovers or tells you clearly.

## 3. Audio files (10 min)
- [ ] STREAM & MEDIA plays a **FLAC**, **M4A**, **MP3**, **WAV**, **AIFF**.
- [ ] ARRANGE imports the same files on a track; they play in time.
- [ ] A channel's **SAMPLE PLAYER → YOUR FILE → BROWSE…** loads a FLAC and plays it.

## 4. Plug-ins (20 min)
- [ ] Open every plug-in in the rack once (39 built-in). Each opens as the three-unit stack, nothing overlaps, ✕ closes it.
- [ ] The power button: click → BYPASSED (sound unprocessed), click → ACTIVE.
- [ ] COMPARE A / B, PROGRAM ◁ ▷, RESET each work.
- [ ] **W-SHAPE TUNER** on the master: turn AMOUNT up and down — the tone changes, the **level stays about the same**.
- [ ] MAXIMIZER last on the master: the master peak never goes over the CEILING.
- [ ] Load one VST3 you own (and the Eclipsa VST3 if installed): it loads, makes sound, saves with the console.

## 5. 360 and IAMF (10 min, headphones)
- [ ] 360 AUDIO: move CH1 left / right / behind — you hear it move.
- [ ] USE IAMF ELEMENT on CH1: the dot turns violet; moving the dial turns the plug-in's knobs and back.

## 6. SOUND SCORE (5 min)
- [ ] Play a finished, released song through the master: the score reads **75-95**.
- [ ] Push the master into clipping: PEAKS drops to 0 and the tip says what to do.
- [ ] Silence: the badge shows "—".

## 7. Save, restart, stability (30 min)
- [ ] Save a console file, close, reopen: faders, plug-ins and their knobs come back.
- [ ] Leave it playing **30 minutes**: no dropouts (STABILITY stays 10/10), no memory climb in Task Manager.

## 8. Phone remote (10 min)
- [ ] Android remote connects (Samsung / Xiaomi / any you have); faders move both ways.
- [ ] Turn the phone screen off for 2 minutes: it reconnects when you turn it back on.

## 9. Publish
1. Say **"pr merge"** — everything goes into `main`.
2. Sign the update: `node scripts/make-update.js` (your key stays on your PC; never send it).
3. Upload the installer and `releases/update.json` to your site / cloud page.
4. Install from the website on a clean PC once more (step 1) — the public download is what users get.
