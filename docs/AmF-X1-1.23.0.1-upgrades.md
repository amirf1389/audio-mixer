# AmF X1 1.23.0.1: what was added and how to use it

AMFB Audio · written 12 November 2026 · for the build on the working branch

This document lists the upgrades made to AmF X1 and to its two servers, says how to use the main ones, and says plainly what was tested and what was not.

## 1. In short

- The sound card is chosen by hand. Automatic detection, automatic ASIO selection and SMART START were removed; a scan only lists what is there.
- A new ENGINE CONTROL page gathers load, audio state, updates, upgrades and one-press tools for the selected channel.
- Five new rack effects: SOUND BARRIER (loudness with impact under a ceiling), ANALOG CHANNEL, PITCH SHIFTER and DUAL LISTEN EQ, beside the earlier SYNC DELAY and FEEDBACK KILLER, each with its own panel.
- Licences can be activated with a 25-character product key, like boxed software. BASIC runs 8 channels, PRO 16, STUDIO and MASTER 32.
- The network service is named AmF X1 Network. Phones pair with a six-digit PIN that changes every minute and get their own token.
- The licence server has a customer portal, a management portal and a hardened nginx front for licence.amirfakhribadri.my. The OTA server has a management portal.

## 2. The console: mixer and plug-ins

- PLUGIN RACK: SOUND BARRIER. PUSH raises the level up to 18 dB, IMPACT keeps the attack of each hit, SMOOTH holds a sticking-out treble back so that loud does not turn harsh, CEILING is a look-ahead limiter nothing passes. AUTO is the run-up: the push follows the music by itself until the barrier is working by the amount asked for. It delays the channel by 3 ms. One press on ENGINE CONTROL puts it on the selected channel.
- ACOUSTICS page: log sine sweep and stepped sine (amplitude, phase, THD, THD+N), RTA to 1/48 octave with RMS averaging and level alignment, impulse response, ETC, RT60 / Topt, delay compensation, waterfall, spectrogram, room simulator, auto EQ with target curves (20 - 400 Hz) set on a strip or exported as REW / miniDSP text and JSON, mic calibration files, SPL offset, offline sweep WAV
- MIXER: MUTE on the master strip; LIMIT CHANNELS (every channel limiter on or off in one press) and AUTO -INF (channels without an input on this sound card go to -inf) in the actions bar. SYNC DELAY: a new effect with TEMPO, NOTE value, FEEDBACK, TONE and WIDTH. TONE shows the ten MIC EQ faders on the same page. NETWORK: CASCADE links two consoles (A / B), each hearing the other's master on strips 31-32.
- MIXER: MUTE ALL now mutes every channel even when some were already muted, and UNMUTE ALL gives the earlier mutes back.
- PLUGIN RACK: every edition, BASIC included, can now fill all 64 slots on a channel.
- PLUGINS: a large plug-in folder gets 3 minutes to scan instead of 30 seconds, so ADD and RESCAN no longer give up on a big collection.
- MIXER: the master VU meter is back at the top of the MASTER strip, above the buttons.
- PLUGINS: ADD and RESCAN now search sub-folders (plug-ins sorted by maker are found), and a path pasted in quotes is accepted.
- MIXER: the VU needle turns on the cap in the middle of the meter and stays on the scale.
- PLUGIN RACK: ANALOG CHANNEL. DRIVE into a soft saturation that leaves quiet signals at their level, BIAS for even harmonics, TONE for the treble loss of transformers and tape, OUTPUT trim.
- PLUGIN RACK: panels finished like real rack gear. Reverbs in brushed aluminium, delays in deep green enamel with cream lettering, modulation in midnight blue, saturation and amps in black with gold lettering, the feedback killer in workshop grey; each with its live display (saturation curve, reverb tail, modulation sweep).
- ENGINE CONTROL page: heat (the audio load, two minutes of it), audio, updates with the update server link, upgrades, and one-press modulation, noise control and pitch control for the selected channel.
- PLUGIN RACK: PITCH SHIFTER, up or down 12 semitones with a fine control, without changing the speed.
- PLUGIN RACK: DUAL LISTEN EQ. Bass and treble listen to the music and set themselves against the middle; BASS and TREBLE say where they should stand, RANGE how far they may move, SPEED how fast. One press on ENGINE CONTROL puts it on the selected channel.

