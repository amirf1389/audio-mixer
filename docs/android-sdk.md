# AmF X1 SDK for Android (AAR)

`releases/android/amfx1-sdk-<version>.aar`: the AmF X1 engine for other Android apps (minSdk 26, arm64-v8a and x86_64). It holds the
engine's C SDK over JNI, the native AAudio loop with device selection, the audio HAL report and the brand profile. The AmF X1 app
uses the same module (`android/amix-sdk`).

## Use it
```kotlin
// app/build.gradle.kts
dependencies { implementation(files("libs/amfx1-sdk-1.24.0.0.aar")); implementation("androidx.core:core-ktx:1.13.1") }
```
```kotlin
val hal = AudioHal.read(context)            // public APIs + a short AAudio probe (opens and closes a stream, plays nothing)
hal.lines().forEach(::println)              // device, SoC, native rate / buffer, low-latency, pro audio, MMAP, burst ...
val profile = BrandProfile.of(hal)          // maker (Samsung, Xiaomi/Redmi/POCO, Pixel, OnePlus, Huawei, Honor, OPPO, realme, vivo ...)
profile.batteryTip                          // where this maker's battery manager is turned off for your app
startActivity(BrandProfile.batterySettings(context))

val engine = Engine()                        // the AmF X1 mixer engine (C SDK over JNI)
val loop = AudioLoop(engine, hal.nativeRate, hal.nativeFrames, outDevice = 0, inDevice = 0, bursts = profile.bursts)
loop.start()                                 // native AAudio first (reopens on unplug), Java AudioRecord/AudioTrack as fallback
println(loop.path)                           // "aaudio" or "java"
loop.stop()
```
Device ids are `AudioDeviceInfo.id` values from `AudioManager.getDevices(...)`; 0 is the system route.

## Multi-channel USB interfaces
Class-compliant USB audio interfaces work without drivers. `UsbAudio` lists them (`AudioManager.getDevices`, `TYPE_USB_DEVICE` and
`TYPE_USB_HEADSET`), one entry per direction, with what Android reports: `channelCounts`, `channelIndexMasks`, `sampleRates` (empty =
any) and `maxChannels` (the highest count or the widest index mask, 0 = not told). `Engine.startUsb` opens the native AAudio loop on a
chosen device id with N channels (`AAudioStreamBuilder_setDeviceId` / `setChannelCount`):
```kotlin
val input = UsbAudio.inputs(context).firstOrNull() ?: return
val output = UsbAudio.outputs(context).firstOrNull()
val rate = if (input.supportsRate(48000)) 48000 else input.sampleRates.first()
engine.startUsb(rate, outDevice = output?.id ?: 0, inDevice = input.id, inChannels = input.channelsFor(0), outChannels = 2)
engine.nativeInChannels()                     // what opened: a device that refuses the count falls back to stereo, then mono
```
Device input k plays on strip k (`UsbAudio.stripMap(channels, strips)` gives the same mapping; channels past the mixer's strips are not
used). On the output the master is on channels 1 / 2 and the others are silent. Tested with Robolectric shadows (the list, the
filtering and the channel arithmetic); multi-channel streams on a real phone with a real interface are not covered by the tests.

## Brand tuning
- **Buffer:** taken from what the HAL reported, not from the brand name: exclusive (MMAP) or shared low-latency output -> 2 bursts,
  a burst longer than 10 ms -> 3, no low-latency path or no AAudio -> 4.
- **Background audio:** each maker's own battery manager is the most common reason audio stops; the profile names the screen to open.
- **Bluetooth:** 100–300 ms of delay on every phone; monitor live with wired headphones or a USB interface.

## Security
- The library declares **no permission**. Your app adds `RECORD_AUDIO` itself and asks for it before opening the microphone.
- It reads only public Android APIs (`AudioManager`, `PackageManager`, `Build`, AAudio). No hidden or reflection APIs, no root, no
  network access, nothing sent off the phone, no code loaded at run time.
- Check the file before using it: `sha256sum -c SHA256SUMS.txt` in `releases/android`.
