; AmF X1 by AMFB Audio: Windows setup built with NSIS (makensis, also on Linux). Installs the console, the engine, the daemon, the ASIO driver
; DLL and the Qt runtime into Program Files; data stays in %APPDATA%\AMFB Audio\AmF X1, the cache under %LOCALAPPDATA% (install\layout.json).
; The uninstaller removes the program and the cache and asks before removing the data. Sign the setup after building (scripts/sign-windows.js).
;   makensis -DVERSION=0.1.0 -DSRC=dist\windows\AmF-X1-0.1.0-windows-x64 -DOUT=dist\windows\AmF-X1-0.1.0-setup.exe install\windows\amfx1.nsi
Unicode true
!include "MUI2.nsh"
!ifndef VERSION
  !define VERSION "0.1.0"
!endif
!ifndef SRC
  !define SRC "..\..\dist\windows\AmF-X1-${VERSION}-windows-x64"
!endif
!ifndef OUT
  !define OUT "..\..\dist\windows\AmF-X1-${VERSION}-setup.exe"
!endif
Name "AmF X1"
OutFile "${OUT}"
InstallDir "$PROGRAMFILES64\AmF X1"
InstallDirRegKey HKLM "Software\AMFB Audio\AmF X1" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
BrandingText "AmF X1 by AMFB Audio"
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "AmF X1"
VIAddVersionKey "CompanyName" "AMFB Audio"
VIAddVersionKey "FileDescription" "AmF X1 setup"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "Copyright AMFB Audio. MIT License."
!define MUI_ICON "..\..\native\amixos\win\amfx1.ico"
!define MUI_UNICON "..\..\native\amixos\win\amfx1.ico"
!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\amixos.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Start AmF X1"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
!include "FileFunc.nsh"
Var Restart
Function .onInit
  ; AUTO UPDATE: the console starts this setup with  /S /RESTART  and quits; wait for it to let go of its files, install silently, start it again
  ${GetParameters} $R0
  ${GetOptions} $R0 "/RESTART" $R1
  IfErrors +2 0
  StrCpy $Restart "1"
FunctionEnd
Section "AmF X1" SecMain
  SectionIn RO
  StrCmp $Restart "1" 0 nowait
  StrCpy $R2 0
  waitloop:
    ClearErrors
    FileOpen $R3 "$INSTDIR\amixos.exe" a
    IfErrors 0 closed
    IntOp $R2 $R2 + 1
    IntCmp $R2 40 nowait 0 nowait   ; 20 s at most
    Sleep 500
    Goto waitloop
  closed:
    FileClose $R3
  nowait:
  SetOutPath "$INSTDIR"
  File /r "${SRC}\*.*"
  CreateDirectory "$APPDATA\AMFB Audio\AmF X1"
  CreateDirectory "$LOCALAPPDATA\AMFB Audio\AmF X1\cache"
  CreateDirectory "$LOCALAPPDATA\AMFB Audio\AmF X1\prefetch"
  WriteRegStr HKLM "Software\AMFB Audio\AmF X1" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\AMFB Audio\AmF X1" "Version" "${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AmFX1" "DisplayName" "AmF X1"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AmFX1" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AmFX1" "Publisher" "AMFB Audio"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AmFX1" "DisplayIcon" "$INSTDIR\amixos.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AmFX1" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AmFX1" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AmFX1" "NoRepair" 1
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateDirectory "$SMPROGRAMS\AmF X1"
  CreateShortcut "$SMPROGRAMS\AmF X1\AmF X1.lnk" "$INSTDIR\amixos.exe"
  CreateShortcut "$SMPROGRAMS\AmF X1\Uninstall AmF X1.lnk" "$INSTDIR\Uninstall.exe"
  CreateShortcut "$DESKTOP\AmF X1.lnk" "$INSTDIR\amixos.exe"
SectionEnd
Section "ASIO driver (register AudioMixerASIO64.dll)" SecAsio
  ; the DLL stays in the program folder; it is registered in place (the program's --install-driver does the same)
  ExecWait '"$INSTDIR\AmF X1.exe" --install-driver'
SectionEnd
Section "-Restart after an update"
  StrCmp $Restart "1" 0 +2
  Exec '"$WINDIR\explorer.exe" "$INSTDIR\amixos.exe"'   ; through the shell: the console starts as the normal user, not elevated
SectionEnd
Section "Uninstall"
  ExecWait '"$INSTDIR\AmF X1.exe" --uninstall-driver'
  Delete "$DESKTOP\AmF X1.lnk"
  RMDir /r "$SMPROGRAMS\AmF X1"
  RMDir /r "$INSTDIR"
  RMDir /r "$LOCALAPPDATA\AMFB Audio\AmF X1"
  MessageBox MB_YESNO "Also remove your AmF X1 data (licence, scenes, presets) from $APPDATA\AMFB Audio\AmF X1?" IDNO keep
    RMDir /r "$APPDATA\AMFB Audio\AmF X1"
  keep:
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AmFX1"
  DeleteRegKey HKLM "Software\AMFB Audio\AmF X1"
SectionEnd
