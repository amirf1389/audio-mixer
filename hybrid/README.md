# AmF X1 Hybrid Mixer

Sixteen stereo strips into one stereo bus. The first strips are the input pairs of the audio
device (ASIO, or WASAPI); the others carry the sound of programs running on the same PC
(Spotify, Apple Music, Roon, Deezer, a browser playing YouTube, TikTok or SoundCloud), captured
with Windows' own process-loopback function. No VST: plugins stay in the console's plugin rack.

```
hybrid/
  core/spsc_ring.hpp      lock-free ring, one writer, one reader
  core/shared.hpp         the shared memory block  Local\AMFB_Mixer_SharedMem  (parameters in, meters out)
  core/dsp.hpp            64-bit biquads (transposed direct form II), 7-band EQ, constant-power balance, Mid/Side
  core/meter.hpp          peak, RMS, 4x true peak, peak hold, phase correlation
  core/mixer.hpp          the mixer (audio thread: no locks, no allocation)
  engine/main.cpp         amfb-hybrid: opens the device through the AmF X1 engine backends
  engine/app_capture.*    Windows: one program's sound into a ring
  node/addon.cc           N-API add-on (attaches to the shared memory)
  node/server.js          control server: pairing PIN, sessions, meters   (127.0.0.1:8377)
  node/public/            the control page
  deploy/                 nginx front and ngrok example
  test/                   core_test.cpp, server.test.js
```

## Build

Engine (part of the engine build, any platform):

    cmake -S engine/cpp -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --target amfb-hybrid hybrid_core_test
    ctest --test-dir build -R hybrid_core

Windows with MSVC: the same two commands from a "x64 Native Tools" prompt
(`-G "Visual Studio 17 2022" -A x64`, then `--config Release`). The Windows installer build
(`scripts/build-console-windows.sh`) ships it as `AmF X1 Hybrid.exe`.

Add-on and control server (Node 18 or newer; on Windows also the "Desktop development with C++" tools):

    cd hybrid/node
    npm install -g node-gyp
    npm run build
    npm start

## Run

    "AmF X1 Hybrid.exe" --backend asio --device "BEHRINGER USB AUDIO" --rate 48000 --frames 480 --in 2
    "AmF X1 Hybrid.exe" --backend asio --in 2 --app 2=Spotify.exe --app 3=chrome.exe

Without `--app` the usual players are put on the strips after the device's inputs.
`--tone 3` puts a 440 Hz test tone on strip 3; `--backend null` runs without a sound card.

## Network control

`server.js` listens on this PC only. It prints a six-digit pairing PIN (also in
`~/.amfb-hybrid/pin`, readable by you alone). The PIN changes every minute and after every use.
A correct PIN gives the browser a session tied to its address; ten idle minutes end it.
Five wrong PINs from one address, or thirty in total, stop pairing for a minute.

From another device, go through nginx (HTTPS, `deploy/nginx-hybrid.conf`) or an ngrok tunnel
(`deploy/ngrok.yml.example`), and name the outside host: `AMFB_HOSTS=mixer.example.com`.
An ngrok address is reachable from the whole internet: the PIN is the only key, so keep the
tunnel closed when it is not in use.

## What it is, and what it is not

- A device strip at unity gain, flat EQ, centre balance and width 1 passes its samples bit-exact
  (tested). Any other setting changes the samples, as it must.
- A captured program is not on the device's clock. Its strip keeps a 20 ms cushion; when the
  program runs fast the cushion is trimmed, when it runs slow a block is silent and counted
  ("source underruns"). So program strips have about 20 ms plus the system's own delay; they are
  not zero-latency and not sample-locked. Device inputs have only the device's block.
- Capture sees a program and what it started. All tabs of one browser are one source: YouTube and
  TikTok in the same browser arrive on the same strip.
- Capture takes the sound a program sends to the speakers. Content Windows marks as protected
  arrives silent, and that is left alone.
- No kernel driver is part of this. A virtual sound card (WaveRT) has to be a signed driver built
  with the Windows Driver Kit and tested on real machines; process capture does the same job here
  without one.
- True peak follows ITU-R BS.1770's 4x oversampling with a 48-tap filter; it reads within about
  0.15 dB at a quarter of the sample rate (tested).

## Tests

    ctest --test-dir build -R hybrid_core                      # ring, filters, balance, true peak, mixer
    AMFB_ENGINE=build/hybrid/amfb-hybrid AMFB_ADDON=hybrid/node/build/Release/amfb_mixer.node \
      node --test hybrid/test/server.test.js                   # engine + add-on + server, end to end
