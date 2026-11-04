using System.IO.Compression;
using System.Security.Cryptography;
using System.Text.Json;
using TanukiMapSetup;

if(args.Length!=1)throw new ArgumentException("Supply one preview setup request JSON path.");
var requestPath=Path.GetFullPath(args[0]);var job=Path.GetDirectoryName(requestPath)!;
var json=new JsonSerializerOptions{PropertyNameCaseInsensitive=true,PropertyNamingPolicy=JsonNamingPolicy.CamelCase,WriteIndented=true};
void Write(string name,object value)
{
    string path=Path.Combine(job,name);File.WriteAllText(path+".tmp",JsonSerializer.Serialize(value,json));
    for(int attempt=0;;++attempt)
    {
        try{File.Move(path+".tmp",path,true);return;}
        catch(Exception error)when((error is IOException or UnauthorizedAccessException)&&attempt<20){Thread.Sleep(25);}
    }
}
try
{
    var request=JsonSerializer.Deserialize<SetupRequest>(File.ReadAllText(requestPath),json)??throw new InvalidDataException("Missing setup request.");
    using var cancel=new CancellationTokenSource();
    using var watch=new Timer(_=>{if(File.Exists(Path.Combine(job,"cancel")))cancel.Cancel();},null,500,500);
    var progressClock=System.Diagnostics.Stopwatch.StartNew();string previousPhase="";
    void Progress(string phase,long bytes,long total)
    {
        if(phase==previousPhase&&progressClock.ElapsedMilliseconds<250&&bytes!=total)return;
        previousPhase=phase;progressClock.Restart();
        // Progress is observational. A reader holding the old progress file
        // must not turn a valid download into an installation failure.
        try{Write("setup-progress.json",new{phase,bytes,total});}
        catch(Exception error)when(error is IOException or UnauthorizedAccessException){}
    }
    string executable=string.IsNullOrEmpty(request.MapFile)
        ?await PreviewInstaller.Ensure(request,Progress,cancel.Token)
        :await PreviewSession.Run(request,Progress,cancel.Token);
    Write("setup-result.json",new{success=true,executable});
}
catch(Exception error){Write("setup-result.json",new{success=false,error=error.Message,diagnostic=error.ToString()});Environment.ExitCode=1;}

