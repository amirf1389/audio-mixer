# AmF X1 · Visual Studio starter (Windows)

Two starter projects, both in this folder:

| Project | What it is | Open with |
|---|---|---|
| `AmFX1.WinShell` | The Windows app: starts the engine and shows the mixer in a window | Visual Studio 2022, `AmFX1.WinShell.csproj` |
| `AmFX1.EngineStarter` | A small C++ program that uses the audio engine (`engine/cpp`) | Visual Studio 2022, **File → Open → Folder**, select `AmFX1.EngineStarter` |

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
5. **Open `AmFX1.WinShell\AmFX1.WinShell.csproj`** in Visual Studio. Let it restore the NuGet package `Microsoft.Web.WebView2`.
6. **Press F5.** A window opens with the mixer. If it says "did not answer", wait a few seconds: the engine is still starting.
7. **Optional: the C++ starter.** Open the `AmFX1.EngineStarter` folder in Visual Studio, pick **x64 Release**, and run. It prints the measurements of a 440 Hz test tone.

## Common problems
- **"Node.js is needed":** install Node.js and restart Visual Studio, so it sees the new PATH.
- **"WebView2 runtime is missing":** install it from microsoft.com/webview2.
- **"Could not find bridge\server.js":** the folder structure changed. Keep `platforms\windows\visual-studio` inside the repository.
- **The engine is still on an old version after an update:** close the app, then press F5 again.

## Editing
- The window title, address and repository search: `AmFX1.WinShell\AppSettings.cs`.
- The starting window size and the status messages: `AmFX1.WinShell\MainForm.cs`.
- The C++ example: `AmFX1.EngineStarter\main.cpp`.

## What is not checked yet
- The C# project was written without a .NET SDK in the build environment, so it may need small fixes on the first build. Send me the errors.
- The C++ starter was built and run with g++ and CMake on Linux, not with the Visual Studio compiler yet.
