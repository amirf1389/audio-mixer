' audio-devices.vbs - lists the Windows sound devices as JSON through WMI (read-only, visible, no network).
' Fallback for AudioDevices.exe:  cscript //nologo native\win\vbs\audio-devices.vbs
Option Explicit
Function Q(s)
  Dim r, i, c
  r = ""
  s = CStr(s)
  For i = 1 To Len(s)
    c = Mid(s, i, 1)
    If c = """" Or c = "\" Then
      r = r & "\" & c
    ElseIf AscW(c) < 32 Then
      r = r & " "
    Else
      r = r & c
    End If
  Next
  Q = """" & r & """"
End Function
Dim wmi, col, d, out, first
On Error Resume Next
Set wmi = GetObject("winmgmts:\\.\root\cimv2")
If Err.Number <> 0 Then
  WScript.Echo "{""ok"":false,""error"":""WMI unavailable""}"
  WScript.Quit 1
End If
Set col = wmi.ExecQuery("SELECT Name, Manufacturer, Status FROM Win32_SoundDevice")
out = "{""ok"":true,""devices"":["
first = True
For Each d In col
  If Not first Then out = out & ","
  out = out & "{""name"":" & Q(d.Name) & ",""vendor"":" & Q(d.Manufacturer) & ",""status"":" & Q(d.Status) & "}"
  first = False
Next
WScript.Echo out & "]}"
