# Building AmF X1 for macOS and iOS

Nothing here was built on a Mac yet: the Linux container compiles and tests only the pure Core Audio negotiation
(`engine/cpp/src/coreaudio_logic.cpp`, test `coreaudio_logic`). Expect a first round of fixes on a real Mac.
No signing keys, passwords or team ids live in the repository: everything comes from your keychain and environment.

## What is where
| Part | Files |
|---|---|
| Core Audio backend (AUHAL, device list, defaults, rate/buffer negotiation, aggregate-safe, device listeners) | `engine/cpp/src/backend_coreaudio.cpp` (Apple only), `engine/cpp/{include/audiomixer,src}/coreaudio_logic.*` (everywhere) |
| Console bundle | `native/amixos/CMakeLists.txt` (`APPLE AND NOT IOS`), `native/amixos/mac/Info.plist.in`, `native/amixos/mac/amixos.entitlements` |
| Capture protection | `native/amixos/src/CaptureGuardMac.mm` (`NSWindow.sharingType = none`) |
| macOS build / sign / notarize / dmg | `scripts/macos/build.sh` |
| iOS remote (SwiftUI) | `platforms/ios/` (XcodeGen `project.yml`), `scripts/ios/build.sh` |
| CI | `.github/workflows/apple.yml` (macos-14) |

Windows-only features (ASIO, DirectSound, WASAPI exclusive, APO info, Windows volume/mute, app capture, Windows tray specifics) are
behind `_WIN32` and are absent on macOS; the device page offers `coreaudio` and `null`. Different input and output devices need an
**aggregate device** (Audio MIDI Setup); an aggregate's clock is never changed by the console (the audio unit converts).

## 1. Tools (once)
1. macOS 13 or newer, Xcode 15+ (`xcode-select --install`, then open Xcode once to accept the licence).
2. `brew install cmake ninja xcodegen`
3. Qt 6.8 for macOS (Qt Online Installer: *Qt 6.8.x > macOS*). Note the kit path, e.g. `~/Qt/6.8.3/macos`.

## 2. macOS console
```sh
git clone <repo> && cd audio-mixer
QT_DIR=~/Qt/6.8.3/macos scripts/macos/build.sh
```
Without credentials this makes an **unsigned** (ad-hoc signed) universal `build/macos/AmF X1.app` and `dist/macos/AmF-X1-<version>-macos.dmg`
and says so. To open it on another Mac: right-click > Open (Gatekeeper).

### Signed and notarized
1. In your Apple Developer account create a **Developer ID Application** certificate; install it in the login keychain.
   `security find-identity -v -p codesigning` shows its name.
2. Store notary credentials once (app-specific password from appleid.apple.com):
   `xcrun notarytool store-credentials amfx1 --apple-id you@example.com --team-id TEAMID`
3. Build:
   ```sh
   export AMIX_SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)"
   export AMIX_NOTARY_PROFILE=amfx1
   QT_DIR=~/Qt/6.8.3/macos scripts/macos/build.sh
   ```
   The app is signed inside-out with the hardened runtime and `com.apple.security.device.audio-input`, the dmg is notarized and stapled.
4. Check: `spctl -a -vv "build/macos/AmF X1.app"` and `xcrun stapler validate dist/macos/*.dmg`.

First run: macOS asks for microphone access (NSMicrophoneUsageDescription); allow it, or the input is silent
(System Settings > Privacy & Security > Microphone).

## 3. iOS remote
```sh
scripts/ios/build.sh                          # simulator build, unsigned
open platforms/ios/AmFX1Remote.xcodeproj      # run on the simulator / your iPhone from Xcode
AMIX_TEAM_ID=TEAMID scripts/ios/build.sh      # signed archive + .ipa (AMIX_EXPORT_METHOD=development|ad-hoc|app-store-connect)
```
Use: on the computer start the remote service with LAN access (console REMOTE page), then in the app scan the QR code
(`http://HOST:PORT/remote#pin=NNNNNN`) or type address + PIN. The token is kept in the iOS Keychain. The app shows master,
channel faders and mutes, and each channel's inserts (add / bypass / remove). iOS asks for camera (QR) and local network access.
ATS allows plain HTTP to local addresses only (`NSAllowsLocalNetworking`); the service itself accepts private addresses only.

## 4. GitHub Actions
`.github/workflows/apple.yml` builds the unsigned dmg and the iOS simulator app on every relevant push. Signing turns on only when
these repository secrets exist (add them yourself; none are committed): `MACOS_CERT_P12_BASE64`, `MACOS_CERT_PASSWORD`,
`MACOS_SIGN_IDENTITY`, and for notarization `APPLE_ID`, `APPLE_APP_PASSWORD`, `APPLE_TEAM_ID`; `APPLE_TEAM_ID_IOS` for the iOS archive.
