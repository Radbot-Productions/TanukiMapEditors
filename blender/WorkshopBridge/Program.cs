using System.Text.Json;
using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;

if(args.Length!=1)throw new ArgumentException("Supply one request JSON path.");
string job=Path.GetDirectoryName(Path.GetFullPath(args[0]))!;
void Write(object result)
{
    string temporary=Path.Combine(job,"result.tmp");File.WriteAllText(temporary,JsonSerializer.Serialize(result));File.Move(temporary,Path.Combine(job,"result.json"),true);
}
ulong itemId=0;
try
{
    using var request=JsonDocument.Parse(File.ReadAllText(args[0]));var r=request.RootElement;
    using var steam=new PortableWorkshopClient(Path.Combine(AppContext.BaseDirectory,"tanuki_steam_api64.dll"),job);
    steam.Connect();
    string action=r.GetProperty("action").GetString()!;
    Task work=action switch
    {
        "connect"=>Task.CompletedTask,
        "refresh"=>steam.Refresh(),
        "download"=>Download(),
        "publish"=>Publish(),
        _=>throw new ArgumentException("Unsupported Workshop action.")
    };
    while(!work.IsCompleted){steam.Poll();Thread.Sleep(20);}
    work.GetAwaiter().GetResult();
    if(action=="refresh")Write(new{success=true,account=steam.Account,items=steam.Items});
    if(action=="connect")Write(new{success=true,account=steam.Account,connected=true});
    async Task Download()
    {
        var result=await steam.Download(ulong.Parse(r.GetProperty("id").GetString()!));
        Write(new{success=true,file=result.File,owned=result.Item.Owned,id=result.Item.Id.ToString(),title=result.Item.Title});
    }
    async Task Publish()
    {
        ulong existing=ulong.Parse(r.GetProperty("id").GetString()!);
        itemId=await steam.Publish(r.GetProperty("file").GetString()!,r.GetProperty("title").GetString()!,r.GetProperty("preview").GetString()!,existing,
            id=>{itemId=id;Write(new{pending=true,id=id.ToString(),message="Upload pending. Retain this item ID if the editor closes."});});
        Write(new{success=true,id=itemId.ToString()});
    }
}
catch(Exception error){Write(new{success=false,id=itemId.ToString(),error=error.Message});Environment.ExitCode=1;}
