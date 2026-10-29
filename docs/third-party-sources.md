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
| RtAudio | https://www.music.mcgill.ca/~gary/rtaudio/ | Cross-platform audio I/O (the "RT" in the bridge's audio engine) | Reference only. Check the license before bundling. |

## Pending

- Driver sources: none listed yet.
- WASM codec targets: none listed yet.
