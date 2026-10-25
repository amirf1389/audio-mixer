# audiomixer-native for Windows 1.19.0.0

Console programs: the Audio Mixer engine (`engine/cpp/app/main.cpp`) on WASAPI and ASIO, with no browser or Node.js. `--exclusive` takes exclusive control of the WASAPI device (the Windows mixer and other programs are bypassed; the error says if the device is busy or exclusive mode is off in the sound settings). One file sets a machine up (run from an administrator prompt): `--install-driver` writes the ASIO driver DLL that is embedded in the program to `%ProgramFiles%\Audio Mixer ASIO\` and registers it ("Audio Mixer" then appears in every ASIO program), `--uninstall-driver` removes it, and `--install-flexasio <FlexASIO-x.y.exe> --sha256 <hex>` runs a FlexASIO installer silently only when its SHA-256 matches. Run `audiomixer-native-windows-x64.exe --list` first: it shows the backends built in and the installed ASIO drivers.

| File | SHA-256 |
|---|---|
| `AudioMixer-1.19.0.0-native-windows-x64.exe` | `75198a304b5910678d472b9605da0e42ec29730d19bc4c3ebbdfbe5218d1e0c5` |
| `AudioMixer-1.19.0.0-native-windows-x86.exe` | `270fdc1e5e6e99d7c784e96da9a0616b3f433fade60af436c1fbc68f8c455131` |

Built with `node scripts/build-native.js` (mingw-w64, static). Compiled with `-Werror`; not run on Windows. Not code-signed. The same files (also used to self-update `audiomixer-native.exe --update`) are listed in `releases/update.json` under `win-x64-native` / `win-x86-native`.
