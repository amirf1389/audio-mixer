@echo off
rem Audio Mixer local system server (Windows): runs only the server on http://localhost:8765 (no browser window).
rem Open the mixer yourself at that address, or use start-pc-mode.bat to start and open it. Installs Node.js 18+ automatically when missing (see start-pc-mode.bat).
title Audio Mixer local server
set "AM_HERE=%~dp0"
where node >nul 2>nul || call "%AM_HERE%start-pc-mode.bat" --ensure-node
where node >nul 2>nul
if errorlevel 1 (
  echo Node.js 18 or newer is required and could not be installed: https://nodejs.org/
  pause
  exit /b 1
)
cd /d "%~dp0"
if not exist bridge\server.js if exist ..\..\bridge\server.js cd /d ..\..
echo Starting the Audio Mixer local server on http://localhost:8765  (Ctrl+C to stop)
node bridge\server.js
echo.
echo The server stopped.
pause