## 3. The console: licence, settings and safety

- Updates like a mixing console: a found update downloads (optionally by itself), INSTALL AND RESTART closes the console, installs silently and boots it again with UPDATED TO on the boot screen; the setup is checked against the signed manifest's sha256 again before it runs
- Updates are now fetched automatically when the signed manifest shows a newer version. INSTALL AND RESTART still asks first.
- ACCOUNT: PIN AT START. With your own PIN set, the console starts locked until the PIN is entered.
- Licence server: PRO TRIAL · 1 YEAR. A trial key always ends (one year at most) and is for one computer.
- DIAGNOSTICS: the FACTORY SETUP card is gone. FACTORY RESET is now complete: every setting including the language and the network tokens goes back to the factory state and the first-start setup runs again; only the licence stays.
- Uninstall (Windows): every uninstall is a factory reset. Settings, console, scenes, presets and caches are removed, so a later install starts as new; the uninstaller only asks whether to keep the licence key.
- SETTINGS holds FACTORY RESET (moved from DIAGNOSTICS). MIC EQ is reached from TONE as DYNAMIC MIC. AI MIX has a brain icon.
- Licence: BASIC runs channels 1 to 8, PRO 1 to 16, STUDIO and MASTER all 32. A product key (XXXXX-XXXXX-XXXXX-XXXXX-XXXXX) can be typed on LICENCE and is traded at the licence server for the full key.
- Updates: the update server link is open to any https address; the update list must still carry the vendor's signature.
- Licence server: a customer portal at /portal/ to activate a product key (copy the key or save it as a key file the console picks up), check a key, and see the newest release with its checksums.
- Licence server: portal management in the admin page. Product keys made, used and unused, activations and refusals of the last day, what the portal's Updates tab shows, and a new product key per licence that replaces the old one.
- Licence server: a hardened nginx front (deploy/license/nginx-license.conf). Only the app's four calls, the customer portal and /health are open over https; management is never published.
- The console knows the vendor's domain: licence.amirfakhribadri.my is the licence server unless another is typed, and amirfakhribadri.my / ota.amirfakhribadri.my are accepted for updates.
- Licence server: the management page is a portal with a side bar (Dashboard, Ending soon, Issue a key, Keys on record, Customer portal, Activity) and lists the licences that end within 30 days, the nearest first.

## 4. Network and remote control

- NETWORK > AUDIO + MIDI: network audio on the open AES67 profile (send the master, receive a stream into strips, list the streams announced on the network), network MIDI (RTP-MIDI) and the computer's MIDI ports with a default controller map and LEARN. C SDK version 5 adds the same calls. No PTP clock and no encryption; Dante, NDI and SRT are not in this build (Dante devices are reached through their AES67 mode).
- DAEMON: named access tokens beside the master token. --new-token NAME [--role control|view] [--days N], --list-tokens, --revoke-token ID; a view token only reads the meters; tokens are stored as hashes and shown once.
- DAEMON (Windows): amixos-daemon.exe no longer fails with "entry point not found"; it carries its own C++ runtime.
- DAEMON security: control accepts the token in the header only (not in the URL), wrong tokens are slowed and refused with 429 after 20 in a minute, short master tokens are refused, and replies carry nosniff / no-frame / no-referrer headers.
- NETWORK: the service program is now AmF X1 Network.exe, and its tokens are managed in the console (NETWORK > REMOTE CONTROL): make a token per phone with CONTROL or VIEW and an end date, see the list, revoke one.
- Security (AmF X1 Network): web pages from other sites can no longer read the console's state or meters through the browser, and requests that name a made-up host are refused (DNS rebinding).
- NETWORK: a token stays on screen for 10 minutes, and a named token works only from the address that used it first.
- NETWORK: the service's status line is readable (no stray characters) and names AmF X1 Network.
- NETWORK: pairing PIN for the remote. Six digits, a new one every minute, shown while the service runs. A phone sends it once and gets its own 30-day token tied to its address; one phone per PIN, and five wrong tries close pairing until the next PIN.

