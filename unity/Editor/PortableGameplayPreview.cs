using System;
using System.Diagnostics;
using System.IO;
using System.Text.RegularExpressions;

namespace Tanuki.Authoring
{
    /// <summary>Shared external Tanuki player protocol. No editor or game-engine dependency.</summary>
    public sealed class PortableGameplayPreview : IDisposable
    {
        public const string ReadyMarker="TANUKI_PREVIEW_READY";
        readonly Process process;
        public string DirectoryPath {get;}
        public string MapFile=>Path.Combine(DirectoryPath,"map.tanukimap");
        public string LogFile=>Path.Combine(DirectoryPath,"preview.log");
        public bool HasExited=>process==null||process.HasExited;
        public string Status
        {
            get
            {
                if(ReadLog(LogFile).Contains(ReadyMarker))return "Tanuki preview is running. Escape opens its menu.";
                string progress=ReadLog(Path.Combine(DirectoryPath,"setup-progress.json"));
                var phase=Regex.Match(progress,"\"phase\"\\s*:\\s*\"([^\"]+)\"");
                var bytes=Regex.Match(progress,"\"bytes\"\\s*:\\s*(\\d+)");var total=Regex.Match(progress,"\"total\"\\s*:\\s*(\\d+)");
                string value=phase.Success?phase.Groups[1].Value:"Preparing Tanuki preview";
                if(bytes.Success&&total.Success&&long.TryParse(total.Groups[1].Value,out long size)&&size>1&&long.TryParse(bytes.Groups[1].Value,out long done))value+=" ("+(done/1048576)+" / "+(size/1048576)+" MB)";
                return value;
            }
        }
        public static string ReadLog(string path)
        {
            if(!File.Exists(path))return "";
            using(var stream=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete))
            using(var reader=new StreamReader(stream))return reader.ReadToEnd();
        }
        static string Quote(string value){if(value.Contains("\""))throw new ArgumentException("Invalid quoted preview path.");return "\""+value+"\"";}
        public static string Resolve(string executable)
        {
            executable=Path.GetFullPath(executable);
            if(!File.Exists(executable)||Path.GetFileNameWithoutExtension(executable)!="TanukiMapPreview")throw new InvalidOperationException("No compatible Tanuki gameplay preview was found.");
            string descriptor=null;
            for(var parent=new DirectoryInfo(Path.GetDirectoryName(executable));parent!=null;parent=parent.Parent)
            {var candidate=Path.Combine(parent.FullName,"preview.json");if(File.Exists(candidate)){descriptor=candidate;break;}}
            var match=descriptor==null?Match.Empty:Regex.Match(File.ReadAllText(descriptor),"\"maxSchemaVersion\"\\s*:\\s*(\\d+)");
            if(!match.Success||!int.TryParse(match.Groups[1].Value,out int schema)||schema<5)throw new InvalidOperationException("Update the Tanuki gameplay preview player: this map requires schema 5.");
            string native=Path.Combine(Path.GetDirectoryName(executable),"Tanuki","Binaries","Win64","TanukiMapPreview.exe");return File.Exists(native)?native:executable;
        }
        static string Json(string value)
        {var result=new System.Text.StringBuilder("\"");foreach(char c in value??""){if(c=='\\'||c=='\"')result.Append('\\').Append(c);else if(c<32)result.Append("\\u").Append(((int)c).ToString("x4"));else result.Append(c);}return result.Append('"').ToString();}
        public PortableGameplayPreview(string executable,string parent,Action<string> export,string setupExecutable)
        {
            if(!File.Exists(setupExecutable))throw new IOException("The Tanuki preview setup helper is missing. Reinstall the current editor plugin package.");
            DirectoryPath=Path.Combine(parent,Guid.NewGuid().ToString("N"));Directory.CreateDirectory(DirectoryPath);
            try{export(MapFile);string request=Path.Combine(DirectoryPath,"setup-request.json");File.WriteAllText(request,"{\"requiredSchema\":5,\"candidateExecutable\":"+Json(executable)+",\"mapFile\":"+Json(MapFile)+",\"logFile\":"+Json(LogFile)+"}");process=Process.Start(new ProcessStartInfo{FileName=setupExecutable,Arguments=Quote(request),WorkingDirectory=Path.GetDirectoryName(setupExecutable),UseShellExecute=false,CreateNoWindow=true});if(process==null)throw new IOException("Tanuki preview setup did not start.");}
            catch{File.Delete(MapFile);if(Directory.Exists(DirectoryPath)&&Directory.GetFileSystemEntries(DirectoryPath).Length==0)Directory.Delete(DirectoryPath);throw;}
        }
        public string Complete()
        {
            if(!HasExited)throw new InvalidOperationException("Preview is still running.");
            bool ready=ReadLog(LogFile).Contains(ReadyMarker);
            bool success=process!=null&&process.ExitCode==0&&ready;
            File.Delete(MapFile);
            if(success){File.Delete(LogFile);foreach(var file in new[]{"setup-request.json","setup-result.json","setup-progress.json","cancel"})File.Delete(Path.Combine(DirectoryPath,file));if(Directory.GetFileSystemEntries(DirectoryPath).Length==0)Directory.Delete(DirectoryPath);}
            string result=ReadLog(Path.Combine(DirectoryPath,"setup-result.json"));
            if(!success&&!File.Exists(LogFile))File.WriteAllText(LogFile,result);
            process?.Dispose();return success?"Preview closed. Continue editing.":"Preview failed; diagnostics: "+LogFile;
        }
        public void Dispose(){if(!HasExited){File.WriteAllText(Path.Combine(DirectoryPath,"cancel"),"");if(!process.WaitForExit(5000)){process.Kill();process.WaitForExit();}}File.Delete(MapFile);process?.Dispose();}
    }
}
