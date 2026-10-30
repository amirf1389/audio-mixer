# Audio Mixer: CMake project (Linux)

Open this folder (`platforms/linux/cmake`) in VS Code with the CMake Tools extension, in CLion, or in Qt Creator. Or build from the repository root:

```
cmake -S platforms/linux/cmake -B build
cmake --build build
sudo cmake --install build      # installs the ALSA plugin
```

## What it builds

| Target | Sources | Output |
|---|---|---|
| `audiomixer-native` | `engine/cpp` | the native engine (ALSA backend when the headers are installed) |
| `asound_module_pcm_audiomixer` | `platforms/linux/alsa-plugin`, `drivers/common/am_link.c` | `libasound_module_pcm_audiomixer.so` |

## Requirements

`bash scripts/setup-build-tools.sh` installs everything, or by hand: `cmake`, a C/C++ compiler, and `libasound2-dev`.

## Not included

The kernel module (`platforms/linux/kernel-module`) has to be built against the running kernel with DKMS. See its `README.txt`.
