# A mix OS for Android (Kotlin + the engine's C SDK)

Two things in one app: a **local mixer** (the engine on the phone, microphone in) and the **remote control** of a console on the LAN
(`RemoteActivity`: connect to `amixos-daemon` with host, port and the token from `~/.amixos/daemon.token`; 32 strips with fader, pan,
mute, solo and meters from the WebSocket stream; master; devices open / stop; a channel page with EQ, compressor and gate). The
**Pairing:** press PAIR (PIN), type the six-digit PIN the console shows under NETWORK → PHONE REMOTE (new every minute), and the phone
gets a token of its own (30 days, revocable there); no long token to type. **FIND CONSOLE** looks for the computer on this Wi-Fi, so there is no address to type. Also: **BUSES** (AUX / FX level and mute),
**MONITOR** (control room source, level, DIM, MONO, MUTE), **CLEAR CLIPS**, rename a strip with a long press on its name, and the
phone follows the console (faders, mutes, solos, names moved on the computer or another phone show up within 3 s; the screen stays on
while connected). **SETTINGS** (from the home screen or the remote): connect on open, port, forget the console; strips shown (8 / 16 / 32), pan
sliders, meter range, how often to follow the console (or off); keep the screen on, vibrate on mute / solo, ask before STOP ENGINE;
the phone's own mixer's sample rate and block size; reset. Tap a channel name for its page (TRIM, POLARITY, DELAY, EQ band, compressor, gate, sends to AUX 1-8 pre / post); tap MASTER for
the limiter and its ceiling. **EQ CONTROLLER** (channel page): 8 bands on a curve you drag (across = frequency, up / down = gain), type, Q, presets (vocal,
speech, kick, acoustic guitar, loudness, telephone), flat, bypass; remembered per strip on the phone. **CALCULATOR** (offline): tempo to
delay times, speaker alignment (distance to ms, temperature), dB ratios and adding sources, wavelength and note, buffer latency,
recording size. **PLUG-INS** (remote): the console's channel racks (slot, plug-in, ON / BYPASS / ERROR), the VST3 / VST2 plug-ins its scan found
(search by name or vendor) and its extensions and built-in effects; read-only, saved by the console when the phone remote starts
(`remote-console.json`, served at `/api/console-info` with the token). **SCENES** (remote): save the console's mix on the phone (levels, pan, mute, solo, names, buses, master) and recall it.
**TOOLS** (no console needed): TUNER (YIN pitch from the microphone, needle in cents), SOUND LEVEL (estimate with a calibration
offset, max hold) and GENERATOR (sine, pink, white, sweep through the phone's output). **Screenshots on every test run, no phone needed:** `gradle testReleaseUnitTest` draws the real screens with
Robolectric into `screenshots-android/` (with `AMFX_DAEMON=<port> AMFX_TOKEN=<token>` and a running amixos-daemon, the remote is shown
connected, plus the channel and master pages). **No app needed:** any browser on the same Wi-Fi can open
`http://<console address>:8765/remote` (shown on the same page) and pair with the same PIN. The
remote client (`remote/DaemonClient.kt`, `MiniJson.kt`, `MiniWebSocket.kt`) is plain Kotlin with no dependencies and is tested on the
JVM against the real daemon: `jvm-test/run.sh`. It replaced the earlier React Native / Expo remote.


Open this folder in Android Studio, or with the SDK and NDK installed:

    cd android && gradle wrapper && ./gradlew assembleRelease      # app/build/outputs/apk/release/app-release.apk
    ./gradlew bundleRelease                                        # app/build/outputs/bundle/release/app-release.aab

What it is: the same C++ engine as the desktop console (`engine/cpp`, through `sdk/c/include/amix.h`) built for arm64-v8a and
x86_64 as `libamix.so` by the NDK, a small JNI layer (`app/src/main/cpp/amix_jni.cpp`), and a Kotlin app with one microphone strip
(fader, pan, mute), the master limiter and peak meters. Audio I/O goes through `AudioRecord` / `AudioTrack` in 256-frame blocks
(`AudioLoop.kt`); an AAudio backend inside the engine is the next step for lower latency.

The release build type signs with the debug key so the APK installs for testing; replace `signingConfig` with your own keystore for
the Play Store. Nothing in the app talks to the network. The CI job in `.github/workflows` builds the same project when the account's
Actions runners work again.
