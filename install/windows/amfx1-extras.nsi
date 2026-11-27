; AmF X1 Extras by AMFB Audio: the optional parts of AmF X1 in their own setup, so that the main setup stays small. Installs into the folder
; of the installed AmF X1 (HKLM\Software\AMFB Audio\AmF X1, InstallDir): the Plug-in Builder and its VST3 shell, AmF X1 Hybrid (program and
; extension), the WEB STACK (web\: nginx, Node.js, the bridge, the browser remote) and the Steinberg ASIO usage guidelines. Its own
; uninstaller (Uninstall AmF X1 Extras.exe) and its own entry in Apps & features; uninstalling AmF X1 removes the extras with it.
;   makensis -DVERSION=1.26.0.0 -DSRC=dist\windows\AmF-X1-1.26.0.0-windows-x64-extras -DOUT=releases\windows\AmF-X1-Extras-1.26.0.0-setup.exe install\windows\amfx1-extras.nsi
; RETURN CODES (as the main setup): 0 success, 1 cancelled, 2 failure (also: AmF X1 is not installed), 1618 another AmF X1 setup is running,
; 112 not enough disk space, 3010 installed but a restart is needed. Silent: /S.
Unicode true
!include "MUI2.nsh"
!include "FileFunc.nsh"
!ifndef VERSION
  !define VERSION "0.1.0"
!endif
!ifndef SRC
  !define SRC "..\..\dist\windows\AmF-X1-${VERSION}-windows-x64-extras"
!endif
!ifndef OUT
  !define OUT "..\..\dist\windows\AmF-X1-Extras-${VERSION}-setup.exe"
!endif
!define ARP "Software\Microsoft\Windows\CurrentVersion\Uninstall\AmFX1Extras"
!tempfile REQKB_FILE
!system 'du -sk "${SRC}" | cut -f1 > "${REQKB_FILE}"' = 0
!define /file REQKB "${REQKB_FILE}"
!delfile "${REQKB_FILE}"
Name "AmF X1 Extras"
OutFile "${OUT}"
InstallDir "$PROGRAMFILES64\AmF X1"
InstallDirRegKey HKLM "Software\AMFB Audio\AmF X1" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
SetCompressorDictSize 64
BrandingText "AmF X1 Extras by AMFB Audio"
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "AmF X1 Extras"
VIAddVersionKey "CompanyName" "AMFB Audio"
VIAddVersionKey "FileDescription" "AmF X1 Extras setup"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "Copyright AMFB Audio. MIT License."
!define MUI_ICON "..\..\native\amixos\win\amfx1.ico"
!define MUI_UNICON "..\..\native\amixos\win\amfx1.ico"
!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\..\LICENSE"
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
; what uses the extras' files: the console (it loads AmF X1 Hybrid.dll), Hybrid, the Plug-in Builder and nginx of the web stack
!macro CloseExtras UN
Function ${UN}CloseOne   ; $R9 = the program's file name: asked to close, forced after $R8 half seconds
  nsExec::Exec 'taskkill /IM "$R9"'
  StrCpy $R2 0
  closewait:
    nsExec::ExecToStack 'cmd /c tasklist /FI "IMAGENAME eq $R9" | find /I "$R9"'
    Pop $R3
    Pop $R4
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
Function ${UN}CloseAll
  IfFileExists "$INSTDIR\web\nginx\nginx.exe" 0 +2
    nsExec::Exec '"$INSTDIR\web\nginx\nginx.exe" -p "$LOCALAPPDATA\AMFB Audio\AmF X1\webstack" -c "$LOCALAPPDATA\AMFB Audio\AmF X1\webstack\nginx.conf" -s stop'
  StrCpy $R8 20
  StrCpy $R9 "amixos.exe"
  Call ${UN}CloseOne
  StrCpy $R8 4
  StrCpy $R9 "AmF X1 Hybrid.exe"
  Call ${UN}CloseOne
  StrCpy $R9 "AmF X1 Plugin Builder.exe"
  Call ${UN}CloseOne
