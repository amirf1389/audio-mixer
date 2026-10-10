# Audio Mixer - Qt edition (Android)

A Qt 6 / Qt Quick project that wraps the mixer page the same way the Java app in `../android/` does:
a small HTTP server on `127.0.0.1` (ports 8765-8774) serves the page and the two JSON routes, and a `WebView` shows it.
Audio devices come from **Qt Multimedia** (`QMediaDevices`) instead of `AudioManager`.

> **Status: source only, not built or run in this repository's tests.** Building it needs the Qt 6.5+ SDK for Android and the
> Android NDK, which are not part of this repository's tooling. The Java app (`../android/`, `npm run build:apk`) is the tested,
> shipping Android app. Treat this project as a starting point for people who work with Qt.

## What it does

| Part | File | |
|---|---|---|
| Page server | `src/MixerServer.*` | static files from `assets:/www`, `GET /api/status`, `GET /api/interfaces` (same JSON shape as the PC bridge and the Java app, `"engine":"qt"`) |
| Devices | `src/AudioDevices.*` | inputs and outputs with channel counts and sample-rate range, plus a `native` block (rate, channel limits) |
| Shell | `qml/Main.qml`, `src/main.cpp` | `WebView` on the page; asks for the microphone permission (`QMicrophonePermission`) |
| Android | `android/AndroidManifest.xml` | `RECORD_AUDIO`, `MODIFY_AUDIO_SETTINGS`, low-latency / USB host features (not required) |

## What it does not do (yet)

- **No PCM streaming routes** (`/ws/input`, `/ws/output`): the page's LIVE SOURCES panel therefore lists the devices but cannot patch
  native multichannel audio through this server. The Java app implements those routes with `AudioRecord` / `AudioTrack`.
  A Qt version would add a `QWebSocketServer` plus `QAudioSource` / `QAudioSink` workers.
- **No background engine / Quick Settings tile**: the Java app has an `EngineService` foreground service and an `EngineTileService`.
- **Microphone inside the WebView** (`getUserMedia`) depends on what Qt WebView grants on the device; this has not been checked.

## Build

1. Install Qt 6.5 or newer with the *Android* kit for your phone's ABI, the Android SDK and NDK (Qt Online Installer + Qt Creator set this up).
2. Stage the page (offline copy of the page, needs `npm` once to fetch Tailwind and Font Awesome):

       node scripts/prepare-qt.js

3. Open `android-qt/CMakeLists.txt` in Qt Creator, choose the Android kit and run, or from a shell:

       cmake -S android-qt -B android-qt/build -DCMAKE_TOOLCHAIN_FILE=<Qt>/android_arm64_v8a/lib/cmake/Qt6/qt.toolchain.cmake -DANDROID_SDK_ROOT=<sdk> -DANDROID_NDK_ROOT=<ndk>
       cmake --build android-qt/build --target apk

The APK is unsigned-debug by default; sign it with your own key for distribution (the Java app's `npm run build:apk` shows how its key is handled).
