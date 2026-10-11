@echo off
rem Removes the Audio Mixer ASIO driver from the ASIO driver list (needs administrator rights). The DLL files stay where they are.
net session >/dev/null 2>/dev/null || (echo Run this file as administrator: right-click, Run as administrator. & pause & exit /b 1)
if exist "%~dp0AudioMixerASIO64.dll" regsvr32 /u /s "%~dp0AudioMixerASIO64.dll" && echo Removed AudioMixerASIO64.dll
if exist "%~dp0AudioMixerASIO32.dll" "%SystemRoot%\SysWOW64\regsvr32.exe" /u /s "%~dp0AudioMixerASIO32.dll" && echo Removed AudioMixerASIO32.dll
pause
