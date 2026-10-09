@echo off
rem Audio Mixer local system server (Windows): runs only the server on http://localhost:8765 (no browser window).
rem Open the mixer yourself at that address, or use start-pc-mode.bat to start and open it. Needs Node.js 18+.
title Audio Mixer local server
where node >nul 2>nul
if errorlevel 1 (
  echo Node.js is required. Download it from https://nodejs.org/ and run this file again.
  pause
  exit /b 1
)
cd /d "%~dp0"
echo Starting the Audio Mixer local server on http://localhost:8765  (Ctrl+C to stop)
node bridge\server.js
echo.
echo The server stopped.
pause
