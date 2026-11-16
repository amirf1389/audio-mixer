@echo off
rem AmF X1 MANAGER (vendor only): licence keys, customers, releases (OTA), key requests and activity on one sign-in.
rem It starts management\vendor\server.js on THIS computer (127.0.0.1:8790) and opens it in the browser. Never part of the customer download.
rem Set LICENSE_SERVER and OTA_SERVER below (or in the environment) to your servers; an SSH tunnel to 127.0.0.1 is the safe way.
setlocal
cd /d "%~dp0.."
where node >/dev/null 2>/dev/null || (echo Node.js 18 or newer is needed: https://nodejs.org/ & pause & exit /b 1)
if "%LICENSE_SERVER%"=="" set "LICENSE_SERVER=http://127.0.0.1:8787"
if "%OTA_SERVER%"=="" set "OTA_SERVER=http://127.0.0.1:8780"
if "%MANAGER_PORT%"=="" set "MANAGER_PORT=8790"
start "AmF X1 Manager" /min node vendor\server.js
timeout /t 2 /nobreak >nul
start "" "http://127.0.0.1:%MANAGER_PORT%/"
endlocal
