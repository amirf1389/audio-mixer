namespace AmFX1;

// EDIT HERE: the name, the address of the mixer page, and how the repository is found.
internal static class AppSettings
{
    public const string ProductName = "AmF X1 by AMFB Audio";
    public const string VersionLabel = "0.1 Alpha";
    public const string BridgeUrl = "http://localhost:8765/";

    // Walks up from the built app until it finds bridge\server.js (the repository root). Works from any folder inside the repository.
    public static string? FindRepoRoot()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir is not null)
        {
            if (File.Exists(Path.Combine(dir.FullName, "bridge", "server.js"))) return dir.FullName;
            dir = dir.Parent;
        }
        return null;
    }
}
