' Audio Mixer - double-click launcher for Windows (VBScript).
' It runs start-pc-mode.bat in a normal, visible console window (nothing is hidden) and the page opens in your browser.
' If Windows has VBScript turned off (optional feature on Windows 11 24H2+), run start-pc-mode.bat instead.
Option Explicit
Dim sh, fso, here, bat
Set sh = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
here = fso.GetParentFolderName(WScript.ScriptFullName)
bat = here & "\start-pc-mode.bat"
If Not fso.FileExists(bat) Then
  MsgBox "start-pc-mode.bat was not found next to this file:" & vbCrLf & here, vbExclamation, "Audio Mixer"
  WScript.Quit 1
End If
sh.CurrentDirectory = here
sh.Run "cmd /c """ & bat & """", 1, False
