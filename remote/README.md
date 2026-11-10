# A mix OS Remote (React Native, TypeScript)

A phone remote for the engine running on your PC as **amixos-daemon** (`engine/cpp/app/daemon`). The phone never carries audio:
it sends control (`POST /api/control`) and shows the meters (`WS /ws/meters`, 20 frames a second).

```
PC:    amixos-daemon --allow-lan            # the engine as a background service; token in ~/.amixos/daemon.token
Phone: Connect screen -> PC address, port 8765, token  -> Mixer / Channel / Devices
```

Screens: **Connect** (address, port, token, HTTP vs HTTPS), **Mixer** (strips with meters, faders, mute / solo, master with
limiter and loudness), **Channel** (name, pan, polarity, delay, 4-band EQ, compressor, noise gate), **Devices** (engine status,
every device of every backend, open / stop).

## Security
* The daemon binds 127.0.0.1 unless started with `--allow-lan`. On a LAN the token protects control, **but plain HTTP is not
  encrypted**: use it only on a trusted home / studio network. Beyond that, put the nginx TLS front (`deploy/nginx`) in front
  of the daemon and switch the Connect screen to HTTPS.
* The token is typed on the phone once; this version keeps it in memory only (no secure storage yet).

## Build
```
cd remote && npm install
npm run typecheck              # tsc over the app
npm run test:client            # the TS client against a real amixos-daemon (build engine/cpp first; AMIXOS_DAEMON=path overrides)
npx expo start                 # Expo dev client; npx expo run:android / run:ios for native builds
```
The client (`src/api/client.ts`) has no React Native imports, so the same code is tested under Node against the daemon.

## APK / AAB
GitHub Actions (`.github/workflows/build-apps.yml`, job `android`) builds `amixos-remote-debug.apk` (debug-signed: installs on any phone
with "unknown sources") and `amixos-remote-release-unsigned.aab`. Sign the AAB on your PC before Play with your upload key:
`jarsigner -keystore upload.jks amixos-remote-release-unsigned.aab upload` (or `apksigner` for an APK). The keystore never goes into the repository.
Locally: `npx expo prebuild --platform android && cd android && ./gradlew assembleDebug bundleRelease` (Android Studio's SDK + JDK 17).
