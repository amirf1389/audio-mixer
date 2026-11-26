@echo off
rem AmF X1 WEB STACK: nginx (web\nginx) in front of the service, plus the Node bridge (web\node, web\bridge).
rem Usage: start-web-stack.cmd [listen port, 8080] [service port, 8765] [bridge port, 8766]
rem Start "AmF X1 Network.exe" (or the console's NETWORK page) first. Stop with stop-web-stack.cmd. Listens on 127.0.0.1 only.
setlocal
set "WEB=%~dp0"
if "%WEB:~-1%"=="\" set "WEB=%WEB:~0,-1%"
set "LPORT=%~1" & if "%~1"=="" set "LPORT=8080"
set "DPORT=%~2" & if "%~2"=="" set "DPORT=8765"
set "BPORT=%~3" & if "%~3"=="" set "BPORT=8766"
set "PREFIX=%LOCALAPPDATA%\AMFB Audio\AmF X1\webstack"
if not exist "%PREFIX%\logs" mkdir "%PREFIX%\logs"
if not exist "%PREFIX%\temp" mkdir "%PREFIX%\temp"
set "W=%WEB:\=/%"
powershell -NoProfile -Command "(Get-Content -Raw '%WEB%\nginx\conf\nginx.conf.in').Replace('@WEB_DIR@','%W%').Replace('@NGINX_DIR@','%W%/nginx').Replace('@LISTEN_ADDR@','127.0.0.1').Replace('@LISTEN_PORT@','%LPORT%').Replace('@DAEMON_PORT@','%DPORT%').Replace('@BRIDGE_PORT@','%BPORT%') | Set-Content -Encoding ASCII '%PREFIX%\nginx.conf'"
start "AmF X1 bridge" /min cmd /c "set BRIDGE_PORT=%BPORT%&& "%WEB%\node\node.exe" "%WEB%\bridge\bridge\server.js""
start "" /b "%WEB%\nginx\nginx.exe" -p "%PREFIX%" -c "%PREFIX%\nginx.conf"
echo web remote: http://127.0.0.1:%LPORT%/remote/
