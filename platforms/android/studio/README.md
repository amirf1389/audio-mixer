# Audio Mixer: Android Studio project

Open this folder (`platforms/android/studio`) in Android Studio, then press Run.

## What it uses

- **Java sources:** `platforms/android/src` (shared with `scripts/build-apk.js`, not copied).
- **Resources:** `platforms/android/res`, plus generated launcher icons.
- **Manifest:** a copy of `platforms/android/AndroidManifest.xml` without the `package` attribute (Android Gradle Plugin 8 takes it from `namespace`).
- **Page assets:** the mixer page and its fonts, generated into `app/src/generated/assets/www`.

The generated files come from `node scripts/prepare-android-studio.js`, which runs before every build. It needs Node.js 18+ on the PATH, and internet once to fill the npm cache in `dist/cache/android`.

## Requirements

- Android Studio (Koala or newer) with Android SDK Platform 34.
- JDK 17 (bundled with Android Studio).
- Node.js 18+ on the PATH.

## Command line

```
cd platforms/android/studio
./gradlew assembleDebug      # Windows: gradlew.bat assembleDebug
```

The debug APK is written to `app/build/outputs/apk/debug/`. Release builds still go through `npm run build:apk`, which signs with the project key.