## 5. The console: sound card and audio

- SOUND CARD > ASIO ROUTING: OPEN (default) opens the ASIO driver you pick first, ASIO4ALL, FlexASIO or the manufacturer's own driver (Realtek included), with the other paths as fallback; GUARDED keeps the earlier rules. Auto detect on DirectSound leaves a headset's hands-free side off.
- SOUND CARD: a mixer of another maker connected through its own ASIO driver is recognised (Behringer X32 / X AIR / WING, Midas M32 / MR18, Yamaha, Soundcraft, Allen & Heath, PreSonus, Zoom LiveTrak, Tascam Model, Mackie, QSC, RODE); CONNECT + ASSIGN CHANNELS opens every channel (32 at most) and puts desk channel n on strip n, the master on the desk's USB returns 1-2.
- SOUND CARD: I/O CHECK shows what arrives on each input and leaves on each output of the audio interface, and whether the console keeps real time.
- SOUND CARD: MANUAL selection. The ASIO DRIVERS list shows every driver found; nothing is opened until you press OPEN on one, and no other driver is tried in its place.
- SOUND CARD: AUDIO / MIDI SETTINGS window. Mute audio input, audio device type, output, input with level, active output and input channel pairs, sample rate, buffer size and MIDI inputs, all chosen by hand and started with APPLY.
- AUDIO / MIDI SETTINGS: a Test button plays 1 kHz at -20 dBFS for one second on outputs 1 + 2. The window, the ASIO DRIVERS list and MANUAL are translated into all 21 languages.
- SOUND CARD: DAC detection. USB DACs and headphone DACs are listed with their output count and top sample rate, with OPEN for outputs only.
- SOUND CARD: DSP MODE. DIGITAL leaves the mix as computed; ANALOG rounds the peaks off softly on the master.
- SOUND CARD: automatic selection is gone. The device is always chosen by hand: OPEN starts a driver, the same button turns it off; a driver whose device was in use is read again when it is opened, so the real channel count is used.
- SOUND CARD: SMART START and the AFTER ASIO fallback are gone. SCAN is discovery only: it lists what is there and opens nothing; START opens exactly the device you chose.
- Start: the opening animation is shorter (1.8 s), a click goes straight into the interface, and the interface opens after 3 s even if the animation stalls.
- WASAPI: the headset's microphone is always kept off. A headset or Bluetooth microphone is never opened on WASAPI, and with headphones as the output the system's default microphone is not opened; SOUND CARD says so when it happens.
- SOUND CARD and SETTINGS redesigned: SOUND CARD opens with one status strip (what runs, channels, rate, block, latency, START / STOP, AUDIO / MIDI SETTINGS) and a DEVICES card; the driver ladder and routing rows are gone. SETTINGS is in two columns with FACTORY RESET in its own panel.

## 6. Servers and web pages

- OTA server: the management page is a portal with a side bar. Dashboard (live version, channels, files, update checks and downloads of the week, licence requests), Channels, Releases, Publish, Files, Requests (search, copy the machine code, export CSV), Reach, Health and Audit.
- Customer portal, Updates: a version check (is my copy up to date?), downloads by system with size and a copy button for the checksum, how to check a download, and what is new release by release.

## 7. Earlier in this version