FunctionEnd
!macroend
!insertmacro CloseExtras ""
!insertmacro CloseExtras "un."
Function .onInit
  System::Call 'kernel32::CreateMutex(p 0, i 0, t "AmFX1SetupMutex") p .r1 ?e'   ; the same mutex as the main setup: never both at once
  Pop $R0
  StrCmp $R0 183 0 notRunning
    SetErrorLevel 1618
    Abort
  notRunning:
  ClearErrors
  ReadRegStr $R0 HKLM "Software\AMFB Audio\AmF X1" "InstallDir"
  IfErrors noCore
  IfFileExists "$R0\amixos.exe" 0 noCore
  StrCpy $INSTDIR $R0
  Goto coreOk
  noCore:
    SetErrorLevel 2
    MessageBox MB_OK|MB_ICONSTOP "AmF X1 is not installed on this computer. Install AmF X1 first, then AmF X1 Extras." /SD IDOK
    Abort
  coreOk:
  StrCpy $R1 $INSTDIR 3
  ClearErrors
  ${DriveSpace} $R1 "/D=F /S=K" $R2
  IfErrors spaceOk
  IntCmp $R2 ${REQKB} spaceOk 0 spaceOk
    SetErrorLevel 112
    MessageBox MB_OK|MB_ICONSTOP "Not enough free disk space on $R1 for AmF X1 Extras (needs about ${REQKB} KB)." /SD IDOK
    Abort
  spaceOk:
FunctionEnd
Function .onInstSuccess
  IfRebootFlag 0 +2
  SetErrorLevel 3010
FunctionEnd
Function .onInstFailed
  SetErrorLevel 2
FunctionEnd
Section "AmF X1 Extras" SecExtras
  SectionIn RO
  Call CloseAll
  SetOverwrite on
  SetOutPath "$INSTDIR"
  ; a file still held (Hybrid.dll in a console another user runs) is replaced at the next restart instead of stopping the install
  IfFileExists "$INSTDIR\AmF X1 Hybrid.dll" 0 +3
    Rename "$INSTDIR\AmF X1 Hybrid.dll" "$INSTDIR\AmF X1 Hybrid.dll.old"
    Delete /REBOOTOK "$INSTDIR\AmF X1 Hybrid.dll.old"
  File /r "${SRC}\*.*"
  WriteRegStr HKLM "Software\AMFB Audio\AmF X1 Extras" "Version" "${VERSION}"
  WriteRegStr HKLM "${ARP}" "DisplayName" "AmF X1 Extras"
  WriteRegStr HKLM "${ARP}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${ARP}" "Publisher" "AMFB Audio"
  WriteRegStr HKLM "${ARP}" "DisplayIcon" "$INSTDIR\amixos.exe"
  WriteRegStr HKLM "${ARP}" "UninstallString" '"$INSTDIR\Uninstall AmF X1 Extras.exe"'
  WriteRegStr HKLM "${ARP}" "QuietUninstallString" '"$INSTDIR\Uninstall AmF X1 Extras.exe" /S'
  WriteRegDWORD HKLM "${ARP}" "EstimatedSize" ${REQKB}
  WriteRegDWORD HKLM "${ARP}" "NoModify" 1
  WriteRegDWORD HKLM "${ARP}" "NoRepair" 1
  WriteUninstaller "$INSTDIR\Uninstall AmF X1 Extras.exe"
  CreateDirectory "$SMPROGRAMS\AmF X1"
  CreateShortcut "$SMPROGRAMS\AmF X1\AmF X1 Plugin Builder.lnk" "$INSTDIR\AmF X1 Plugin Builder.exe"
SectionEnd
Section "Uninstall"
  Call un.CloseAll
  Delete "$SMPROGRAMS\AmF X1\AmF X1 Plugin Builder.lnk"
  Delete /REBOOTOK "$INSTDIR\AmF X1 Plugin Builder.exe"
  Delete /REBOOTOK "$INSTDIR\AmF X1 Hybrid.exe"
  Delete /REBOOTOK "$INSTDIR\AmF X1 Hybrid.dll"
  RMDir /r /REBOOTOK "$INSTDIR\AMFB Plugin Shell.vst3"
  RMDir /r /REBOOTOK "$INSTDIR\web"
  Delete "$INSTDIR\drivers\asio\Steinberg ASIO Usage Guidelines.pdf"
  Delete "$INSTDIR\licenses\NODEJS-LICENSE.txt"
  Delete "$INSTDIR\licenses\NGINX-LICENSE.txt"
  Delete "$INSTDIR\licenses\NGINX-zlib-LICENSE.txt"
  Delete "$INSTDIR\licenses\NGINX-PCRE-LICENCE.txt"
  Delete "$INSTDIR\licenses\NGINX-OpenSSL-LICENSE.txt"
  Delete "$INSTDIR\licenses\WEB-STACK-SOURCES.txt"
  Delete "$INSTDIR\Uninstall AmF X1 Extras.exe"
  DeleteRegKey HKLM "${ARP}"
  DeleteRegKey HKLM "Software\AMFB Audio\AmF X1 Extras"
SectionEnd
