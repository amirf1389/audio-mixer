# audiomixer-native for Windows 1.18.0.0

Console programs: the Audio Mixer engine (`engine/cpp/app/main.cpp`) on WASAPI and ASIO, with no browser or Node.js. `--exclusive` takes exclusive control of the WASAPI device (the Windows mixer and other programs are bypassed; the error says if the device is busy or exclusive mode is off in the sound settings). One file sets a machine up (run from an administrator prompt): `--install-driver` writes the ASIO driver DLL that is embedded in the program to `%ProgramFiles%\Audio Mixer ASIO\` and registers it ("Audio Mixer" then appears in every ASIO program), `--uninstall-driver` removes it, and `--install-flexasio <FlexASIO-x.y.exe> --sha256 <hex>` runs a FlexASIO installer silently only when its SHA-256 matches. Run `audiomixer-native-windows-x64.exe --list` first: it shows the backends built in and the installed ASIO drivers.

| File | SHA-256 |
|---|---|
| `AudioMixer-1.18.0.0-native-windows-x64.exe` | `6466cb808584abf98c9e39c65329dad45cb0cb866a3f68b7022a9a3b0a0e55c4` |
| `AudioMixer-1.18.0.0-native-windows-x86.exe` | `a3913f0fda01a6cc53ff991772ac281e93fbf09d9ef681d573681f437c616f2a` |

Built with `node scripts/build-native.js` (mingw-w64, static). Compiled with `-Werror`; not run on Windows. Not code-signed. These files are not listed in `releases/update.json`.
