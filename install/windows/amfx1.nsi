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
; STORE RETURN CODES (Partner Center "Installer handling"): 0 success, 1 cancelled, 2 failure, 1618 another AmF X1 setup is running,
; 112 not enough disk space, 3010 installed but a restart is needed (a file in use is replaced at the next start; never restarts by itself)
!tempfile REQKB_FILE
!system 'du -sk "${SRC}" | cut -f1 > "${REQKB_FILE}"' = 0
!define /file REQKB "${REQKB_FILE}"
!delfile "${REQKB_FILE}"
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
; CLOSE EVERY AmF X1 PROGRAM before files and settings are touched. Not only the console: the phone-remote service (AmF X1 Network.exe, a
; child of the console that keeps running when the console is forced closed), AmF X1 Hybrid.exe, AmF X1.exe, the Diagnostics app and the
; Plugin Builder. A copy of any of them still running locks its file, and the setup stopped with "Error opening file for writing:
; ...\AmF X1 Network.exe". Each is asked to close first (the console saves and gives Windows' sound back), then forced with what it started.
; Without this an uninstall also left the console running; on quit it wrote its state (a master at -inf with it) back after the reset.
!macro CloseApps UN
Function ${UN}CloseOne   ; $R9 = the program's file name, $R8 = how many half seconds to wait for it to close by itself
  nsExec::Exec 'taskkill /IM "$R9"'
  StrCpy $R2 0
  closewait:
    nsExec::ExecToStack 'cmd /c tasklist /FI "IMAGENAME eq $R9" | find /I "$R9"'
    Pop $R3   ; exit code: 0 = still running
    Pop $R4   ; the output (not needed)
    StrCmp $R3 "0" 0 closed
    IntOp $R2 $R2 + 1
    IntCmp $R2 $R8 forced 0 forced
    Sleep 500
    Goto closewait
  forced:
    nsExec::Exec 'taskkill /F /T /IM "$R9"'
    Sleep 700
  closed:
FunctionEnd
Function ${UN}CloseConsole
  StrCpy $R8 20
  StrCpy $R9 "amixos.exe"
  Call ${UN}CloseOne   ; the console first (up to 10 s: it saves), then what it may have left running
  StrCpy $R8 4
  StrCpy $R9 "AmF X1 Network.exe"
  Call ${UN}CloseOne
  StrCpy $R9 "AmF X1 Hybrid.exe"
  Call ${UN}CloseOne
  StrCpy $R9 "AmF X1.exe"
  Call ${UN}CloseOne
  StrCpy $R9 "AmF X1 Diagnostics.exe"
  Call ${UN}CloseOne
  StrCpy $R9 "AmF X1 Plugin Builder.exe"
  Call ${UN}CloseOne
  ; the WEB STACK (web\nginx, started by the console or web\start-web-stack.cmd): asked to stop through its own pid file, never by image name
  IfFileExists "$INSTDIR\web\nginx\nginx.exe" 0 +2
    nsExec::Exec '"$INSTDIR\web\nginx\nginx.exe" -p "$LOCALAPPDATA\AMFB Audio\AmF X1\webstack" -c "$LOCALAPPDATA\AMFB Audio\AmF X1\webstack\nginx.conf" -s stop'
FunctionEnd
; A FILE STILL IN USE after that (the ASIO driver DLL loaded by a DAW, a program another user runs): Windows lets a file in use be renamed,
; so it is moved aside to <name>.old<n> and removed at the next restart, and the new file goes in its place. The install never stops on it.
Function ${UN}FreeFile   ; $R9 = a file name in $INSTDIR
  IfFileExists "$INSTDIR\$R9" 0 free
  ClearErrors
  FileOpen $R3 "$INSTDIR\$R9" a
  IfErrors locked
  FileClose $R3
  Goto free
  locked:
    StrCpy $R5 0
    nextName:
      IfFileExists "$INSTDIR\$R9.old$R5" 0 doRename
      Delete "$INSTDIR\$R9.old$R5"
      IfFileExists "$INSTDIR\$R9.old$R5" 0 doRename
      IntOp $R5 $R5 + 1
      IntCmp $R5 20 free nextName free
    doRename:
      ClearErrors
      Rename "$INSTDIR\$R9" "$INSTDIR\$R9.old$R5"
      IfErrors free
      Delete /REBOOTOK "$INSTDIR\$R9.old$R5"
      DetailPrint "In use, replaced at the next restart: $R9"
  free:
