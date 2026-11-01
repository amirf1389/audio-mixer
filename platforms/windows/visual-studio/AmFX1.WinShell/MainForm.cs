using System.Diagnostics;
using System.Drawing;
using System.Windows.Forms;
using Microsoft.Web.WebView2.WinForms;

namespace AmFX1;

public sealed class MainForm : Form
{
    private readonly WebView2 web = new() { Dock = DockStyle.Fill };
    private readonly Label status = new() { Dock = DockStyle.Bottom, Height = 28, TextAlign = ContentAlignment.MiddleLeft, Padding = new Padding(8, 0, 0, 0) };
    private Process? engine;
    private int retries;

    public MainForm()
    {
        Text = $"{AppSettings.ProductName} · {AppSettings.VersionLabel}";
        Width = 1280;
        Height = 800;
        MinimumSize = new Size(900, 600);
        Controls.Add(web);
        Controls.Add(status);
        Load += async (_, _) => await StartAsync();
        web.NavigationCompleted += (_, e) => OnNavigationCompleted(e.IsSuccess);
        FormClosed += (_, _) => StopEngine();
    }

    private async Task StartAsync()
    {
        var root = AppSettings.FindRepoRoot();
        if (root is null)
        {
            status.Text = "Could not find bridge\\server.js. Keep the repository folders together.";
            return;
        }

        try
        {
            engine = BridgeProcess.Start(root);
            status.Text = "Starting the mixer engine...";
        }
        catch (Exception ex)
        {
            status.Text = "Node.js is needed to start the engine (install it from nodejs.org). " + ex.Message;
            return;
        }

        try
        {
            await web.EnsureCoreWebView2Async();
        }
        catch (Exception ex)
        {
            status.Text = "The WebView2 runtime is missing: install it from microsoft.com/webview2. " + ex.Message;
            return;
        }

        web.Source = new Uri(AppSettings.BridgeUrl);
    }

    // The engine needs a moment to listen. Retry a few times before showing an error.
    private async void OnNavigationCompleted(bool success)
    {
        if (success)
        {
            status.Text = $"Ready: {AppSettings.BridgeUrl}";
            retries = 0;
            return;
        }
        if (retries++ >= 10)
        {
            status.Text = "The engine did not answer. Check that Node.js can run bridge\\server.js.";
            return;
        }
        status.Text = "Waiting for the engine...";
        await Task.Delay(1000);
        web.Source = new Uri(AppSettings.BridgeUrl);
    }

    private void StopEngine()
    {
        try { engine?.Kill(true); } catch (InvalidOperationException) { /* already stopped */ }
        engine?.Dispose();
    }
}
