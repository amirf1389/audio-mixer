# 10. Troubleshooting

**Where the log is.** The console writes every interface message to `console.log` in the data folder (`%APPDATA%\AMFB Audio\AmF X1` on Windows). Send it with any report.

**No sound / faders show nothing.** The engine is not running or has no input: the amber alert bar says so. Open CONNECTIONS, pick the device, START. On SOUND CARD read LAST START and the DRIVER LADDER. `"AmF X1.exe" --list-devices` lists what the drivers expose.

**ASIO driver not found.** It must be registered under HKLM\SOFTWARE\ASIO (64-bit) or WOW6432Node\ASIO (32-bit; needs the 32-bit program). A failed load prints the Windows error text with the HRESULT.

**Interface grey or wrong colours.** Switch the theme with ☾ / ☀ once. If still wrong, start `amixos.exe --software` (CPU renderer) or `--opengl` and send console.log.

**Windows Defender / SmartScreen.** Release files are signed by AMFB Audio; a build that is not signed shows "unknown publisher". The program is unpacked, embeds nothing, asks for no elevation and installs no service; the driver DLL ships beside it and is registered only on request.

**Bluetooth sounds late.** That is the codec (100-250 ms); use a wired output for live playing.

**UM2 / Behringer checks.** With the interface plugged in: `"AmF X1.exe" --list-devices` should show BEHRINGER USB AUDIO (ASIO) and its WASAPI / DirectSound endpoints; `--backend asio --out "BEHRINGER" --seconds 5` should run five seconds without overruns; in the console the strip chips read "BEHRINGER … · IN 1 / IN 2".

**Factory reset.** SETTINGS › FACTORY RESET clears the console and reruns the setup; your licence stays.
