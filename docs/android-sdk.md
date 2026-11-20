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
loop.start()                                 // AUTO: native AAudio (reopens on unplug), else OpenSL ES, else Java AudioRecord/AudioTrack
println(loop.path)                           // "aaudio", "opensl" or "java"
loop.stop()
```
Device ids are `AudioDeviceInfo.id` values from `AudioManager.getDevices(...)`; 0 is the system route.

## Audio paths

The mixer can run on three audio APIs. All three feed the same engine. The two native paths also run the same mixing step
(`engine/cpp/app/sdk/amix_mixstep.hpp`: input k to strip k, the four decks, the multitrack copy, `amix_mixer_process`, the master
recorder, interleaving). So decks, inputs, multitrack and meters behave the same on both native paths.

| Path | `loop.path` | What it is |
|---|---|---|
| AAudio | `"aaudio"` | Native, Android 8+ (API 26). A low-latency output stream whose data callback mixes. The input is read without blocking. It supports device selection, multi-channel USB, and reopening after a disconnect. |
| OpenSL ES | `"opensl"` | Native. One engine, one output mix and an Android simple buffer queue player: float PCM through `SL_ANDROID_DATAFORMAT_PCM_EX`, or 16-bit if the device refuses float. It also has an optional 16-bit mono or stereo recorder with the UNPROCESSED preset (VOICE_RECOGNITION if that is refused). Two buffers of the phone's native size (AudioManager's `PROPERTY_OUTPUT_FRAMES_PER_BUFFER` at `PROPERTY_OUTPUT_SAMPLE_RATE`) take turns. The buffer-queue callback mixes into the one that finished and enqueues it again. Nothing is allocated or locked in the callbacks. It plays to the system route only: no device selection, at most 2 inputs. |
| Java | `"java"` | `AudioRecord` / `AudioTrack` on a worker thread calling `Engine.process`. This is the last resort. |

`AudioLoop(..., choice = AudioPathChoice.AUTO, nativeFrames = framesPerBuffer)` picks the path with `AudioPaths.choose(api, choice,
aaudioFailed, openSlFailed)`:

- **AUTO**: AAudio on API 26 and above, then OpenSL ES if AAudio does not open, then Java. Below API 26: OpenSL ES, then Java.
- **AAUDIO**: AAudio, then Java. Below API 26 it goes straight to Java.
- **OPENSL**: OpenSL ES, then Java.
- **JAVA**: Java only.

The SDK's minSdk is 26, so AUTO picks AAudio unless AAudio fails to open. The rule for older phones is there for builds with a lower
minSdk. OpenSL ES directly: `engine.startOpenSl(rate, framesPerBuffer, mic, inChannels)` returns the rate in use (0 = not available).
`engine.openSlState()` returns `[running, floatOutput, sampleRate, framesPerBuffer, inputOpen, inChannels, blocksMixed,
inputShortBlocks]`, and `Engine.slesStop()` stops it. In the app, choose the path under SETUP > AUDIO PATH in the phone mixer. The
Devices screen's LOW LATENCY report shows the path in use ("Mixer path: ...").

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
