# audiomixer-native for Windows 1.18.0.0

Console programs: the Audio Mixer engine (`engine/cpp/app/main.cpp`) on WASAPI and ASIO, with no browser or Node.js. Run `audiomixer-native-windows-x64.exe --list` first: it shows the backends built in and the installed ASIO drivers.

| File | SHA-256 |
|---|---|
| `AudioMixer-1.18.0.0-native-windows-x64.exe` | `0b741334eccd18cbe23740f56150e22fe1f0579fe59a253848eb221ba424b56d` |
| `AudioMixer-1.18.0.0-native-windows-x86.exe` | `e1455130d34ee085068ccf95a66b33f6d9b7407cd083ab4a077afe93864cb409` |

Built with `node scripts/build-native.js` (mingw-w64, static). Compiled with `-Werror`; not run on Windows. Not code-signed. These files are not listed in `releases/update.json`.
