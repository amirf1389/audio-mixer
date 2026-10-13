# Google Play Protect and antivirus warnings for the Audio Mixer APK

Play Protect (and some antivirus apps) can warn about or block `AudioMixer-<version>-android.apk` although it is harmless. A scanner cannot read intentions; it
sees a **file installed outside Google Play**, signed by its **developer** (not by Google), that has a **microphone**, a **background service**, a **local web server**
and a **WebView**. New, rare apps with that profile are flagged by behaviour and reputation, not because of anything they do. This page is what the project did about
it, and what to do when the warning still appears.

## What the app does to give a scanner nothing to hold on to

| Signal | What the app does |
|---|---|
| Permissions | Only `INTERNET` (the page talks to the engine inside the app on 127.0.0.1), `RECORD_AUDIO` (inputs of the mixer), `WAKE_LOCK` and `FOREGROUND_SERVICE` (the audio engine with its notification). No SMS, contacts, call log, location, camera, storage, overlay, accessibility, package installer, all-apps query or boot start. `scripts/apk-audit.js` fails the build, and `bridge/test.js` fails the tests, when anything else is added. |
| Network | No blanket `android:usesCleartextTraffic="true"` ("this app may send unencrypted traffic to any host," a signal scanners read as reckless or C2-like). A network security config (`android/res/xml/network_security_config.xml`) permits cleartext (plain HTTP) to **this device only** — `127.0.0.1` / `localhost`, where the page's WebView talks to the engine bundled in the same app — and denies it everywhere else. Nothing else changes: an update check or a license key request already goes out only over `https`. `scripts/apk-audit.js`'s `auditNetworkConfig()` fails the build if that file is ever widened to another host. |
| Notification listener | The "now playing" access is a notification-listener component, which scanners rate as sensitive. It is **shipped switched off** (`android:enabled="false"`); only **LICENSE page > APP SAFETY > SWITCH ON** turns it on, and then Android asks you in its own settings. It reads no notification. |
| Components | No broadcast receiver and no content provider (boot / package receivers look like droppers); every component with an intent filter says `exported` explicitly; not debuggable, not test-only, no backup of app data. |
| APK content | Only the manifest, one `classes.dex`, resources and the page; no native library, no second dex, no downloaded code, no dynamic class loading (`DexClassLoader` / `InMemoryDexClassLoader` / `PathClassLoader`) — `scripts/apk-audit.js` scans the built `classes.dex` for those strings, since loading unshipped code is the one behaviour that turns "WebView app" into "dropper" for a scanner. The APK is not obfuscated or packed. |
| Signing | v1 + v2 + v3 signatures with one 3072-bit key that must stay the same from release to release. Its SHA-256 fingerprint is in `android/signing-fingerprint.txt`; `scripts/build-apk.js` prints it and warns loudly when a build is signed with a different key (a new key means "unknown signer" for Play Protect and a refused update for installed copies). |
| Transparency | LICENSE page > **APP SAFETY** (Android app only): package, version, installer, the certificate fingerprint of the installed copy, every permission with its reason and whether it is granted, and the steps below. |

## When the warning appears

1. **Check the file.** In the app: LICENSE > APP SAFETY > *signing certificate SHA-256*. It must equal `android/signing-fingerprint.txt` of this project. If it does not, do not install.
2. **Install anyway.** In the warning tap *More details* > *Install anyway*. If *Send app for scanning* is offered, choose it: Google scans the file and the warning usually disappears for everybody who has the same file.
3. **Report the false positive.** Play Protect settings > *Improve harmful app detection* on. Then send the APK and the text below to Google (Play Console help > *Appeal Play Protect warning*, or `https://support.google.com/googleplay/android-developer/contact/protect_appeals`) and, for antivirus apps, to the vendor's false-positive form. `node scripts/play-protect-appeal.js <path/to/app.apk>` fills the template below in with this exact build's version, file hash and signing fingerprint (and fails if the APK is not signed with the published key).
4. **Publish through Google Play** (the permanent fix): upload the same APK / an AAB signed with your upload key to a Play Console internal or closed test track. Google then signs and verifies the app, Play Protect knows it, and the warning for sideloaded copies fades with its reputation. Use the same package name `com.audiomixer.app`.

## Text for a false-positive report

> App: Audio Mixer (`com.audiomixer.app`), version <version>, SHA-256 of the APK <sha256 of the file>, signing certificate SHA-256 <android/signing-fingerprint.txt>.
> Open-source: https://github.com/amirf1389/audio-mixer. It is a virtual mixing console: a WebView shows a bundled web page; a small engine inside the app serves the page on
> 127.0.0.1 and reads / writes the phone's audio interfaces (microphone, USB audio, Bluetooth) for the mixer. Permissions: INTERNET, RECORD_AUDIO, WAKE_LOCK,
> FOREGROUND_SERVICE (see APP SAFETY in the app and `scripts/apk-audit.js`). No SMS, contacts, location, camera, storage, overlay, accessibility, installer or boot access; no
> receivers, no providers, no native code, no code loaded at run time (classes.dex holds no dynamic class loader). Unencrypted (HTTP) traffic is permitted to this device only
> (127.0.0.1 / localhost, a network security config) and denied to every other host; anything the app does elsewhere already goes out only over https. Please remove the Play
> Protect warning for this file.

## For the maintainer

- Keep the keystore (`dist/cache/signing`, or `ANDROID_KEYSTORE`, `ANDROID_KEYSTORE_PASS`, `ANDROID_KEY_ALIAS`) safe and use it for every release. Never commit it.
- `node scripts/apk-audit.js <apk>` checks the manifest and the built file (including classes.dex for dynamic class loading); `npm test` in `bridge/` includes it.
- `node scripts/play-protect-appeal.js <apk>` writes the false-positive report text above, filled in from the real build.
- The Qt project (`android/qt/`, source only) declares the notification listener enabled; it is not built here and not part of the APK.
