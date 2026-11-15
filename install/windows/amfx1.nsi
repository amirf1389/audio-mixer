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
!insertmacro MUI_PAGE_COMPONENTS   ; what the setup installs is shown and can be chosen: the ASIO driver (a system-wide COM driver) is optional
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
; CLOSE A RUNNING CONSOLE before files and settings are touched: asked to close first (it saves and gives Windows' sound back), forced after 10 s.
; Without this an uninstall left the console running; on quit it wrote its state (a master at -inf with it) back after the reset.
!macro CloseConsole UN
Function ${UN}CloseConsole
  nsExec::Exec 'taskkill /IM amixos.exe'
  StrCpy $R2 0
  closewait:
    nsExec::ExecToStack 'cmd /c tasklist /FI "IMAGENAME eq amixos.exe" | find /I "amixos.exe"'
    Pop $R3
    StrCmp $R3 "0" 0 closed
    IntOp $R2 $R2 + 1
    IntCmp $R2 20 forced 0 forced
    Sleep 500
    Goto closewait
  forced:
    nsExec::Exec 'taskkill /F /IM amixos.exe'
    Sleep 500
  closed:
FunctionEnd
!macroend
!insertmacro CloseConsole ""
!insertmacro CloseConsole "un."
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
  StrCmp $Restart "1" +2 0
  Call CloseConsole   ; a setup started by hand: the console may be open
  ; THE INSTALL ID: written once per fresh install (an update over an installed console keeps it). The console compares it with the one it
  ; last saw; a new id means this is a new install, so it starts from the factory console (no master left at -inf from before).
  IfFileExists "$INSTDIR\install.id" idDone 0
  ${GetTime} "" "L" $0 $1 $2 $3 $4 $5 $6
  CreateDirectory "$INSTDIR"
  FileOpen $7 "$INSTDIR\install.id" w
  FileWrite $7 "$2$1$0-$4$5$6"
  FileClose $7
  idDone:
  SetOutPath "$INSTDIR"
  File /r /x "AmF X1 Diagnostics.exe" /x "AmF X1 Plugin Builder.exe" "${SRC}\*.*"   ; the two tools come with their own sections below
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
SectionEnd
Section "Diagnostics app (checks speakers, headphones, microphones and drivers)" SecDiag
  SetOutPath "$INSTDIR"
  File /nonfatal "${SRC}\AmF X1 Diagnostics.exe"
  CreateDirectory "$SMPROGRAMS\AmF X1"
  CreateShortcut "$SMPROGRAMS\AmF X1\AmF X1 Diagnostics.lnk" "$INSTDIR\AmF X1 Diagnostics.exe"
SectionEnd
Section "Plugin Builder (design your own VST3 plug-ins)" SecBuilder
  SetOutPath "$INSTDIR"
  File /nonfatal "${SRC}\AmF X1 Plugin Builder.exe"
  CreateDirectory "$SMPROGRAMS\AmF X1"
  CreateShortcut "$SMPROGRAMS\AmF X1\AmF X1 Plugin Builder.lnk" "$INSTDIR\AmF X1 Plugin Builder.exe"
SectionEnd
Section "Desktop shortcut" SecDesktop
  CreateShortcut "$DESKTOP\AmF X1.lnk" "$INSTDIR\amixos.exe"
SectionEnd
Section "ASIO driver (lets other programs reach the console)" SecAsio
  ; the DLL stays in the program folder; it is registered in place (the program's --install-driver does the same)
  ExecWait '"$INSTDIR\AmF X1.exe" --install-driver'
SectionEnd
; PHONE REMOTE OVER WI-FI: without an inbound rule Windows Firewall drops the phones' connections to port 8765 (the console's NETWORK ->
; PHONE REMOTE service), often without asking. The rules are as narrow as the service itself: TCP 8765 only, these two programs only,
; PRIVATE networks only (home / studio Wi-Fi, never public), and only from the local subnet. The uninstaller removes them.
Section "Phone remote over Wi-Fi (Windows Firewall rule: private network, port 8765)" SecFirewall
  nsExec::Exec 'netsh advfirewall firewall delete rule name="AmF X1 Phone Remote"'
  nsExec::Exec 'netsh advfirewall firewall add rule name="AmF X1 Phone Remote" dir=in action=allow protocol=TCP localport=8765 profile=private remoteip=localsubnet program="$INSTDIR\amixos.exe" description="AmF X1: phones and tablets on this network reach the console remote (token or PIN needed)"'
  nsExec::Exec 'netsh advfirewall firewall add rule name="AmF X1 Phone Remote" dir=in action=allow protocol=TCP localport=8765 profile=private remoteip=localsubnet program="$INSTDIR\AmF X1 Network.exe" description="AmF X1: phones and tablets on this network reach the console remote (token or PIN needed)"'
SectionEnd
Section "-Restart after an update"
  StrCmp $Restart "1" 0 +2
  Exec '"$WINDIR\explorer.exe" "$INSTDIR\amixos.exe"'   ; through the shell: the console starts as the normal user, not elevated
SectionEnd
Section "Uninstall"
  Call un.CloseConsole   ; before anything is removed: a console left running would write its settings back on quit
  ExecWait '"$INSTDIR\AmF X1.exe" --uninstall-driver'
  nsExec::Exec 'netsh advfirewall firewall delete rule name="AmF X1 Phone Remote"'   ; the phone remote's firewall rules go with the program
  Delete "$DESKTOP\AmF X1.lnk"
  Delete "$DESKTOP\AmF X1 Diagnostics.lnk"
  RMDir /r "$SMPROGRAMS\AmF X1"
  RMDir /r "$INSTDIR"
  ; EVERY UNINSTALL IS A FACTORY RESET: the settings (registry), the console, scenes, presets, caches and network tokens of this user
  ; are removed, so that a later install starts exactly as from the factory. Only the licence key is asked about.
  SetShellVarContext current
  DeleteRegKey HKCU "Software\AMFB Audio\A mix OS"
  DeleteRegKey /ifempty HKCU "Software\AMFB Audio"
  RMDir /r "$LOCALAPPDATA\AMFB Audio\AmF X1"
  StrCpy $0 "$APPDATA\AMFB Audio"
  Delete "$0\amfx1-license.keep"
  Delete "$0\amfx1-edition.keep"
  Rename "$0\AmF X1\license.json" "$0\amfx1-license.keep"
  Rename "$0\AmF X1\edition.json" "$0\amfx1-edition.keep"
  RMDir /r "$0\AmF X1"
  IfFileExists "$0\amfx1-license.keep" askLicence 0
  IfFileExists "$0\amfx1-edition.keep" askLicence licenceDone
  askLicence:
  MessageBox MB_YESNO "AmF X1 is back to its factory state on this computer.$\n$\nKeep the licence key here for a later install?" IDYES keepLicence
    Delete "$0\amfx1-license.keep"
    Delete "$0\amfx1-edition.keep"
    Goto licenceDone
  keepLicence:
    CreateDirectory "$0\AmF X1"
    Rename "$0\amfx1-license.keep" "$0\AmF X1\license.json"
    Rename "$0\amfx1-edition.keep" "$0\AmF X1\edition.json"
  licenceDone:
  RMDir "$0"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AmFX1"
  DeleteRegKey HKLM "Software\AMFB Audio\AmF X1"
SectionEnd
