# Audio Mixer: Xcode project (iOS)

The iOS app already has an Xcode project generator: `scripts/build-ios.js` writes an XcodeGen spec from `platforms/ios/project.yml.template` and stages the mixer page.

```
node scripts/build-ios.js            # writes the Xcode project archive into platforms/ios/releases
```

Then on a Mac:

```
brew install xcodegen
tar xzf platforms/ios/releases/AudioMixer-<version>-ios-xcode-project.tar.gz
cd <the unpacked folder> && xcodegen generate && open AudioMixer.xcodeproj
```

Sources: `platforms/ios/AudioMixer` (Swift). Set your Apple developer team in Xcode (Signing & Capabilities) before running on a device. `node scripts/build-ios.js --ipa` on a Mac also makes an unsigned `.ipa`.
