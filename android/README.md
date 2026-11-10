# A mix OS for Android (Kotlin + the engine's C SDK)

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
