; AmF X1 by AMFB Audio: Windows installer (Inno Setup 6). Everything of the program goes into Program Files; nothing is written there at run
; time: the program keeps its data in %APPDATA%\AMFB Audio\AmF X1, its cache and prefetch under %LOCALAPPDATA%\AMFB Audio\AmF X1, temp files
; in %TEMP%\AmF X1 (install\layout.json). Uninstall removes the program and the cache; the data (licence, scenes, presets) stays unless the
; user says so. Build: iscc install\windows\amfx1.iss  (after the Qt console and the engine are built into dist\windows\ and signed with scripts/sign-windows.js;
; sign the setup .exe too). SignTool=... can be set in the Inno Setup IDE to sign the setup automatically.
#define AppName "AmF X1"
#define AppVendor "AMFB Audio"
#define AppVersion GetEnv("AMFX1_VERSION")
#if AppVersion == ""
  #define AppVersion "0.1.0"
#endif
[Setup]
AppId={{7A6E1F3C-5B21-4C7D-9D2E-4F1A2B3C4D5E}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppVendor}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir=..\..\dist\windows
OutputBaseFilename=AmF-X1-{#AppVersion}-setup
Compression=lzma2/ultra
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayIcon={app}\amixos.exe
[Files]
Source: "..\..\dist\windows\amixos.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\dist\windows\amixos-daemon.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\dist\windows\amix.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\dist\windows\AmF X1.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\dist\windows\AudioMixerASIO64.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\..\dist\windows\edition-token.json"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\..\dist\windows\qt\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
[Dirs]
Name: "{userappdata}\{#AppVendor}\{#AppName}"
Name: "{localappdata}\{#AppVendor}\{#AppName}\cache"
Name: "{localappdata}\{#AppVendor}\{#AppName}\prefetch"
[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\amixos.exe"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\amixos.exe"; Tasks: desktopicon
[Tasks]
Name: "desktopicon"; Description: "Create a desktop icon"; GroupDescription: "Additional icons:"
[Run]
Filename: "{app}\amixos.exe"; Description: "Start {#AppName}"; Flags: nowait postinstall skipifsilent
[UninstallDelete]
Type: filesandordirs; Name: "{localappdata}\{#AppVendor}\{#AppName}"
Type: filesandordirs; Name: "{tmp}\..\{#AppName}"
[Code]
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  { the cache goes with the program; the data (licence, scenes, presets) only when the user says so }
  if (CurUninstallStep = usPostUninstall) and (MsgBox('Also remove your AmF X1 data (licence, scenes, presets) from ' + ExpandConstant('{userappdata}\{#AppVendor}\{#AppName}') + '?', mbConfirmation, MB_YESNO) = IDYES) then
    DelTree(ExpandConstant('{userappdata}\{#AppVendor}\{#AppName}'), True, True, True);
end;