public sealed record SetupRequest(string ManifestUrl="",string ManifestFile="",string CacheRoot="",int RequiredSchema=5,bool Force=false,string CandidateExecutable="",string MapFile="",string LogFile="",string UserDirectory="");
public sealed record PlayerPart(string Name,string Url,string Sha256,long Bytes);
public sealed record PlayerFile(string Path,long Bytes);
public sealed record PlayerManifest(int Protocol,string Version,int MaxSchemaVersion,string ArchiveSha256,long ArchiveBytes,long UncompressedBytes,string EntryPoint,PlayerPart[] Parts,PlayerFile[] Files);
public static class PreviewInstaller
{
    static readonly JsonSerializerOptions Json=new(){PropertyNameCaseInsensitive=true,PropertyNamingPolicy=JsonNamingPolicy.CamelCase};
    public static string DefaultCache=>Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"RadbotProductions","TanukiMapPreview");
    static void Parents(string path)=>SetupCore.CheckParents(path);
    static void CleanOwned(string directory,string token)
    {
        Parents(directory);
        if(File.ReadAllText(Path.Combine(directory,".tanuki-preview-owner"))!=token)throw new IOException("Preview setup ownership changed; preserving staging data.");
        if(Directory.EnumerateFileSystemEntries(directory,"*",SearchOption.AllDirectories).Any(p=>(File.GetAttributes(p)&FileAttributes.ReparsePoint)!=0))throw new IOException("Preview setup refuses a link in its staging data.");
        Directory.Delete(directory,true);
    }
    static string? Existing(string cache,PlayerManifest manifest)
    {
        try
        {
        string pointer=Path.Combine(cache,"current.json");if(!File.Exists(pointer))return null;
        using var value=JsonDocument.Parse(File.ReadAllText(pointer));
        if(value.RootElement.GetProperty("version").GetString()!=manifest.Version||value.RootElement.GetProperty("archiveSha256").GetString()!=manifest.ArchiveSha256)return null;
        string root=value.RootElement.GetProperty("directory").GetString()!;Parents(root);
        if(!Path.GetFullPath(root).StartsWith(Path.GetFullPath(Path.Combine(cache,"versions"))+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase))return null;
        foreach(var file in manifest.Files){string path=SetupCore.Child(root,file.Path);if(!File.Exists(path)||new FileInfo(path).Length!=file.Bytes)return null;}
        return SetupCore.Child(root,manifest.EntryPoint);
        }
        catch(Exception error)when(error is IOException or JsonException or KeyNotFoundException or InvalidOperationException or ArgumentException or UnauthorizedAccessException){return null;}
    }
    static void RetireOldVersions(string cache,string current)
    {
        var active=new List<string>();bool unknown=false;
        foreach(var process in System.Diagnostics.Process.GetProcessesByName("TanukiMapPreview"))
        {try{if(process.MainModule?.FileName is string path)active.Add(Path.GetFullPath(path));else unknown=true;}catch{unknown=true;}finally{process.Dispose();}}
        if(unknown)return;
        foreach(string directory in Directory.EnumerateDirectories(Path.Combine(cache,"versions")))
        {
            if(directory==current||active.Any(path=>path.StartsWith(Path.GetFullPath(directory)+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase)))continue;
            string receipt=Path.Combine(directory,"tanuki-preview-install.json");if(!File.Exists(receipt))continue;
            try
            {
                using var json=JsonDocument.Parse(File.ReadAllText(receipt));if(json.RootElement.GetProperty("product").GetString()!="TanukiMapPreview")continue;
                Parents(directory);if(Directory.EnumerateFileSystemEntries(directory,"*",SearchOption.AllDirectories).Any(p=>(File.GetAttributes(p)&FileAttributes.ReparsePoint)!=0))continue;
                Directory.Delete(directory,true);
            }
            catch(Exception error)when(error is IOException or JsonException or KeyNotFoundException or UnauthorizedAccessException){/* Preserve locked or unrecognized installations. */}
        }
    }
    public static async Task<string> Ensure(SetupRequest request,Action<string,long,long> status,CancellationToken cancel)
    {
        string cache=Path.GetFullPath(string.IsNullOrWhiteSpace(request.CacheRoot)?DefaultCache:request.CacheRoot);Parents(cache);Directory.CreateDirectory(cache);
        FileStream? gate=null;
        while(gate==null){cancel.ThrowIfCancellationRequested();try{gate=new FileStream(Path.Combine(cache,"install.lock"),FileMode.OpenOrCreate,FileAccess.ReadWrite,FileShare.None);}catch(IOException){status("Waiting for another preview installation",0,0);await Task.Delay(500,cancel);}}
        await using var installGate=gate;
        string stages=Path.Combine(cache,"staging");if(Directory.Exists(stages))foreach(string previous in Directory.EnumerateDirectories(stages))
        {string marker=Path.Combine(previous,".tanuki-preview-owner");if(File.Exists(marker)&&Path.GetFileName(previous)==File.ReadAllText(marker))CleanOwned(previous,File.ReadAllText(marker));}
        using var http=new HttpClient{Timeout=Timeout.InfiniteTimeSpan};http.DefaultRequestHeaders.UserAgent.ParseAdd("TanukiMapEditors/0.2");
        status("Checking preview version",0,0);
        string manifestText;
        if(!string.IsNullOrEmpty(request.ManifestFile))manifestText=File.ReadAllText(Path.GetFullPath(request.ManifestFile));
        else
        {
            var uri=new Uri(request.ManifestUrl);if(uri.Scheme!="https")throw new InvalidDataException("Preview updates require HTTPS.");
            manifestText=await http.GetStringAsync(uri,cancel);
        }
        if(manifestText.Length>4*1024*1024)throw new InvalidDataException("Preview manifest is too large.");
        var manifest=JsonSerializer.Deserialize<PlayerManifest>(manifestText,Json)??throw new InvalidDataException("Missing player manifest.");
        if(manifest.Parts==null||manifest.Files==null||string.IsNullOrEmpty(manifest.Version)||manifest.ArchiveSha256==null||manifest.Protocol!=1||manifest.MaxSchemaVersion<request.RequiredSchema||manifest.Parts.Length is <1 or >32||manifest.Files.Length is <1 or >100000||manifest.ArchiveBytes<=0||manifest.UncompressedBytes<=0||manifest.Version.Length>80||manifest.Version.Any(c=>!char.IsAsciiLetterOrDigit(c)&&c is not '.' and not '-'))throw new InvalidDataException("Unsupported preview manifest.");
        _=SetupCore.Child(cache,manifest.EntryPoint);
        if(manifest.ArchiveSha256.Length!=64||!manifest.ArchiveSha256.All(Uri.IsHexDigit)||manifest.Parts.Any(p=>p.Bytes<=0||p.Sha256.Length!=64||!p.Sha256.All(Uri.IsHexDigit))||manifest.Parts.Sum(p=>p.Bytes)!=manifest.ArchiveBytes)throw new InvalidDataException("Invalid preview archive checksums/sizes.");
        var ready=request.Force?null:Existing(cache,manifest);if(ready!=null){status("Preview ready",1,1);return ready;}
        string token=Guid.NewGuid().ToString("N"),stage=Path.Combine(cache,"staging",token);Directory.CreateDirectory(stage);File.WriteAllText(Path.Combine(stage,".tanuki-preview-owner"),token);
        try
        {
            string archive=Path.Combine(stage,"player.zip");long completed=0;
            await using(var output=new FileStream(archive,FileMode.CreateNew,FileAccess.Write))
            {
                byte[] buffer=new byte[1024*1024];
                foreach(var part in manifest.Parts)
                {
                    cancel.ThrowIfCancellationRequested();using var digest=IncrementalHash.CreateHash(HashAlgorithmName.SHA256);long received=0;
                    Stream source;HttpResponseMessage? response=null;
                    if(!string.IsNullOrEmpty(request.ManifestFile))source=File.OpenRead(SetupCore.Child(Path.GetDirectoryName(Path.GetFullPath(request.ManifestFile))!,part.Name));
                    else
                    {
                        var uri=new Uri(part.Url);if(uri.Scheme!="https")throw new InvalidDataException("Preview downloads require HTTPS.");
                        response=await http.GetAsync(uri,HttpCompletionOption.ResponseHeadersRead,cancel);response.EnsureSuccessStatusCode();
                        if(response.RequestMessage?.RequestUri?.Scheme!="https")throw new InvalidDataException("Insecure download redirect.");source=await response.Content.ReadAsStreamAsync(cancel);
                    }
                    try
                    {
                        await using(source)
                        {int count;while((count=await source.ReadAsync(buffer,cancel))>0){received+=count;if(received>part.Bytes)throw new InvalidDataException("Preview download exceeds its declared size.");digest.AppendData(buffer,0,count);await output.WriteAsync(buffer.AsMemory(0,count),cancel);completed+=count;status("Downloading preview",completed,manifest.ArchiveBytes);}}
                    }
                    finally{response?.Dispose();}
                    if(received!=part.Bytes||!Convert.ToHexString(digest.GetHashAndReset()).Equals(part.Sha256,StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("Preview download checksum mismatch. Play again to retry installation.");
                }
            }
            status("Verifying preview",0,0);
            await using(var stream=File.OpenRead(archive))if(!Convert.ToHexString(await SHA256.HashDataAsync(stream,cancel)).Equals(manifest.ArchiveSha256,StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("Preview archive checksum mismatch.");
            string content=Path.Combine(stage,"content");Directory.CreateDirectory(content);long extracted=0;
            using(var zip=ZipFile.OpenRead(archive))
            {
                if(zip.Entries.Count>100000)throw new InvalidDataException("Too many preview files.");var names=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                foreach(var entry in zip.Entries)
                {
                    cancel.ThrowIfCancellationRequested();if(((entry.ExternalAttributes>>16)&0xf000)==0xa000)throw new InvalidDataException("Preview archive contains a symbolic link.");
                    string destination=SetupCore.Child(content,entry.FullName);if(!names.Add(destination))throw new InvalidDataException("Duplicate preview archive path.");
                    extracted+=entry.Length;if(extracted>manifest.UncompressedBytes)throw new InvalidDataException("Preview archive exceeds its declared extracted size.");
                    if(entry.FullName.EndsWith('/')||entry.FullName.EndsWith('\\'))Directory.CreateDirectory(destination);
                    else{Directory.CreateDirectory(Path.GetDirectoryName(destination)!);await using var input=entry.Open();await using var output=new FileStream(destination,FileMode.CreateNew);await input.CopyToAsync(output,cancel);}
                    status("Installing preview",extracted,manifest.UncompressedBytes);
                }
            }
            foreach(var file in manifest.Files){string path=SetupCore.Child(content,file.Path);if(!File.Exists(path)||new FileInfo(path).Length!=file.Bytes)throw new InvalidDataException("Preview archive file inventory mismatch.");}
            string executable=SetupCore.Child(content,manifest.EntryPoint);if(!File.Exists(executable))throw new InvalidDataException("Preview executable is missing.");
            using(var descriptor=JsonDocument.Parse(File.ReadAllText(Path.Combine(Path.GetDirectoryName(executable)!,"preview.json"))))
                if(descriptor.RootElement.GetProperty("maxSchemaVersion").GetInt32()<request.RequiredSchema)throw new InvalidDataException("Downloaded preview is too old for these tools.");
            string versions=Path.Combine(cache,"versions");Directory.CreateDirectory(versions);string installed=Path.Combine(versions,manifest.Version+"-"+token[..8]);
            File.WriteAllText(Path.Combine(content,"tanuki-preview-install.json"),JsonSerializer.Serialize(new{product="TanukiMapPreview",version=manifest.Version,archiveSha256=manifest.ArchiveSha256,files=manifest.Files},Json));
            Directory.Move(content,installed);executable=SetupCore.Child(installed,manifest.EntryPoint);
            string pointer=Path.Combine(cache,"current.json");File.WriteAllText(pointer+".tmp",JsonSerializer.Serialize(new{directory=installed,executable,version=manifest.Version,archiveSha256=manifest.ArchiveSha256,maxSchemaVersion=manifest.MaxSchemaVersion},Json));File.Move(pointer+".tmp",pointer,true);
            RetireOldVersions(cache,installed);
            status("Preview ready",1,1);return executable;
        }
        finally{CleanOwned(stage,token);}
    }
}
