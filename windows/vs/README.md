# A mix OS for Windows: Visual Studio solution

`AmixOS.sln` (Visual Studio 2022, v143, C++20) builds, for x64:

| project | kind | what |
|---|---|---|
| `amix` | DLL | the C SDK (`sdk/c/include/amix.h`): engine, WASAPI + ASIO runtime, devices, licence check. Exports only `amix_*`. |
| `amixos-daemon` | EXE | the engine as a local HTTP / WebSocket service (binds 127.0.0.1) |
| `AmixLauncher` | VB.NET WinForms (.NET 8) | starts / stops the daemon, shows its status, opens the console and the web remote |

Build: open the solution, pick **Release | x64**, Build Solution. Output in `windows\vs\out\Release\`: `amix.dll`, `amix.lib`,
`amixos-daemon.exe`, `AmixLauncher.exe`. Put `amixos.exe` (the Qt console, built with Qt 6 for MSVC from `native/amixos`) in the same
folder and the launcher's OPEN CONSOLE button finds it.

The ASIO sources (`engine/cpp/src/backend_asio.cpp`, `drivers/asio`) need the Steinberg ASIO SDK headers, which are in the repository
under Steinberg's licence; the WASAPI path has no such dependency. The same sources build with MinGW (`scripts/build-native.js`) and
on GitHub Actions (`.github/workflows`); the solution is for people who work in Visual Studio.

The launcher is a thin front end: it never opens the daemon to the LAN (`--allow-lan` serves unencrypted HTTP and is for your own TLS
proxy only), stores no secrets and reads the status from `http://127.0.0.1:8765/api/status`.
