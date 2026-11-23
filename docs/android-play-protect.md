# Android: getting AmF X1 past Google Play Protect

Play Protect warns about an APK when it cannot tell who made it or has never seen it. Nothing in AmF X1 tries to hide from it, and nothing
should: the fix is to make the app clearly identifiable and clean.

## What the build already does
- Targets the current Android SDK (35), so no "built for an old version of Android" warning.
- Asks only for what it uses: the microphone (local mixer, tools) and the network (the console remote). No SMS, contacts, location,
  accessibility service, overlay or device-admin rights, nothing that loads code at run time.
- Plain HTTP is allowed for the console on the local network; known public hosts (amfb.audio, GitHub) must use HTTPS
  (`res/xml/network_security_config.xml`).
- **Release builds are signed with AMFB Audio's own key** when it is provided (below). An APK signed with Android's public *debug*
  certificate is the most common reason for the warning: never hand one out.

## 1. Create the release key once (keep it safe and backed up)
```
keytool -genkeypair -v -keystore amfx1-release.jks -alias amfx1 -keyalg RSA -keysize 4096 -validity 10000
```
Store the `.jks` and its passwords outside the repository (a password manager or your CI secrets). Losing it means updates can no longer
be installed over the existing app.

## 2. Build a signed release
```
export AMFX1_KEYSTORE=/path/to/amfx1-release.jks
export AMFX1_KEYSTORE_PASSWORD=...   AMFX1_KEY_ALIAS=amfx1   AMFX1_KEY_PASSWORD=...
cd android && gradle assembleRelease      # or bundleRelease for the Play Store (.aab)
apksigner verify --print-certs app/build/outputs/apk/release/app-release.apk   # must show your certificate, not "Android Debug"
```
Without `AMFX1_KEYSTORE` the release build stops with an error. For a local test only, `gradle assembleRelease -PallowDebugSigning=true`
builds a debug-signed APK named `*-release-UNSIGNED-TEST.apk`: never publish it (anyone can sign a replacement with the public debug key).

## 3. Distribute so Play Protect knows the app
- **Best: publish on Google Play** (closed or internal testing is enough to start). Apps from Play are scanned and trusted by
  Play Protect; use Play App Signing with your upload key.
- **Direct download (sideload):** keep signing every release with the same key, and if Play Protect still warns, submit the signed
  APK through Google's Play Protect appeal for developers (in the Play Console Help centre). After Google reviews it the warning stops
  for that signing key.
- Publish the SHA-256 of each APK next to the download (`releases/android/SHA256SUMS.txt`) so users can check the file.
