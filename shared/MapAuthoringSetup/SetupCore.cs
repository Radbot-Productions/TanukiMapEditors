using System.Diagnostics;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text.Json;

namespace TanukiMapSetup;

public sealed record Kit(string Editor, string Version, string Archive, string Sha256, string EntryPoint)
{
    public override string ToString() => $"{Editor} — {Version}";
}
public sealed record Release(int Schema, string Version, Kit[] Kits);

public static class SetupCore
{
    public static Release ReadRelease(string path)
    {
        var release = JsonSerializer.Deserialize<Release>(File.ReadAllText(path), new JsonSerializerOptions { PropertyNameCaseInsensitive = true })
            ?? throw new InvalidDataException("The toolkit download list is empty.");
        if (release.Schema != 1 || release.Kits.Length == 0 || string.IsNullOrWhiteSpace(release.Version))
            throw new InvalidDataException("The toolkit download list is not supported.");
        foreach (var kit in release.Kits)
        {
            if (kit.Editor is not ("Unity" or "Godot" or "Unreal") || kit.Sha256.Length != 64 || !kit.Sha256.All(Uri.IsHexDigit))
                throw new InvalidDataException("The toolkit download list contains an invalid package.");
            if (string.IsNullOrWhiteSpace(kit.Version) || string.IsNullOrWhiteSpace(kit.Archive))
                throw new InvalidDataException("A toolkit version or archive is missing.");
            _ = Child(Path.GetTempPath(), kit.EntryPoint);
        }
        if (release.Kits.Select(x => x.Editor).Distinct().Count() != release.Kits.Length)
            throw new InvalidDataException("The toolkit download list repeats an editor.");
        return release;
    }

    public static string Child(string root, string relative)
    {
        if (string.IsNullOrWhiteSpace(relative) || Path.IsPathRooted(relative) || relative.Contains(':'))
            throw new InvalidDataException("A toolkit path is invalid.");
        var prefix = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        var result = Path.GetFullPath(Path.Combine(prefix, relative.Replace('/', Path.DirectorySeparatorChar).Replace('\\', Path.DirectorySeparatorChar)));
        if (!result.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("A toolkit path escapes its folder.");
        return result;
    }

    public static void CheckParents(string path)
    {
        for (var info = new DirectoryInfo(Path.GetFullPath(path)); info != null; info = info.Parent)
            if (info.Exists && (info.Attributes & FileAttributes.ReparsePoint) != 0)
                throw new IOException("Choose a folder without directory links or junctions.");
    }

    public static async Task<string> Install(Kit kit, string manifestDirectory, string destination, Action<string> status, CancellationToken cancel = default)
    {
        destination = Path.GetFullPath(destination);
        CheckParents(destination);
        if (Directory.Exists(destination) || File.Exists(destination)) throw new IOException("That folder already exists. Choose a new project folder.");
        var parent = Path.GetDirectoryName(destination) ?? throw new IOException("Choose a project folder.");
        Directory.CreateDirectory(parent);
        var stage = Path.Combine(parent, ".tanuki-setup-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(stage);
        var marker = Path.Combine(stage, ".tanuki-owner");
        var token = Guid.NewGuid().ToString("N");
        File.WriteAllText(marker, token);
        try
        {
            status("Downloading toolkit…");
            var archive = Path.Combine(stage, "download.zip");
            if (Uri.TryCreate(kit.Archive, UriKind.Absolute, out var uri))
            {
                if (uri.Scheme != Uri.UriSchemeHttps) throw new InvalidDataException("Toolkit downloads must use HTTPS.");
                using var http = new HttpClient();
                using var response = await http.GetAsync(uri, HttpCompletionOption.ResponseHeadersRead, cancel);
                response.EnsureSuccessStatusCode();
                if (response.RequestMessage?.RequestUri?.Scheme != Uri.UriSchemeHttps) throw new InvalidDataException("The download redirected away from HTTPS.");
                await using var output = new FileStream(archive, FileMode.CreateNew);
                await response.Content.CopyToAsync(output, cancel);
            }
            else File.Copy(Child(manifestDirectory, kit.Archive), archive);
            status("Checking download…");
            await using (var input = File.OpenRead(archive))
            {
                var hash = Convert.ToHexString(await SHA256.HashDataAsync(input, cancel));
                if (!hash.Equals(kit.Sha256, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("The toolkit checksum does not match. Download the release again.");
            }
            status("Creating your project…");
            var project = Path.Combine(stage, "project");
            Directory.CreateDirectory(project);
            using (var zip = ZipFile.OpenRead(archive))
            {
                var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                foreach (var entry in zip.Entries)
                {
                    cancel.ThrowIfCancellationRequested();
                    if (((entry.ExternalAttributes >> 16) & 0xf000) == 0xa000) throw new InvalidDataException("The toolkit contains a symbolic link.");
                    var target = Child(project, entry.FullName);
                    if (!names.Add(target)) throw new InvalidDataException("The toolkit contains duplicate paths.");
                    if (entry.FullName.EndsWith('/') || entry.FullName.EndsWith('\\')) Directory.CreateDirectory(target);
                    else
                    {
                        Directory.CreateDirectory(Path.GetDirectoryName(target)!);
                        await using var input = entry.Open();
                        await using var output = new FileStream(target, FileMode.CreateNew);
                        await input.CopyToAsync(output, cancel);
                    }
                }
            }
            if (!File.Exists(Child(project, kit.EntryPoint))) throw new InvalidDataException("The toolkit is missing its project file.");
            File.WriteAllText(Path.Combine(project, "tanuki-kit-receipt.json"), JsonSerializer.Serialize(new { editor = kit.Editor, version = kit.Version, sha256 = kit.Sha256 }));
            Directory.Move(project, destination);
            status("Project ready.");
            return destination;
        }
        finally
        {
            // Only this invocation's staging directory is disposable. Never remove an installed project.
            CheckParents(stage);
            if (File.ReadAllText(marker) != token) throw new IOException("The setup staging ownership marker changed; cleanup stopped.");
            Directory.Delete(stage, true);
        }
    }

    public static void Open(Kit kit, string project, string editor)
    {
        if (!File.Exists(editor)) throw new FileNotFoundException("Choose your installed editor executable.", editor);
        var start = new ProcessStartInfo(editor) { UseShellExecute = false, WorkingDirectory = project };
        if (kit.Editor == "Unity") { start.ArgumentList.Add("-projectPath"); start.ArgumentList.Add(project); }
        else if (kit.Editor == "Godot") { start.ArgumentList.Add("--editor"); start.ArgumentList.Add("--path"); start.ArgumentList.Add(project); }
        else start.ArgumentList.Add(Child(project, kit.EntryPoint));
        _ = Process.Start(start) ?? throw new IOException("The editor did not start.");
    }
}
