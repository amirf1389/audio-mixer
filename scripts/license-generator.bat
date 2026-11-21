@echo off
setlocal
rem AmF X1 license key generator: double-click to open it in your browser (this computer only).
rem AUTO NODE.JS: if Node.js 18+ is missing, it offers to install Node.js LTS with winget (Windows' own package manager), then starts
rem the generator through npm (npm run license:generator). Nothing is installed without asking; the private key never leaves this computer.
cd /d "%~dp0.."
call :checknode
if errorlevel 1 (
  echo Node.js 18 or newer is needed to run the key generator.
  where winget >nul 2>nul
  if errorlevel 1 (
    echo winget is not available on this Windows. Install Node.js LTS from https://nodejs.org/ and run this again.
    start "" https://nodejs.org/
    pause & exit /b 1
  )
  choice /m "Install Node.js LTS now with winget"
  if errorlevel 2 (echo Not installed. & pause & exit /b 1)
  winget install --id OpenJS.NodeJS.LTS -e --accept-source-agreements --accept-package-agreements
  set "PATH=%ProgramFiles%\nodejs;%PATH%"
  call :checknode
  if errorlevel 1 (echo Node.js was installed but is not found yet: close this window and double-click again. & pause & exit /b 1)
)
where npm >nul 2>nul && (call npm run --silent license:generator -- %*) || (node scripts\license-generator.js --open %*)
pause
exit /b 0
:checknode
where node >nul 2>nul || exit /b 1
for /f "delims=" %%v in ('node -p "process.versions.node.split('.')[0]"') do set NODEMAJOR=%%v
if %NODEMAJOR% LSS 18 (echo Node.js %NODEMAJOR% is too old. & exit /b 1)
exit /b 0
