# A mix OS for Android (Kotlin + the engine's C SDK)

Two things in one app: a **local mixer** (the engine on the phone, microphone in) and the **remote control** of a console on the LAN
(`RemoteActivity`: connect to `amixos-daemon` with host, port and the token from `~/.amixos/daemon.token`; 32 strips with fader, pan,
mute, solo and meters from the WebSocket stream; master; devices open / stop; a channel page with EQ, compressor and gate). The
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
