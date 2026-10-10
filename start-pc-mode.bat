@echo off
rem Audio Mixer PC mode (Windows): starts the local system server and opens the mixer.
rem If Node.js 18+ is missing it is installed first, after asking: with winget (official Node.js LTS) when available, otherwise the official zip
rem from nodejs.org (SHA-256 checked, into %LOCALAPPDATA%\AudioMixer\node, no administrator rights). Set AUDIO_MIXER_YES=1 to skip the questions.
rem The Microsoft Visual C++ runtime that the native audio module (Audify) needs is installed with winget when it is missing.
setlocal EnableDelayedExpansion
cd /d "%~dp0"
set "AM_NODE_DIR=%LOCALAPPDATA%\AudioMixer\node"
call :find_node
if not defined NODE_OK (
  call :install_node
  call :find_node
)
if not defined NODE_OK (
  echo Node.js 18 or newer is required: https://nodejs.org/
  pause
  exit /b 1
)
call :vcruntime
node client\cli.js %*
pause
exit /b 0

:find_node
set "NODE_OK="
where node >nul 2>nul && for /f "delims=" %%v in ('node -p "process.versions.node.split('.')[0]" 2^>nul') do if %%v GEQ 18 set "NODE_OK=1"
if defined NODE_OK exit /b 0
if exist "%AM_NODE_DIR%\node.exe" (
  set "PATH=%AM_NODE_DIR%;%PATH%"
  for /f "delims=" %%v in ('node -p "process.versions.node.split('.')[0]" 2^>nul') do if %%v GEQ 18 set "NODE_OK=1"
)
exit /b 0

:ask
rem %~1 = question; returns 0 for yes
if "%AUDIO_MIXER_YES%"=="1" exit /b 0
choice /c YN /d Y /t 60 /m "%~1"
if errorlevel 2 exit /b 1
exit /b 0

:install_node
call :ask "Node.js 18 or newer was not found. Install the official Node.js LTS now?"
if errorlevel 1 exit /b 1
where winget >nul 2>nul
if not errorlevel 1 (
  echo Installing Node.js LTS with winget ...
  winget install --id OpenJS.NodeJS.LTS -e --silent --accept-package-agreements --accept-source-agreements
  if exist "%ProgramFiles%\nodejs\node.exe" set "PATH=%ProgramFiles%\nodejs;%PATH%"
  where node >nul 2>nul && exit /b 0
)
echo Downloading the official Node.js zip from nodejs.org ...
set "ARCH=x64"
if /i "%PROCESSOR_ARCHITECTURE%"=="x86" if not defined PROCESSOR_ARCHITEW6432 set "ARCH=x86"
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" set "ARCH=arm64"
set "BASE=https://nodejs.org/dist/latest-v22.x"
curl.exe -fsSL "%BASE%/SHASUMS256.txt" -o "%TEMP%\am-shasums.txt" || (echo Cannot reach nodejs.org. & exit /b 1)
set "WANT=" & set "FILE="
for /f "tokens=1,2" %%a in ('findstr /r /c:"node-v[0-9.]*-win-%ARCH%\.zip$" "%TEMP%\am-shasums.txt"') do (set "WANT=%%a" & set "FILE=%%b")
if not defined FILE (echo No Node.js build for win-%ARCH% was found. & exit /b 1)
curl.exe -fsSL "%BASE%/%FILE%" -o "%TEMP%\%FILE%" || (echo Download failed. & exit /b 1)
set "GOT="
for /f "skip=1 tokens=*" %%h in ('certutil -hashfile "%TEMP%\%FILE%" SHA256') do if not defined GOT set "GOT=%%h"
set "GOT=%GOT: =%"
if /i not "%GOT%"=="%WANT%" (
  echo The download does not match nodejs.org's checksum. Nothing was installed.
  del "%TEMP%\%FILE%" 2>nul
  exit /b 1
)
if exist "%AM_NODE_DIR%" rmdir /s /q "%AM_NODE_DIR%"
mkdir "%LOCALAPPDATA%\AudioMixer" 2>nul
tar.exe -xf "%TEMP%\%FILE%" -C "%LOCALAPPDATA%\AudioMixer" || (echo Could not unpack Node.js. & exit /b 1)
set "FOLDER=%FILE:.zip=%"
ren "%LOCALAPPDATA%\AudioMixer\%FOLDER%" node
del "%TEMP%\%FILE%" "%TEMP%\am-shasums.txt" 2>nul
echo Node.js installed in %AM_NODE_DIR% (checksum verified).
exit /b 0

:vcruntime
if not exist "bridge\node_modules\audify" exit /b 0
set "VCDLL=%SystemRoot%\System32\vcruntime140_1.dll"
set "VCID=Microsoft.VCRedist.2015+.x64"
if /i "%PROCESSOR_ARCHITECTURE%"=="x86" if not defined PROCESSOR_ARCHITEW6432 (set "VCDLL=%SystemRoot%\System32\vcruntime140.dll" & set "VCID=Microsoft.VCRedist.2015+.x86")
if exist "%VCDLL%" exit /b 0
where winget >nul 2>nul || (echo The native audio module needs the Microsoft Visual C++ runtime: https://aka.ms/vs/17/release/vc_redist.x64.exe & exit /b 0)
call :ask "The native audio module (ASIO) needs the Microsoft Visual C++ runtime. Install it with winget now?"
if errorlevel 1 exit /b 0
winget install --id %VCID% -e --silent --accept-package-agreements --accept-source-agreements
exit /b 0
