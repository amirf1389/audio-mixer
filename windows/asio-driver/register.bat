@echo off
rem Registers the Audio Mixer ASIO driver for ALL users (needs administrator rights): it then appears in the ASIO driver list of DAWs, OBS ...
rem Use the 64-bit DLL for 64-bit hosts; the 32-bit DLL is for 32-bit hosts. Both can be registered. Nothing else is changed on the PC.
net session >/dev/null 2>/dev/null || (echo Run this file as administrator: right-click, Run as administrator. & pause & exit /b 1)
if exist "%~dp0AudioMixerASIO64.dll" regsvr32 /s "%~dp0AudioMixerASIO64.dll" && echo Registered AudioMixerASIO64.dll
if exist "%~dp0AudioMixerASIO32.dll" "%SystemRoot%\SysWOW64\regsvr32.exe" /s "%~dp0AudioMixerASIO32.dll" && echo Registered AudioMixerASIO32.dll
echo Done. Start Audio Mixer, then pick "Audio Mixer" as the ASIO driver in your host.
pause
