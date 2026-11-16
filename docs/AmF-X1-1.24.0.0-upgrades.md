# AmF X1 1.24.0.0: what was added and how to use it

AMFB Audio · written 16 November 2026 · for the build on the working branch

This document lists the upgrades in AmF X1 1.24.0.0, says how to use the main ones, and says plainly what was tested and what was not.

## 1. In short

- The phone app finds the computer on the Wi-Fi by itself, follows it when its address changes, and shows how it linked up (LINK TIMELINE).
- The phone handles several consoles: a console bar shows each one and switches in one tap.
- The console lists the phones and browsers linked to it (CONNECTED PHONES), with DISCONNECT and FORGET.
- Scenes can be made and edited on the phone without a console, and recalled when one is linked.
- The web remote has a meter bridge on every page and the console's sound settings.
- WINDOWS VOLUME on the console and both remotes, with GAIN MODE: the Windows slider as a trim after the master.
- The engine needs much less CPU, most of all on older AMD and Intel processors. The sound is unchanged.

## 2. Phone app (Android)

- **Finds the console by itself.** When the mixer opens, the phone searches its Wi-Fi network for the computer (about a second). A console it paired with before connects at once, even after the router gave the computer a new address: each console has a fixed console ID. A console it never paired with asks for its PIN once. While not linked, the phone keeps looking (after 3, 6 and 12 s, then every 20 s), so it links up as soon as the computer starts PHONE REMOTE. Settings: "Find the console on the Wi-Fi by itself" (on by default).
- **LINK TIMELINE** under the status line: searching, found or moved, connected · live sync, disconnected.
- **Several consoles.** The console bar at the top of the mixer shows every paired console: LINKED (green), on this Wi-Fi (cyan), away (grey), and NEW · PIN for a console found but not paired. Tap one to move the live link to it. The phone keeps one live link at a time.
- **Names.** A console is shown by the name the phone has for it, else by its computer's name (STUDIO-PC, STAGE-PC).
- **Disconnected by the console.** If the console presses DISCONNECT or FORGET, the phone says so and does not reconnect while the console holds it out (2 minutes for DISCONNECT).
- **Offline scenes.** SCENES opens without a console. NEW SCENE starts from the last console the phone saw (its channel names and levels). EDIT sets each channel's name, level and mute and the master. One scene can be marked RECALL ON CONNECT: when a console is linked the phone asks first (RECALL / LATER / DON'T RECALL), so a live console never changes without a tap.
- **WINDOWS on the MASTER page:** the computer's Windows volume and the GAIN MODE switch.

## 3. The console

- **NETWORK → CONNECTED PHONES:** every phone and browser linked right now, with its name, address and how long it has been linked. DISCONNECT drops it at once; that pairing is refused for 2 minutes (clear message, not counted as a wrong token). FORGET revokes the pairing: that phone needs the PIN again. Revoking no longer restarts the service, so the other phones stay linked.
- **SETTINGS → Windows volume:** a slider for the Windows output volume, both ways (the keyboard's volume keys move it too).
- **SETTINGS → GAIN MODE:** the Windows volume becomes an extra gain after the master; the fader stays where it is. 100% = 0 dB, 50% = −9 dB, 25% = −18 dB, 0% = off. Useful with ASIO or a USB interface, which Windows' own volume does not reach. When the console plays through Windows' own mixer on the default output (WASAPI shared), Windows already sets that level, so no trim is added (no double cut). The MIXER's master shows WIN % and the trim.
- **Settings follow the remotes.** Stereo width, DSP MODE, output word length, DC blocker, AUTO LEVEL, GAIN MODE and the Windows mute rule changed on a remote reach the console in about 50 ms, and the other way round.

## 4. Web remote (browser)

- **Meter bridge** under the tabs on every page: each channel (peak, RMS, peak-hold line, a clip light that stays on), the master L / R with its peak in dB and the number of clips. Tap it to reset peaks and clips. SETTINGS: bridge on / off, peak hold 1 s, 2 s, 5 s or HOLD.
- **SETTINGS → CONSOLE SOUND:** stereo width, DSP MODE (digital / analog), output word length (float, 16, 24), DC blocker, AUTO LEVEL (target, lift), clip point, meter peak hold.
- **SETTINGS → WINDOWS:** Windows volume, GAIN MODE with the trim it adds, and "fader at −∞ mutes Windows".
- The browser shows the console's DISCONNECT and does not reconnect during the hold. It tells the console its name, so CONNECTED PHONES lists it by name.

## 5. Engine: less CPU

Measured with `engine_bench` (48 kHz, 256 frames, 32 channels with EQ, compressor and gate), old and new code alternated on the same machine:

| Case | 1.23.0.1 | 1.24.0.0 | Change |
|---|---|---|---|
| 32 channels playing | 687 µs per block | 451 µs | −35% |
| 4 playing, 28 silent | 717 µs | 95 µs | −87% |
| 4 playing, 28 muted (silent inputs) | 704 µs | 91 µs | −87% |

- The compressor, gate and the master's de-esser and compressor computed dB with `log10` / `pow` on every sample; that was about half of the engine's work, and slower still on processors without FMA. They now use plain-arithmetic versions (within 1e-7 dB of the library).
- Only EQ bands that change the sound are run: of 13 per channel, bands that are off, or a bell / shelf at 0 dB, are skipped.
- A channel with digital silence at its input, once its delay line has played out and its output is below −140 dBFS, rests until sound returns (in the same block). A muted channel that receives sound keeps running (meters, pre-fader sends).
- The build stays plain x86-64 (SSE2): it needs no newer instruction set, so it runs on any 64-bit AMD or Intel processor.

## 6. How it was tested

- Engine: all 35 tests pass, including new ones for the fast dB math, idle channels, EQ bands, connected phones (list, disconnect, hold, forget), console settings, Windows volume and GAIN MODE.
- Console self-check passes: Windows volume (with a stand-in Windows slider), settings sync both ways (about 50 ms), CONNECTED PHONES (a phone paired with the PIN, listed, disconnected, forgotten), live sync.
- Phone app: 54 tests pass, including end-to-end runs against the real console: found on the network, followed from an address that no longer exists, fader phone → console and console → phone; two real consoles side by side (bar, switch, disconnect).
- Web remote: browser checks of the meter bridge, its reset and settings, DISCONNECT, and every new setting applied on the console.

**Not tested here:** the Windows volume and GAIN MODE on a real Windows PC (the build environment is Linux; the Windows code uses the same documented endpoint interface as the existing Windows mute and was compiled for Windows), the new setup run on Windows, and the speed-up measured on an actual older processor or with the Windows (MinGW) math library.

## 7. Files

- `releases/windows/AmF-X1-1.24.0.0-setup.exe`, `AmF-X1-1.24.0.0-windows-x64.zip` (unsigned: sign them with `scripts/sign-windows.js`).
- `releases/android/AmF-X1-1.24.0.0-b1-android.apk`, `amfx1-sdk-1.24.0.0.aar`.
- `releases/features.json` has the 1.24.0.0 entry; `releases/update.json` must be generated and signed again by you (`node scripts/make-update.js`).