FunctionEnd
Function ${UN}FreeAll
  StrCpy $R9 "amixos.exe"
  Call ${UN}FreeFile
  StrCpy $R9 "AmF X1 Network.exe"
  Call ${UN}FreeFile
  StrCpy $R9 "AmF X1 Network.dll"
  Call ${UN}FreeFile
  StrCpy $R9 "AmF X1 Hybrid.exe"
  Call ${UN}FreeFile
  StrCpy $R9 "AmF X1 Hybrid.dll"
  Call ${UN}FreeFile
  StrCpy $R9 "AmF X1.exe"
  Call ${UN}FreeFile
  StrCpy $R9 "AmF X1 Diagnostics.exe"
  Call ${UN}FreeFile
  StrCpy $R9 "AmF X1 Plugin Builder.exe"
  Call ${UN}FreeFile
  StrCpy $R9 "AudioMixerASIO64.dll"
  Call ${UN}FreeFile
  StrCpy $R9 "AudioMixerASIO32.dll"
  Call ${UN}FreeFile
  StrCpy $R9 "amix_soxr.dll"
  Call ${UN}FreeFile
FunctionEnd
!macroend
!insertmacro CloseApps ""
!insertmacro CloseApps "un."
Function .onInit
  System::Call 'kernel32::CreateMutex(p 0, i 0, t "AmFX1SetupMutex") p .r1 ?e'
  Pop $R0
  StrCmp $R0 183 0 notRunning   ; ERROR_ALREADY_EXISTS: another copy of this setup is running
    SetErrorLevel 1618
    Abort
  notRunning:
  StrCpy $R1 $INSTDIR 3
  ClearErrors
  ${DriveSpace} $R1 "/D=F /S=K" $R2
  IfErrors spaceOk
  IntCmp $R2 ${REQKB} spaceOk 0 spaceOk
    SetErrorLevel 112
    MessageBox MB_OK|MB_ICONSTOP "Not enough free disk space on $R1 for AmF X1 (needs about ${REQKB} KB)." /SD IDOK
    Abort
  spaceOk:
  ; AUTO UPDATE: the console starts this setup with  /S /RESTART  and quits; wait for it to let go of its files, install silently, start it again
  ${GetParameters} $R0
  ${GetOptions} $R0 "/RESTART" $R1
  IfErrors +2 0
  StrCpy $Restart "1"
