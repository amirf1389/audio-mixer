using System.Diagnostics;

namespace AmFX1;

// Starts the mixer engine: node bridge/server.js, in the background, with no console window.
internal static class BridgeProcess
{
    public static Process Start(string repoRoot)
    {
        var server = Path.Combine(repoRoot, "bridge", "server.js");
        var info = new ProcessStartInfo("node", $"\"{server}\"")
        {
            WorkingDirectory = repoRoot,
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        return Process.Start(info) ?? throw new InvalidOperationException("The engine did not start.");
    }
}
