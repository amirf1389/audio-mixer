# Third-party sources

Projects the Audio Mixer relies on or is evaluating. Listing a project here does not add it to the build: each one
is installed, bundled, or linked separately, and its license has to be checked before that happens.

| Project | Link | Use | Status |
|---|---|---|---|
| FFmpeg | https://github.com/FFmpeg/FFmpeg | Codecs and container formats | Reference only. License depends on build options (LGPL or GPL); bundling needs a license review. |
| node-audio-asio (distopik) | https://github.com/distopik/node-audio-asio | Node.js ASIO binding | Reference only. Needs the Steinberg ASIO SDK to build. |

## Pending

- Driver sources: none listed yet.
- WASM codec targets: none listed yet.
