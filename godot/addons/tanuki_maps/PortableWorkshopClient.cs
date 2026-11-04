using Radbot.MapAuthoring.Steamworks;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Environment=System.Environment;
using System.Runtime.InteropServices;
using System.Threading;

public class PortableWorkshopClient : IDisposable
{
    readonly string stateDirectory;
    static bool resolverInstalled;
    public PortableWorkshopClient(string nativeLibrary,string stateDirectory)
    {
        this.stateDirectory=Path.GetFullPath(stateDirectory);
        if(!resolverInstalled)
        {
            NativeLibrary.SetDllImportResolver(typeof(SteamAPI).Assembly,(name,assembly,path)=>name=="tanuki_steam_api64"?NativeLibrary.Load(nativeLibrary):IntPtr.Zero);
            resolverInstalled=true;
        }
    }
    public static string Hash(string file)=>Convert.ToHexString(System.Security.Cryptography.SHA1.HashData(File.ReadAllBytes(file))).ToLowerInvariant();
    public const uint App=5128560;
    public record Item(ulong Id,string Title,bool Owned,string Hash);
    public List<Item> Items {get;private set;}=new();
    bool connected;
    ulong owner;
    readonly List<IDisposable> calls=new();
    readonly CancellationTokenSource lifetime=new();
    public string Account=>connected?SteamFriends.GetPersonaName():"Steam: not connected";
    public bool Busy {get;private set;}
    public void Connect()
    {
        if(connected)return;
        string old=Environment.GetEnvironmentVariable("SteamAppId"),oldGame=Environment.GetEnvironmentVariable("SteamGameId");
        try{Environment.SetEnvironmentVariable("SteamAppId",App.ToString());Environment.SetEnvironmentVariable("SteamGameId",App.ToString());if(!SteamAPI.Init())throw new InvalidOperationException("Open Steam and sign in to an account with Tanuki Tag access.");connected=true;}
        finally{Environment.SetEnvironmentVariable("SteamAppId",old);Environment.SetEnvironmentVariable("SteamGameId",oldGame);}
        if(SteamUtils.GetAppID().m_AppId!=App || !SteamUser.BLoggedOn()){Dispose();throw new InvalidOperationException("Steam is not connected to Tanuki Tag.");}
        owner=SteamUser.GetSteamID().m_SteamID;
    }
    public void Poll(){if(connected)SteamAPI.RunCallbacks();}
    async Task<T> Wait<T>(SteamAPICall_t call) where T:struct
    {
        if(call==SteamAPICall_t.Invalid)throw new InvalidOperationException("Steam refused the operation.");
        var completion=new TaskCompletionSource<T>();var result=CallResult<T>.Create((value,failed)=>{if(failed)completion.TrySetException(new IOException("Steam connection failed. Check the recorded item before retrying."));else completion.TrySetResult(value);});
        calls.Add(result);result.Set(call);
        try{return await completion.Task.WaitAsync(TimeSpan.FromSeconds(120),lifetime.Token);}finally{result.Dispose();calls.Remove(result);}
    }
    void Check(bool valid,string message){if(!valid)throw new InvalidOperationException(message);}
    async Task<List<Item>> Query(UGCQueryHandle_t query)
    {
        try
        {
            Check(SteamUGC.SetReturnMetadata(query,true),"Steam refused map metadata.");
            var r=await Wait<SteamUGCQueryCompleted_t>(SteamUGC.SendQueryUGCRequest(query));Check(r.m_eResult==EResult.k_EResultOK,"Steam query failed: "+r.m_eResult);
            var list=new List<Item>();
            for(uint i=0;i<r.m_unNumResultsReturned;i++)
                if(SteamUGC.GetQueryUGCResult(query,i,out var item) && item.m_eResult==EResult.k_EResultOK && !item.m_bBanned && item.m_nConsumerAppID.m_AppId==App && SteamUGC.GetQueryUGCMetadata(query,i,out string metadata,1024) && System.Text.RegularExpressions.Regex.IsMatch(metadata,@"\Atanukimap/1:[0-9a-fA-F]{40}\z"))
                    list.Add(new Item(item.m_nPublishedFileId.m_PublishedFileId,item.m_rgchTitle,item.m_ulSteamIDOwner==owner,metadata[12..]));
            return list;
        }
        finally{SteamUGC.ReleaseQueryUGCRequest(query);}
    }
    public async Task Refresh()
    {
        Check(!Busy,"A Workshop operation is already running.");Busy=true;
        try
        {
            Connect();var list=new List<Item>();uint count=SteamUGC.GetNumSubscribedItems();Check(count<=10000,"Too many subscribed items.");
            var ids=new PublishedFileId_t[count];SteamUGC.GetSubscribedItems(ids,count);
            for(int offset=0;offset<ids.Length;offset+=50){var batch=ids.Skip(offset).Take(50).ToArray();list.AddRange(await Query(SteamUGC.CreateQueryUGCDetailsRequest(batch,(uint)batch.Length)));}
            // Published items need not be subscribed to appear in the update picker.
            for(uint page=1;page<=200;page++)
            {
                var own=await Query(SteamUGC.CreateQueryUserUGCRequest(SteamUser.GetSteamID().GetAccountID(),EUserUGCList.k_EUserUGCList_Published,EUGCMatchingUGCType.k_EUGCMatchingUGCType_Items,EUserUGCListSortOrder.k_EUserUGCListSortOrder_LastUpdatedDesc,new AppId_t(App),new AppId_t(App),page));
                list.AddRange(own);if(own.Count<50)break;
            }
            Items=list.GroupBy(i=>i.Id).Select(g=>g.First()).OrderBy(i=>i.Title).ToList();
        }
        finally{Busy=false;}
    }
    public async Task<(string File,Item Item)> Download(ulong id)
    {
        Check(!Busy,"A Workshop operation is already running.");Busy=true;
        try
        {
            Connect();var result=await Query(SteamUGC.CreateQueryUGCDetailsRequest(new[]{new PublishedFileId_t(id)},1));Check(result.Count==1,"This is not an accessible Tanuki map.");var item=result[0];
            Check(SteamUGC.DownloadItem(new PublishedFileId_t(id),true),"Steam refused the download.");
            var deadline=DateTime.UtcNow.AddSeconds(120);
            while(DateTime.UtcNow<deadline)
            {
                var state=(EItemState)SteamUGC.GetItemState(new PublishedFileId_t(id));
                if((state&EItemState.k_EItemStateInstalled)!=0 && (state&(EItemState.k_EItemStateNeedsUpdate|EItemState.k_EItemStateDownloading|EItemState.k_EItemStateDownloadPending))==0 && SteamUGC.GetItemInstallInfo(new PublishedFileId_t(id),out _,out string folder,4096,out _))
                {
                    string source=Path.Combine(folder,"map.tanukimap");Check(Hash(source).Equals(item.Hash,StringComparison.OrdinalIgnoreCase),"Downloaded map checksum mismatch.");
                    string snapshot=Path.Combine(stateDirectory,Guid.NewGuid().ToString("N"),"map.tanukimap");Directory.CreateDirectory(Path.GetDirectoryName(snapshot)!);File.Copy(source,snapshot);Check(Hash(snapshot).Equals(item.Hash,StringComparison.OrdinalIgnoreCase),"Workshop revision changed during download.");return(snapshot,item);
                }
                await Task.Delay(100,lifetime.Token);
            }
            throw new TimeoutException("Workshop download timed out.");
        }
        finally{Busy=false;}
    }
    public async Task<ulong> Publish(string file,string title,string preview,ulong existing,Action<ulong> record)
    {
        Check(!Busy,"A Workshop operation is already running.");Busy=true;
        try
        {
            Connect();Check(!string.IsNullOrWhiteSpace(title)&&title.Length<=128,"Workshop title must contain 1–128 characters.");
            Check(File.Exists(preview)&&new FileInfo(preview).Length<1024*1024,"Preview PNG must be smaller than 1 MiB.");
            ulong id=existing;
            if(id==0){var created=await Wait<CreateItemResult_t>(SteamUGC.CreateItem(new AppId_t(App),EWorkshopFileType.k_EWorkshopFileTypeCommunity));Check(created.m_eResult==EResult.k_EResultOK,"Item creation failed: "+created.m_eResult);id=created.m_nPublishedFileId.m_PublishedFileId;record(id);Check(!created.m_bUserNeedsToAcceptWorkshopLegalAgreement,"Accept the Workshop agreement in Steam, then update item "+id);}
            else{var items=await Query(SteamUGC.CreateQueryUGCDetailsRequest(new[]{new PublishedFileId_t(id)},1));Check(items.Count==1&&items[0].Owned,"Only the item's owner can update it.");}
            var update=SteamUGC.StartItemUpdate(new AppId_t(App),new PublishedFileId_t(id));
            Check(SteamUGC.SetItemTitle(update,title)&&SteamUGC.SetItemContent(update,Path.GetDirectoryName(file))&&SteamUGC.SetItemMetadata(update,"tanukimap/1:"+Hash(file))&&SteamUGC.SetItemPreview(update,preview)&&(existing!=0||SteamUGC.SetItemVisibility(update,ERemoteStoragePublishedFileVisibility.k_ERemoteStoragePublishedFileVisibilityPublic)),"Steam refused map metadata.");
            var submitted=await Wait<SubmitItemUpdateResult_t>(SteamUGC.SubmitItemUpdate(update,null));Check(submitted.m_eResult==EResult.k_EResultOK,"Upload failed: "+submitted.m_eResult+". Check item "+id+" before retrying.");Check(!submitted.m_bUserNeedsToAcceptWorkshopLegalAgreement,"Accept the Workshop agreement in Steam.");record(id);SteamUGC.SubscribeItem(new PublishedFileId_t(id));return id;
        }
        finally{Busy=false;}
    }
    public void Dispose(){lifetime.Cancel();foreach(var call in calls.ToArray())call.Dispose();calls.Clear();if(connected)SteamAPI.Shutdown();connected=false;}
}
