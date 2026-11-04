using System.Diagnostics;
using System.Text.Json;
using TanukiMapSetup;

public static class PreviewSession
{
    public const string ManifestUrl="https://github.com/Radbot-Productions/TanukiMapEditors/releases/download/player-v0.2.0-preview.3/preview-player.json";
    public static string? Resolve(string? executable,int requiredSchema)
    {
        if(string.IsNullOrWhiteSpace(executable))return null;
        try
        {
            executable=Path.GetFullPath(executable);
            if(!File.Exists(executable)||!Path.GetFileName(executable).Equals("TanukiMapPreview.exe",StringComparison.OrdinalIgnoreCase))return null;
            for(var parent=new DirectoryInfo(Path.GetDirectoryName(executable)!);parent!=null;parent=parent.Parent)
            {
                string descriptor=Path.Combine(parent.FullName,"preview.json");if(!File.Exists(descriptor))continue;
                using var json=JsonDocument.Parse(File.ReadAllText(descriptor));
                if(json.RootElement.GetProperty("protocol").GetInt32()!=1||json.RootElement.GetProperty("maxSchemaVersion").GetInt32()<requiredSchema)return null;
                string native=Path.Combine(parent.FullName,"Tanuki","Binaries","Win64","TanukiMapPreview.exe");return File.Exists(native)?native:executable;
            }
        }
        catch(Exception error)when(error is IOException or JsonException or KeyNotFoundException or ArgumentException or UnauthorizedAccessException){return null;}
        return null;
    }
    public static string? Discover(SetupRequest request)
    {
        foreach(string? candidate in new[]{Environment.GetEnvironmentVariable("TANUKI_GAMEPLAY_PREVIEW_EXECUTABLE"),request.CandidateExecutable})
            if(Resolve(candidate,request.RequiredSchema) is string ready)return ready;
        string cache=string.IsNullOrEmpty(request.CacheRoot)?PreviewInstaller.DefaultCache:Path.GetFullPath(request.CacheRoot);
        try
        {
            using var pointer=JsonDocument.Parse(File.ReadAllText(Path.Combine(cache,"current.json")));
            string directory=pointer.RootElement.GetProperty("directory").GetString()!;
            SetupCore.CheckParents(directory);
            if(!Path.GetFullPath(directory).StartsWith(Path.GetFullPath(Path.Combine(cache,"versions"))+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase))return null;
            using var receipt=JsonDocument.Parse(File.ReadAllText(Path.Combine(directory,"tanuki-preview-install.json")));
            if(receipt.RootElement.GetProperty("product").GetString()!="TanukiMapPreview")return null;
            foreach(var file in receipt.RootElement.GetProperty("files").EnumerateArray())
            {string path=SetupCore.Child(directory,file.GetProperty("path").GetString()!);if(!File.Exists(path)||new FileInfo(path).Length!=file.GetProperty("bytes").GetInt64())return null;}
            return Resolve(pointer.RootElement.GetProperty("executable").GetString(),request.RequiredSchema);
        }
        catch(Exception error)when(error is IOException or JsonException or KeyNotFoundException or InvalidOperationException or ArgumentException or UnauthorizedAccessException){return null;}
    }
    public static async Task<string> Run(SetupRequest request,Action<string,long,long> progress,CancellationToken cancel)
    {
        if(!File.Exists(request.MapFile)||string.IsNullOrEmpty(request.LogFile))throw new InvalidDataException("The editor snapshot or preview log path is missing.");
        request=request with{ManifestUrl=string.IsNullOrEmpty(request.ManifestUrl)?ManifestUrl:request.ManifestUrl};
        string executable=(!request.Force?Discover(request):null)??await PreviewInstaller.Ensure(request,progress,cancel);
        for(int attempt=0;attempt<2;++attempt)
        {
            progress(attempt==0?"Starting Tanuki preview":"Starting repaired Tanuki preview",0,0);
            File.Delete(request.LogFile);
            var start=new ProcessStartInfo(executable){UseShellExecute=false,WorkingDirectory=Path.GetDirectoryName(executable)};
            foreach(string argument in new[]{"-CustomConfig=MapPreview","-TanukiPreviewMap="+Path.GetFullPath(request.MapFile),"-abslog="+Path.GetFullPath(request.LogFile),"-windowed","-ResX=1280","-ResY=720"})start.ArgumentList.Add(argument);
            string userDirectory=string.IsNullOrEmpty(request.UserDirectory)?Path.Combine(string.IsNullOrEmpty(request.CacheRoot)?PreviewInstaller.DefaultCache:request.CacheRoot,"userdata"):request.UserDirectory;
            start.ArgumentList.Add("-UserDir="+Path.GetFullPath(userDirectory));
            using var process=Process.Start(start)??throw new IOException("Could not start the Tanuki preview.");
            try{await process.WaitForExitAsync(cancel);}
            finally{if(!process.HasExited){process.Kill(true);await process.WaitForExitAsync();}}
            string log="";if(File.Exists(request.LogFile)){using var stream=new FileStream(request.LogFile,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete);using var reader=new StreamReader(stream);log=await reader.ReadToEndAsync(cancel);}
            if(process.ExitCode==0)return executable;
            if(attempt==0&&!log.Contains("TANUKI_PREVIEW_READY"))
            {progress("Repairing preview after a startup failure",0,0);executable=await PreviewInstaller.Ensure(request with{Force=true},progress,cancel);continue;}
            throw new IOException("The Tanuki preview exited with code "+process.ExitCode+". Diagnostics: "+request.LogFile);
        }
        throw new IOException("The repaired Tanuki preview could not start.");
    }
}
