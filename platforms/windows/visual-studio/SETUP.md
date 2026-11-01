# AmF X1 · Visual Studio solution (Windows)

One solution, `AmFX1.sln`, with three projects:

| Project | What it is | Build output |
|---|---|---|
| `AmFX1.WinShell` | The Windows app: starts the engine and shows the mixer in a window. **Startup project.** | `AmFX1.exe` |
| `AmFX1.EngineStarter` | A small C++ program that uses the audio engine (`engine/cpp`) | `engine_check` (x64) |
| `AmFX1.AsioDriver` | The ASIO driver for DAWs and OBS (Steinberg SDK under GPLv3) | `AudioMixerASIO64.dll` (x64) |

## Fast setup (about 10 minutes, mostly downloads)
1. Open **PowerShell** in this folder.
2. Run: `powershell -ExecutionPolicy Bypass -File .\setup.ps1`
3. The script installs anything missing, restores the packages, and opens the app project.
4. In Visual Studio, press **F5**. The app starts the engine and shows the mixer.

## Step by step (if the script fails)
1. **Install Git, Node.js (LTS) and the .NET 8 SDK**, from git-scm.com, nodejs.org and dotnet.microsoft.com.
2. **Install Visual Studio 2022 Community.** In the installer, tick **Desktop development with C++** and **.NET desktop development**.
3. **Install the WebView2 runtime** from microsoft.com/webview2 (most Windows 10/11 PCs already have it).
4. **Check the repository.** `bridge\server.js` must exist two folders above this one: `audio-mixer\bridge\server.js`.
5. **Open `AmFX1.sln`** in Visual Studio. Right-click **AmFX1.WinShell → Set as Startup Project**. Let it restore the NuGet package `Microsoft.Web.WebView2`.
6. **Press F5.** A window opens with the mixer. If it says "did not answer", wait a few seconds: the engine is still starting.
7. **Optional: the other two projects.** Build **AmFX1.EngineStarter** (x64) and run it: it prints the measurements of a 440 Hz test tone. Build **AmFX1.AsioDriver** (x64) to get `AudioMixerASIO64.dll`.
8. **Optional: the ASIO driver.** Register it from an administrator PowerShell: `powershell -ExecutionPolicy Bypass -File ..\..\..\scripts\register-asio-driver.ps1 -InstallDir <folder with the DLL>`. The DLL is in `AmFX1.AsioDriver\x64\<Configuration>\`.

## Common problems
- **"Node.js is needed":** install Node.js and restart Visual Studio, so it sees the new PATH.
- **"WebView2 runtime is missing":** install it from microsoft.com/webview2.
- **"Could not find bridge\server.js":** the folder structure changed. Keep `platforms\windows\visual-studio` inside the repository.
- **The engine is still on an old version after an update:** close the app, then press F5 again.

## Editing
- The window title, address and repository search: `AmFX1.WinShell\AppSettings.cs`.
- The starting window size and the status messages: `AmFX1.WinShell\MainForm.cs`.
- The C++ example: `AmFX1.EngineStarter\main.cpp`.
- The ASIO driver project: `AmFX1.AsioDriver\AmFX1.AsioDriver.vcxproj`.

## What is not checked yet
- The C# project and the two C++ projects (`.vcxproj`) were written without Visual Studio or a .NET SDK here, so they may need small fixes on the first build. Send me the errors.
- The C++ starter was built and run with g++ and CMake on Linux; the ASIO driver was built with mingw-w64 only. Neither has been built by Visual Studio yet.
- The PowerShell setup script was not run here (no PowerShell).
