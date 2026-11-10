' The launcher window: start / stop amixos-daemon.exe (next to this program), show the engine status read from its HTTP API on
' 127.0.0.1, open the Qt console (amixos.exe) if it is installed next to us, open the web remote. Nothing here touches the network
' beyond localhost. The daemon's --allow-lan flag is deliberately not offered: it serves unencrypted HTTP.
Imports System.Diagnostics
Imports System.Drawing
Imports System.IO
Imports System.Net.Http
Imports System.Windows.Forms

Public Class MainForm
    Inherits Form

    Private ReadOnly statusLabel As New Label()
    Private ReadOnly logBox As New TextBox()
    Private ReadOnly startButton As New Button()
    Private ReadOnly stopButton As New Button()
    Private ReadOnly consoleButton As New Button()
    Private ReadOnly remoteButton As New Button()
    Private ReadOnly poll As New Timer()
    Private daemon As Process
    Private ReadOnly http As New HttpClient()
    Private Const Port As Integer = 8765

    Public Sub New()
        Text = "A mix OS launcher"
        ClientSize = New Size(640, 420)
        BackColor = Color.FromArgb(15, 20, 28)
        ForeColor = Color.FromArgb(241, 245, 249)
        Font = New Font("Segoe UI", 9.0F)

        statusLabel.Text = "ENGINE: not running"
        statusLabel.Location = New Point(16, 16)
        statusLabel.AutoSize = True
        statusLabel.Font = New Font("Consolas", 11.0F, FontStyle.Bold)
        Controls.Add(statusLabel)

        startButton.Text = "START ENGINE"
        startButton.Location = New Point(16, 52)
        startButton.Size = New Size(140, 32)
        AddHandler startButton.Click, AddressOf StartDaemon
        Controls.Add(startButton)

        stopButton.Text = "STOP"
        stopButton.Location = New Point(164, 52)
        stopButton.Size = New Size(100, 32)
        AddHandler stopButton.Click, AddressOf StopDaemon
        Controls.Add(stopButton)

        consoleButton.Text = "OPEN CONSOLE"
        consoleButton.Location = New Point(272, 52)
        consoleButton.Size = New Size(140, 32)
        AddHandler consoleButton.Click, Sub() LaunchBeside("amixos.exe", "The console (amixos.exe) is not installed next to the launcher.")
        Controls.Add(consoleButton)

        remoteButton.Text = "WEB REMOTE"
        remoteButton.Location = New Point(420, 52)
        remoteButton.Size = New Size(120, 32)
        AddHandler remoteButton.Click, Sub() Process.Start(New ProcessStartInfo("http://127.0.0.1:" & Port & "/") With {.UseShellExecute = True})
        Controls.Add(remoteButton)

        logBox.Multiline = True
        logBox.ReadOnly = True
        logBox.ScrollBars = ScrollBars.Vertical
        logBox.Location = New Point(16, 100)
        logBox.Size = New Size(608, 304)
        logBox.BackColor = Color.FromArgb(5, 7, 10)
        logBox.ForeColor = Color.FromArgb(148, 163, 184)
        logBox.Font = New Font("Consolas", 9.0F)
        Controls.Add(logBox)

        poll.Interval = 1000
        AddHandler poll.Tick, AddressOf PollStatus
        poll.Start()
    End Sub

    Private Function Beside(ByVal name As String) As String
        Return Path.Combine(AppContext.BaseDirectory, name)
    End Function

    Private Sub Log(ByVal line As String)
        logBox.AppendText(DateTime.Now.ToString("HH:mm:ss") & "  " & line & Environment.NewLine)
    End Sub

    Private Sub StartDaemon(ByVal sender As Object, ByVal e As EventArgs)
        If daemon IsNot Nothing AndAlso Not daemon.HasExited Then
            Log("The engine is already running.")
            Return
        End If
        Dim exe As String = Beside("amixos-daemon.exe")
        If Not File.Exists(exe) Then
            Log("amixos-daemon.exe was not found next to the launcher: " & exe)
            Return
        End If
        Dim psi As New ProcessStartInfo(exe, "--port " & Port) With {.UseShellExecute = False, .CreateNoWindow = True, .RedirectStandardOutput = True, .RedirectStandardError = True}
        daemon = Process.Start(psi)
        AddHandler daemon.OutputDataReceived, Sub(s, a) If a.Data IsNot Nothing Then BeginInvoke(Sub() Log(a.Data))
        AddHandler daemon.ErrorDataReceived, Sub(s, a) If a.Data IsNot Nothing Then BeginInvoke(Sub() Log(a.Data))
        daemon.BeginOutputReadLine()
        daemon.BeginErrorReadLine()
        Log("Engine started (pid " & daemon.Id & ") on 127.0.0.1:" & Port)
    End Sub

    Private Sub StopDaemon(ByVal sender As Object, ByVal e As EventArgs)
        If daemon Is Nothing OrElse daemon.HasExited Then
            Log("The engine is not running.")
            Return
        End If
        daemon.Kill(True)
        daemon.WaitForExit(3000)
        Log("Engine stopped.")
    End Sub

    Private Sub LaunchBeside(ByVal name As String, ByVal missing As String)
        Dim exe As String = Beside(name)
        If File.Exists(exe) Then
            Process.Start(New ProcessStartInfo(exe) With {.UseShellExecute = True})
        Else
            Log(missing)
        End If
    End Sub

    Private Async Sub PollStatus(ByVal sender As Object, ByVal e As EventArgs)
        Dim running As Boolean = daemon IsNot Nothing AndAlso Not daemon.HasExited
        If Not running Then
            statusLabel.Text = "ENGINE: not running"
            statusLabel.ForeColor = Color.FromArgb(251, 191, 36)
            Return
        End If
        Try
            Dim body As String = Await http.GetStringAsync("http://127.0.0.1:" & Port & "/api/status")
            statusLabel.Text = "ENGINE: running  " & If(body.Length > 80, body.Substring(0, 80) & "...", body)
            statusLabel.ForeColor = Color.FromArgb(16, 185, 129)
        Catch ex As Exception
            statusLabel.Text = "ENGINE: starting..."
            statusLabel.ForeColor = Color.FromArgb(251, 191, 36)
        End Try
    End Sub

    Protected Overrides Sub OnFormClosing(ByVal e As FormClosingEventArgs)
        If daemon IsNot Nothing AndAlso Not daemon.HasExited Then daemon.Kill(True)
        MyBase.OnFormClosing(e)
    End Sub
End Class