FunctionEnd
Function .onInstSuccess
  IfRebootFlag 0 +2
  SetErrorLevel 3010
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
  Call CloseConsole   ; by hand or after an update: whatever of AmF X1 is still running (the update's console has already quit)
  Call FreeAll        ; and a file another program still holds is moved aside instead of stopping the install
  ; THE INSTALL ID: written once per fresh install (an update over an installed console keeps it). The console compares it with the one it
  ; last saw; a new id means this is a new install, so it starts from the factory console (no master left at -inf from before).
  IfFileExists "$INSTDIR\install.id" idDone 0
  ; A CLEAN INSTALL (no install.id: not an update): what an earlier install of this user left (settings in the registry, the console, scenes,
  ; presets, caches, network tokens) is removed, as the uninstaller does; the licence key and the edition are kept. The console also runs a
  ; full factory reset on its first start (install.id is new to it).
  SetShellVarContext current
  DeleteRegKey HKCU "Software\AMFB Audio\A mix OS"
  RMDir /r "$LOCALAPPDATA\AMFB Audio\AmF X1"
  StrCpy $8 "$APPDATA\AMFB Audio"
  IfFileExists "$8\AmF X1\*.*" 0 cleanDone
    CreateDirectory "$8\amfx1-keep"
    Rename "$8\AmF X1\license.json" "$8\amfx1-keep\license.json"
    Rename "$8\AmF X1\edition.json" "$8\amfx1-keep\edition.json"
    RMDir /r "$8\AmF X1"
    CreateDirectory "$8\AmF X1"
    Rename "$8\amfx1-keep\license.json" "$8\AmF X1\license.json"
    Rename "$8\amfx1-keep\edition.json" "$8\AmF X1\edition.json"
    RMDir "$8\amfx1-keep"
  cleanDone:
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
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AmFX1" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AmFX1" "EstimatedSize" ${REQKB}
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
  ; the 32-bit driver for 32-bit host programs: Windows' 32-bit regsvr32 registers it in the 32-bit registry view (WOW6432Node)
  IfFileExists "$WINDIR\SysWOW64\regsvr32.exe" 0 +2
  ExecWait '"$WINDIR\SysWOW64\regsvr32.exe" /s "$INSTDIR\AudioMixerASIO32.dll"'
SectionEnd
; PHONE REMOTE OVER WI-FI: without an inbound rule Windows Firewall drops the phones' connections to the remote port (the console's NETWORK ->
; PHONE REMOTE service), often without asking. The rules are as narrow as the service itself: TCP, these two programs only,
; PRIVATE networks only (home / studio Wi-Fi, never public), and only from the local subnet. The uninstaller removes them.
Section "Phone remote over Wi-Fi (Windows Firewall rule: this network only, the remote port set in SETTINGS)" SecFirewall
  ; ANY REMOTE PORT: the port is chosen in SETTINGS (1024-65535, default 8765) by a console that runs without admin rights and cannot
  ; edit the firewall, so the rule is bound to the two programs (and this subnet) instead of one port: a changed port keeps working.
  ; EVERY NETWORK PROFILE, THIS SUBNET ONLY: Windows marks many home Wi-Fi networks "Public"; a rule for "Private" alone then let no phone
  ; in (the phone app found the console but never synced). Still only addresses of this network (localsubnet), and the service answers
  ; private addresses only and wants the paired token or the minute's PIN.
  nsExec::Exec 'netsh advfirewall firewall delete rule name="AmF X1 Phone Remote"'
  nsExec::Exec 'netsh advfirewall firewall add rule name="AmF X1 Phone Remote" dir=in action=allow protocol=TCP profile=private,public,domain remoteip=localsubnet program="$INSTDIR\amixos.exe" description="AmF X1: phones and tablets on this network reach the console remote (token or PIN needed)"'
  nsExec::Exec 'netsh advfirewall firewall add rule name="AmF X1 Phone Remote" dir=in action=allow protocol=TCP profile=private,public,domain remoteip=localsubnet program="$INSTDIR\AmF X1 Network.exe" description="AmF X1: phones and tablets on this network reach the console remote (token or PIN needed)"'
SectionEnd
Section "-Restart after an update"
  StrCmp $Restart "1" 0 +2
  Exec '"$WINDIR\explorer.exe" "$INSTDIR\amixos.exe"'   ; through the shell: the console starts as the normal user, not elevated
SectionEnd
Section "Uninstall"
  Call un.CloseConsole   ; before anything is removed: a console left running would write its settings back on quit
  Call un.FreeAll
  ExecWait '"$INSTDIR\AmF X1.exe" --uninstall-driver'
  IfFileExists "$INSTDIR\AudioMixerASIO32.dll" 0 +2
  ExecWait '"$WINDIR\SysWOW64\regsvr32.exe" /u /s "$INSTDIR\AudioMixerASIO32.dll"'
  nsExec::Exec 'netsh advfirewall firewall delete rule name="AmF X1 Phone Remote"'   ; the phone remote's firewall rules go with the program
  Delete "$DESKTOP\AmF X1.lnk"
  Delete "$DESKTOP\AmF X1 Diagnostics.lnk"
  RMDir /r "$SMPROGRAMS\AmF X1"
  RMDir /r /REBOOTOK "$INSTDIR"   ; a file still held by another program goes at the next restart
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
  MessageBox MB_YESNO "AmF X1 is back to its factory state on this computer.$\n$\nKeep the licence key here for a later install?" /SD IDYES IDYES keepLicence
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
