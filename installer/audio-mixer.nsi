; Audio Mixer local system server: Windows installer (NSIS). Built by scripts/build-installer.js
;   makensis -DVERSION=1.0.0 -DSTAGE=<staged folder> -DOUTFILE=<setup.exe> installer/audio-mixer.nsi
; Per-user install (no administrator rights). Bundles the official Node.js runtime, so Node.js need not be installed.
Unicode true
!ifndef VERSION
  !define VERSION "1.0.0"
!endif
!ifndef VERSION4
  !define VERSION4 "1.0.0.0"
!endif
!ifndef STAGE
  !error "pass -DSTAGE=<staged folder>"
!endif
!ifndef OUTFILE
  !define OUTFILE "AudioMixer-Setup.exe"
!endif

!include "MUI2.nsh"
!include "LogicLib.nsh"

Name "Audio Mixer Local Server"
OutFile "${OUTFILE}"
RequestExecutionLevel user
InstallDir "$LOCALAPPDATA\Programs\AudioMixer"
InstallDirRegKey HKCU "Software\AudioMixer" "InstallDir"
SetCompressor /SOLID lzma
BrandingText "Audio Mixer ${VERSION}"

VIProductVersion "${VERSION4}"
VIAddVersionKey "ProductName" "Audio Mixer Local Server"
VIAddVersionKey "FileDescription" "Audio Mixer local system server installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "MIT License"

!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_FUNCTION LaunchApp
!define MUI_FINISHPAGE_RUN_TEXT "Start Audio Mixer (PC mode) now"
!define MUI_FINISHPAGE_RUN_NOTCHECKED

Function LaunchApp
  SetOutPath "$INSTDIR"
  Exec '"$INSTDIR\runtime\node.exe" "$INSTDIR\client\cli.js"'
FunctionEnd

!insertmacro MUI_PAGE_LICENSE "${STAGE}\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "Audio Mixer and local server (required)" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  File /r "${STAGE}\*.*"
  WriteRegStr HKCU "Software\AudioMixer" "InstallDir" "$INSTDIR"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AudioMixer" "DisplayName" "Audio Mixer Local Server"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AudioMixer" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AudioMixer" "Publisher" "Audio Mixer"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AudioMixer" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AudioMixer" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AudioMixer" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AudioMixer" "NoRepair" 1
SectionEnd

Section "Start Menu shortcuts" SecMenu
  CreateDirectory "$SMPROGRAMS\Audio Mixer"
  SetOutPath "$INSTDIR"
  CreateShortCut "$SMPROGRAMS\Audio Mixer\Audio Mixer (PC mode).lnk" "$INSTDIR\runtime\node.exe" '"$INSTDIR\client\cli.js"' "$INSTDIR\runtime\node.exe" 0
  CreateShortCut "$SMPROGRAMS\Audio Mixer\Audio Mixer local server only.lnk" "$INSTDIR\runtime\node.exe" '"$INSTDIR\bridge\server.js"' "$INSTDIR\runtime\node.exe" 0
  CreateShortCut "$SMPROGRAMS\Audio Mixer\Uninstall Audio Mixer.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section /o "Desktop shortcut" SecDesktop
  SetOutPath "$INSTDIR"
  CreateShortCut "$DESKTOP\Audio Mixer.lnk" "$INSTDIR\runtime\node.exe" '"$INSTDIR\client\cli.js"' "$INSTDIR\runtime\node.exe" 0
SectionEnd

Section "Start the local server when I log in" SecAuto
  ; Uses the bundled Node.js; writes a Startup-folder script, removed again by the uninstaller.
  ExecWait '"$INSTDIR\runtime\node.exe" "$INSTDIR\client\cli.js" service install'
SectionEnd

LangString DESC_Main ${LANG_ENGLISH} "The mixer page, the local system server, the client and the bundled Node.js runtime."
LangString DESC_Menu ${LANG_ENGLISH} "Start Menu entries for PC mode and for the server only."
LangString DESC_Desktop ${LANG_ENGLISH} "A desktop shortcut that starts PC mode."
LangString DESC_Auto ${LANG_ENGLISH} "Start the local server automatically at every login (no administrator rights; removed on uninstall)."
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} $(DESC_Main)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMenu} $(DESC_Menu)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} $(DESC_Desktop)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecAuto} $(DESC_Auto)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  ; Stop autostart first. Downloaded drivers in %USERPROFILE%\AudioMixerDrivers are left alone.
  IfFileExists "$INSTDIR\runtime\node.exe" 0 +2
    ExecWait '"$INSTDIR\runtime\node.exe" "$INSTDIR\client\cli.js" service uninstall'
  Delete "$SMPROGRAMS\Audio Mixer\Audio Mixer (PC mode).lnk"
  Delete "$SMPROGRAMS\Audio Mixer\Audio Mixer local server only.lnk"
  Delete "$SMPROGRAMS\Audio Mixer\Uninstall Audio Mixer.lnk"
  RMDir "$SMPROGRAMS\Audio Mixer"
  Delete "$DESKTOP\Audio Mixer.lnk"
  RMDir /r /REBOOTOK "$INSTDIR"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AudioMixer"
  DeleteRegKey HKCU "Software\AudioMixer"
SectionEnd
