@echo off
rem stops what start-web-stack.cmd started
set "WEB=%~dp0"
set "PREFIX=%LOCALAPPDATA%\AMFB Audio\AmF X1\webstack"
"%WEB%nginx\nginx.exe" -p "%PREFIX%" -c "%PREFIX%\nginx.conf" -s stop
taskkill /fi "WINDOWTITLE eq AmF X1 bridge*" /t /f >nul 2>&1
