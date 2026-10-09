@echo off
rem Audio Mixer PC mode (Windows): starts the local system server and opens the mixer. Needs Node.js 18+.
where node >nul 2>nul
if errorlevel 1 (
  echo Node.js is required. Download it from https://nodejs.org/ and run this file again.
  pause
  exit /b 1
)
cd /d "%~dp0"
node client\cli.js %*
pause
