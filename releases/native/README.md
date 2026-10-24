# audiomixer-native for Windows 1.18.0.0

Console programs: the Audio Mixer engine (`engine/cpp/app/main.cpp`) on WASAPI and ASIO, with no browser or Node.js. `--exclusive` takes exclusive control of the WASAPI device (the Windows mixer and other programs are bypassed; the error says if the device is busy or exclusive mode is off in the sound settings). Run `audiomixer-native-windows-x64.exe --list` first: it shows the backends built in and the installed ASIO drivers.

| File | SHA-256 |
|---|---|
| `AudioMixer-1.18.0.0-native-windows-x64.exe` | `968b0fc14b8d0e1272f744a383b30cc1f87930e32706eba744a810571c73a5db` |
| `AudioMixer-1.18.0.0-native-windows-x86.exe` | `22e519af772dea21b9ad2eae845766b8c8155b6c3f10377e3c3294acd8839e5a` |

Built with `node scripts/build-native.js` (mingw-w64, static). Compiled with `-Werror`; not run on Windows. Not code-signed. These files are not listed in `releases/update.json`.