- 360 AUDIO: binaural placement of every strip for headphones (azimuth, elevation, distance, spread), also in the SDK (API v4)
- RTA EQ has its own EQ rack on the master (PROGRAM EQ); TONE (EQ) stays the microphone channel EQ
- MASTERING rack after the program EQ, one card per module: tone shelves, bass mono, de-esser, glue compressor, exciter, saturation and output, and the master limiter with ceiling, release and a gain-reduction meter; six presets
- Input / output detection names what is connected (headphones, headset, speakers, microphone, audio interface, Bluetooth, digital out) on ASIO, WASAPI and DirectSound, shows it in the header, and always pairs an input with the output so every channel meters in real time
- 5.0 surround in the SDK (API v3): per-strip DEPTH, amix_mixer_process5
- Every console text follows the language menu: 21 languages with full interface tables
- WHAT'S NEW feed with a NEW badge in the header, managed from the website's release manager
- Windows: the colours load correctly (the theme was missing in the Windows build), drop-downs follow the theme, the update check has its TLS plug-in
- Boot animation with a soft chord on the master (each can be switched off on SETTINGS); factory setup: text fields follow the theme, the PIN can be changed again once set, the language list scrolls
- Never stuck: sound-card scans and driver starts run beside the interface with a time limit; a driver that does not answer is left behind and the console carries on
- SOFT RESTART in the header (Ctrl+Shift+R): rescan, restart the engine and replay the boot screen without closing the console
- AMFB SIGNATURE sound (the house sound on the master), offered in the factory setup and on MASTERING
- Equalizer APO: export and import of the EQ as its filter text, from the RTA EQ page
- The mixer logo on the program, the window, the task bar, the installer and the shortcuts
- Master fader at -inf switches everything off, the Windows default output included (its own volume control; put back when the fader comes up or the console closes)
- The real-time analyser on TONE (EQ) is on from the start
- Bluetooth start that does not give up at the first refusal: the device's own rate, 48 kHz, without the hands-free microphone, then the other Windows path, with a note of what was tried
- Phone remote over Wi-Fi from the NETWORK page, with a local policy: only this computer and private-network addresses are answered, and every control call needs the token (not encrypted: for your own network)
- OSC remote: with no address list only private-network senders are accepted
- Fifteen effects for every channel's rack, now with TUBE AMP (valve stage, tone stack, sagging power stage, cabinet), PRE AMP, POST AMP, HALL REVERB and ROOM REVERB beside reverb, delay, chorus, drive, flanger, phaser, tremolo, auto wah, bit crusher and exciter
- Fixed: the rack did not refresh by itself when a plug-in was added or removed
- VST3 / VST2 plug-ins are looked for after the window is up, beside the interface and with a time limit, so a slow or hanging plug-in cannot delay the start; the rack menu shows the format, the count and a rescan
- Plug-in editor as a vintage rack unit: wooden frame, cream faceplate, outlined sections with skirted knobs and scale marks, a PROGRAM window with - / + for the presets, an A / B selector, a lamp with an ON / OFF switch and a needle meter; hosted VST plug-ins open in the same panel
- Sound card RESCUE: when no path of the start plan opens (ASIO will not lock, the endpoint is refused), the console now scans the device list and tries the Realtek ASIO driver directly, then RWS (Realtek through Windows shared mode), then the system's default endpoints, so it does not stay without input and output. Nothing is installed or changed on the system.
- ACOUSTICS mic sweep fixed: the measured strip is recorded as the sound card delivers it (before the channel's trim, EQ, gate, compressor and plug-ins), it is muted in the mix while the sweep plays so the microphone does not feed back, one more second is recorded so a slow driver cannot cut the top of the sweep, and an info box reports peak, level, noise, signal-to-noise, the input in use and what to do when it is silent, clipped, too quiet or noisy.
- FILE, VIEW and INFO menus in the header: save / load the console file, scenes, presets, soft restart, quit; jump to pages, light / dark look, tooltips, full screen; About, the sound card in use, what's new, help, licence.
- Vendor side: the licence server now has licence management (keys on record with customer details encrypted, revoke / restore, free a computer's seat, totals, a tamper-evident activity log) and a management page at /admin/ on the server; the OTA management page can attach the feature feed to a release. The interface's buttons were checked against the engine: no dead call.
- EQ layout: each channel has its own 3-band CHANNEL EQ (low shelf, mid with frequency, high shelf) at the top of TONE; the 10-band MIC EQ is a rack item (MIC EQ in the plug-in list, OPEN goes to its page) and no longer shares bands with the channel EQ; RTA EQ is the main EQ on the master only, with an RTA EQ button on the master strip.
- AMFB VOLUME ENHANCER on the master (ENHANCER button on the master strip): LOUDNESS into the limiter with a CEILING, ANALOG drive, SOFTEN and MIX of the glue compressor, LOW / MID / HIGH, MONO BELOW and WIDTH, output and limiting meters, six presets, INIT and bypass. The master chain gained a 1 kHz mid band and a parallel compressor mix for it.
- AMFB MASTER ANALYZER (ANALYZER button on the master strip): short-term, integrated and momentary loudness against a target (streaming, broadcast, podcast, live), range and true peak, left / right peak and RMS, stereo correlation, a spectrogram of the last 15 seconds and the loudness history of the last 3 minutes, with PAUSE and CLEAR.
- Stability: a rare crash of the audio engine is fixed (a burst of changes, such as loading a whole console, could lose the message that detaches an effect); the acoustics distortion analysis no longer reads past its spectrum.
- FILE > Load the console from that file now finds the file FILE > Save writes. The opening animation plays to its end (no skip) and can be switched off in the FACTORY SETUP, which also sets the ASIO routing and no longer has a SKIP button. The FILE menu has no Quit entry: close the window to leave.
- PLUG-INS: OPEN VST FOLDER opens the console's own plug-in folder (created on first use and saved, so it is scanned at every start); each listed folder has an OPEN button.
- Plug-ins come back properly: the console (faders, racks and what each plug-in holds) is now written two seconds after any change and again when the window closes, not only on a clean exit, so a crash, a forced shutdown or an update restart loses nothing; a plug-in whose file was missing waits in its slot with its settings and is tried again after every scan or with a click on WHY?; a loaded plug-in's bypass is restored with a console.
- NOISE GATE: CLOSE NOISE sets the gate 6 dB above the hum and hiss the channel carries while nobody plays. The plug-in editor can be moved by its top edge. ACOUSTICS no longer has the AUTO EQ tab. A new installation goes straight to the console; the factory setup runs after a FACTORY RESET.
- Top bar: FILE, EDIT (undo, redo, A / B, mute all, clear solo, clear clips), VIEW, TOOLS (plug-ins, sound card, effects, mastering, samples, Volume Enhancer, Master Analyzer, settings), HELP and ABOUT. Back again: the MASTER / CHANNEL switch on RTA EQ and the AUTO EQ tab on ACOUSTICS.
- FEEDBACK KILLER: a new rack effect that finds the howl of a microphone hearing its own loudspeaker (one pure, steady tone standing out for a third of a second) and notches it out with up to six filters; SENSITIVITY, DEPTH, WIDTH and HOLD / LIVE mode.
- 360 AUDIO: CINEMA HALL puts 20 objects on strips 1-20, ten fixed points (left, centre, right, sides, rear, overhead) and ten objects that travel around the listener in real time, with a hall reverb on FX 8; HEAD TRACK keeps the scene in the room while the head turns (TURN, NOD, or drag on the map); a live map shows every object.
- STEREO PAIRS: 'Match the stereo partner' in a strip's menu gives channels 1+2, 3+4 ... the same processing and rack, panned to the other side; while LINKED, a plug-in put in, removed, bypassed or adjusted on one side is done on the other (two mono copies, not one stereo plug-in). SOUND CARD: AUTO SELECT chooses which audio system auto detect picks from: BEST, ASIO, WASAPI, DIRECTSOUND or BLUETOOTH.
- RTA EQ fixed: on the master EQ the faders, diamonds and the FREQUENCY / Q / GAIN sliders now stay where they are moved (the page was not told about changes to the master's EQ, so they snapped back).
- BLUETOOTH: find audio devices by WASAPI or DirectSound, CONNECT to the best one, PAIR A DEVICE opens the system pairing page.
- Third-party VST plug-ins open their own editor window, which can be moved and closed; right click on EDIT shows the knob view.
- All pages translated: 1230 interface strings in 21 languages.
- SYNC DELAY has a DELAY TIME panel: the delay in milliseconds, the tempo, six note keys, TAP TEMPO and the echoes as they die away.

## 8. How to do the common things

### Start a sound card

1. Open SOUND CARD.
1. Press SCAN (DISCOVERY ONLY) if your interface is not in the ASIO DRIVERS list.
1. Press OPEN on the driver you want. The strip at the top shows what is running. The same button turns it off.
1. For WASAPI or DirectSound, or to choose channel pairs, rate and buffer, press AUDIO / MIDI SETTINGS, choose, and press APPLY. Test plays one second of 1 kHz.

### Activate a licence

- With a product key (XXXXX-XXXXX-XXXXX-XXXXX-XXXXX): open LICENCE, type it in the key box and press ACTIVATE PRODUCT KEY. The console asks the licence server for the full key and then checks that key itself.
- With a full key (it starts with AMIX1): paste it, press CHECK, then ACTIVATE. This works with no internet.
- On the web: open /portal/ on the licence server, type the product key, and copy the key or save it as a key file.

### Let a phone control the console

1. Open NETWORK and start the service (this computer only, or for phones on this network).
1. Read the PAIRING PIN on the page and type it into the phone within the minute. The phone gets its own token for 30 days, tied to its address.
1. To remove a phone, press REVOKE on its token. A token for a helper who should only watch the meters is made with VIEW.

### Publish an update

1. Build the packages and put them in releases/.
1. Sign the update list on your own computer: node scripts/make-update.js. The private key never leaves that computer.
1. Publish the list and the files (the OTA management portal: Files, then Publish).
1. Customers see it on the portal's Updates tab and in the console under ENGINE CONTROL → CHECK NOW. The console installs only a list that carries your signature and a file that matches its checksum.

### Put the licence server online

1. Point licence.amirfakhribadri.my at the server (an A record).
1. Get the certificate: sudo certbot certonly --nginx -d licence.amirfakhribadri.my
1. Start the licence server with LICENSE_HOST=127.0.0.1 LICENSE_PORT=8787 LICENSE_TRUST_PROXY=1.
1. Copy deploy/license/nginx-license.conf to /etc/nginx/conf.d/, run nginx -t, reload nginx.
1. Management (/admin/) is reached on the server itself or through an SSH tunnel. It is never published.

## 9. Security

- Licence keys are checked offline against the vendor's public key. A product key is only traded for such a key; the console still checks it.
- The licence register keeps hashes of product keys and of network tokens, never the keys themselves. Customer details are encrypted.
- Through nginx only the app's four calls, the customer portal and /health are open. /admin and /issue answer 404 there, with or without the token.
- The network service answers this computer and private addresses only, takes the control token from the header only, slows wrong tokens, refuses made-up host names and does not let other web sites read its replies.
- An update is installed only when its list is signed by the vendor and the file matches its checksum, whatever server it came from.

## 10. What was tested, and what was not

| Part | Tested | Not tested |
|---|---|---|
| Engine and effects | 21 automated test programs pass, including each new effect | Listening tests on real loudspeakers |
| Console | A full automated run of every page with checks for each new feature | Real Windows with a real ASIO interface, DAC or Bluetooth device |
| Licence server | 17 automated tests; every button of the management page; the portal in a real browser | A deployed server on the internet |
| nginx front | 15 checks against a running server with a test certificate | The real domain and certificate |
| OTA server | The existing suite (149 of 154; the 5 failures are for files that were deleted) and a browser run of every page | Publishing a real signed release |
| Installer | It compiles; its checksum is verified | An install and uninstall on a real PC |

Translations of the newest texts are still in English only. The Android remote has no screen for the pairing PIN yet.
