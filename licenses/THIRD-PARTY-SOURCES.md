# Third-party sources

Projects the Audio Mixer relies on or is evaluating. Listing a project here does not add it to the build: each one
is installed, bundled, or linked separately, and its license has to be checked before that happens.

| Project | Link | Use | Status |
|---|---|---|---|
| FFmpeg | https://github.com/FFmpeg/FFmpeg | Codecs and container formats | Reference only. License depends on build options (LGPL or GPL); bundling needs a license review. |
| node-audio-asio (distopik) | https://github.com/distopik/node-audio-asio | Node.js ASIO binding | Reference only. Needs the Steinberg ASIO SDK to build. |
| Node.js | https://nodejs.org | JavaScript runtime for the bridge and build scripts | Reference only. In use. |
| JavaScript (ECMAScript) | https://developer.mozilla.org/docs/Web/JavaScript | Language of the mixer page and bridge | Reference only. |
| C | https://en.cppreference.com/w/c | Language of the Windows driver stub and native code | Reference only. |
| C++ | https://en.cppreference.com/w/cpp | Language of the ASIO driver and native engine | Reference only. |
| Rust | https://www.rust-lang.org | Systems language (candidate for native audio code) | Reference only. Not used yet. |
| QR Code generator (Project Nayuki) | https://github.com/nayuki/QR-Code-generator | The pairing QR code on NETWORK → PHONE REMOTE | **Bundled** (v1.8.0, MIT): `engine/cpp/app/qr/`, licence in `engine/cpp/app/qr/LICENSE.txt`. |
| r8brain-free-src (Aleksey Vaneev) | https://github.com/avaneev/r8brain-free-src | Hi-res sample-rate conversion (64-bit, up to 768 kHz and beyond): media player, SDK `amix_hires.h` | **Bundled** (version 6.5, MIT): `engine/cpp/app/hires/r8brain/`, licence in `LICENSE` there. Built without PFFFT (Ooura FFT only). |
| libsoxr (SoX Resampler) | https://github.com/chirlu/soxr | Alternative very-high-quality resampler for the SDK (`engine: "soxr"`) | **Bundled as its own library** (0.1.3, LGPL 2.1): `engine/cpp/app/hires/soxr/` builds `amix_soxr.dll` / `libamix_soxr.so`, loaded at run time and never linked into AmF X1, so a user can replace it with their own build. Licence `COPYING.LGPL` and `LICENCE` there; the complete source of the library as shipped is this folder. |
| libFLAC (Xiph.Org) | https://github.com/xiph/flac | FLAC writing (16 / 24 / 32-bit): FLAC 24-bit recording, SDK `amix_hr_write` | **Bundled** (1.5.0, BSD-3-Clause): `engine/cpp/app/hires/flac/`, licence `COPYING.Xiph` there. libFLAC only; no GPL tools. |
| dr_wav / dr_flac (David Reid) | https://github.com/mackron/dr_libs | Reading WAV, RF64, Wave64 (PCM 8..64-bit, float 32/64) and FLAC | **Bundled** (dr_wav 0.14.6, dr_flac 0.13.4; public domain or MIT-0, either): `engine/cpp/app/hires/dr/`, licence at the end of each header. |
| RtAudio | https://www.music.mcgill.ca/~gary/rtaudio/ | Cross-platform audio I/O (the "RT" in the bridge's audio engine) | Reference only. Check the license before bundling. |

## Pending

- Driver sources: none listed yet.
- WASM codec targets: none listed yet.
