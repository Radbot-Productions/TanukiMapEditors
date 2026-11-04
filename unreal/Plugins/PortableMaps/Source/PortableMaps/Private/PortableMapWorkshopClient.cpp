#include "PortableMapWorkshopClient.h"
#include "PortableMapWorkshopSubsystem.h"
#include "PortableMapScene.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Async/Async.h"

#if PLATFORM_WINDOWS && STEAM_SDK_INSTALLED
THIRD_PARTY_INCLUDES_START
#include "steam/steam_api.h"
THIRD_PARTY_INCLUDES_END
#define PORTABLE_STEAM 1
#else
#define PORTABLE_STEAM 0
#endif

#if PORTABLE_STEAM
struct FPortableWorkshopDownloadResult
{
    PublishedFileId_t Item;
    AppId_t App;
    TAtomic<int32> Result{0};
    CCallbackManual<FPortableWorkshopDownloadResult,DownloadItemResult_t> Callback;
    FPortableWorkshopDownloadResult(uint64 InItem,AppId_t InApp) : Item(InItem),App(InApp)
    { Callback.Register(this,&FPortableWorkshopDownloadResult::Received); }
    void Received(DownloadItemResult_t* Value)
    { if (Value->m_nPublishedFileId==Item && Value->m_unAppID==App) Result.Store(int32(Value->m_eResult)); }
};
#endif
struct FPortableWorkshopOperation
{
    struct FVerification {bool Success=false; FString Error;};
    TFuture<FVerification> Verification;
    enum class EStage { Idle, Create, UploadDetails, Submit, Subscribe, DownloadDetails, Download, VerifyDownload, Browse } Stage = EStage::Idle;
    uint64 Call = 0, Query = 0, Item = 0;
    uint64 Owner=0;
    bool bCreateNew=false;
    FString Staging, Title, Preview, Hash, ExpectedHash, InstalledFile;
    double Started = 0;
    FPortableMapDownloadComplete Complete;
    TWeakObjectPtr<UObject> RequestOwner;
    TFunction<bool()> IsContextCurrent;
    FPortableMapBrowseComplete BrowseComplete;
    TArray<uint64> BrowseIds;
    TArray<FPortableMapWorkshopItem> BrowseItems;
    int32 BrowseOffset=0;
#if PORTABLE_STEAM
    TUniquePtr<FPortableWorkshopDownloadResult> DownloadResult;
#endif
};

FPortableMapWorkshopClient::FPortableMapWorkshopClient() { Operation=MakeUnique<FPortableWorkshopOperation>(); }
FPortableMapWorkshopClient::~FPortableMapWorkshopClient()
{
    if (IsPending()) Finish(false,TEXT("Workshop operation interrupted; inspect the recorded item ID before retrying"),Operation->Stage==FPortableWorkshopOperation::EStage::Submit);
}
bool FPortableMapWorkshopClient::IsPending() const { return Operation && Operation->Stage != FPortableWorkshopOperation::EStage::Idle; }
uint64 FPortableMapWorkshopClient::GetItemId() const { return Operation ? Operation->Item : 0; }
bool FPortableMapWorkshopClient::CanBegin(FString& Reason) const
{
    if(AccessQuery && !AccessQuery()) {Reason=TEXT("Custom-map Workshop access is disabled by the host feature policy"); return false;}
    if (IsPending()) { Reason = TEXT("A Workshop operation is already pending"); return false; }
    const auto* Settings = GetDefault<UPortableMapWorkshopSettings>();
    if (!Settings->bDeveloperOnlyWorkshopVerified || Settings->ConsumerAppId <= 0)
    { Reason = TEXT("Developer-only Workshop configuration has not been verified"); return false; }
#if PORTABLE_STEAM
    if (!SteamUGC() || !SteamUser() || !SteamUtils() || !SteamUser()->BLoggedOn())
    { Reason = TEXT("Connect the editor/game to your logged-in Steam client first"); return false; }
    if (SteamUtils()->GetAppID() != Settings->ConsumerAppId)
    { Reason = TEXT("Run the Workshop consumer app; Demo/Playtest app IDs do not share its downloads"); return false; }
    Reason.Reset(); return true;
#else
    Reason = TEXT("Steam Workshop backend is unavailable on this platform/build"); return false;
#endif
}
TArray<uint64> FPortableMapWorkshopClient::GetSubscribed(FString& Reason) const
{
    TArray<uint64> Result;
    if (!CanBegin(Reason)) return Result;
#if PORTABLE_STEAM
    TArray<PublishedFileId_t> Ids;
    Ids.SetNum(SteamUGC()->GetNumSubscribedItems());
    const uint32 Count = SteamUGC()->GetSubscribedItems(Ids.GetData(), Ids.Num());
    for (uint32 I=0; I<Count; ++I) Result.Add(Ids[I]);
#endif
    return Result;
}
bool FPortableMapWorkshopClient::Upload(const FString& File, const FString& Title, const FString& Preview, uint64 ExistingItem, FString& Reason)
{
    if (!CanBegin(Reason)) return false;
    FPortableMapFileInfo Info;
    if (!APortableMapScene::InspectFile(File, Info, Reason)) return false;
    if (!Info.PlayerStarts || Title.TrimStartAndEnd().IsEmpty() || Title.Len()>128)
    { Reason = TEXT("A map needs a player start and a title of 1-128 characters"); return false; }
    if (!Preview.IsEmpty() && !FPaths::FileExists(Preview)) { Reason = TEXT("Preview image does not exist"); return false; }
#if PORTABLE_STEAM
    auto& O = *Operation; O = {};
    O.bCreateNew=ExistingItem==0;
    O.Item = ExistingItem; O.Title = Title; O.Preview = FPaths::ConvertRelativePathToFull(Preview);
    if (Preview.IsEmpty()) O.Preview.Reset();
    O.Hash = Info.FileHash; O.Started = FPlatformTime::Seconds();
    O.Staging = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("PortableMaps/Uploads")/FGuid::NewGuid().ToString(EGuidFormats::Digits));
    IFileManager::Get().MakeDirectory(*O.Staging, true);
    if (IFileManager::Get().Copy(*(O.Staging/TEXT("map.tanukimap")), *File, false) != COPY_OK)
    { Reason = TEXT("Cannot stage the selected map"); Finish(false, Reason); return false; }
    FPortableMapFileInfo StagedInfo;
    if (!APortableMapScene::InspectFile(O.Staging/TEXT("map.tanukimap"), StagedInfo, Reason) || StagedInfo.FileHash != O.Hash)
    { Reason = TEXT("Map changed during upload preparation"); Finish(false, Reason); return false; }
    if (ExistingItem)
    {
        PublishedFileId_t Id = ExistingItem;
        O.Query = SteamUGC()->CreateQueryUGCDetailsRequest(&Id,1);
        O.Call = SteamUGC()->SendQueryUGCRequest(O.Query); O.Stage = FPortableWorkshopOperation::EStage::UploadDetails;
    }
    else
    {
        O.Call = SteamUGC()->CreateItem(SteamUtils()->GetAppID(), k_EWorkshopFileTypeCommunity);
        O.Stage = FPortableWorkshopOperation::EStage::Create;
    }
    if (!O.Call) { Reason = TEXT("Steam refused the upload request"); Finish(false, Reason); return false; }
    Status = TEXT("Workshop upload pending"); return true;
#else
    return false;
#endif
}
bool FPortableMapWorkshopClient::BrowseSubscribed(FPortableMapBrowseComplete Complete,FString& Reason,UObject* Owner,TFunction<bool()> Current)
{
    if(!CanBegin(Reason)) return false;
    auto Ids=GetSubscribed(Reason);
    if(Ids.IsEmpty()) {Complete(true,{},TEXT("")); return true;}
#if PORTABLE_STEAM
    auto& O=*Operation; O={}; O.BrowseIds=MoveTemp(Ids); O.BrowseComplete=MoveTemp(Complete);
    O.RequestOwner=Owner; O.IsContextCurrent=MoveTemp(Current); O.Started=FPlatformTime::Seconds(); O.Stage=FPortableWorkshopOperation::EStage::Browse;
    TArray<PublishedFileId_t> Batch; for(int32 I=0;I<FMath::Min(50,O.BrowseIds.Num());I++) Batch.Add(O.BrowseIds[I]);
    O.Query=SteamUGC()->CreateQueryUGCDetailsRequest(Batch.GetData(),Batch.Num()); SteamUGC()->SetReturnMetadata(O.Query,true); O.Call=SteamUGC()->SendQueryUGCRequest(O.Query);
    if(!O.Call) {Reason=TEXT("Steam refused the map list query"); Finish(false,Reason); return false;}
    Status=TEXT("Reading subscribed Workshop maps"); return true;
#else
    return false;
#endif
}
bool FPortableMapWorkshopClient::Download(uint64 Item, const FString& Hash, FPortableMapDownloadComplete Complete, FString& Reason,
    UObject* RequestOwner,TFunction<bool()> IsContextCurrent)
{
    if (!CanBegin(Reason)) return false;
    if (!Item) { Reason = TEXT("Invalid Workshop item ID"); return false; }
#if PORTABLE_STEAM
    auto& O = *Operation; O = {}; O.Item = Item; O.ExpectedHash = Hash; O.Complete = MoveTemp(Complete); O.Started = FPlatformTime::Seconds();
    O.RequestOwner=RequestOwner; O.IsContextCurrent=MoveTemp(IsContextCurrent);
    PublishedFileId_t Id = Item;
    O.Query = SteamUGC()->CreateQueryUGCDetailsRequest(&Id,1);
    SteamUGC()->SetReturnMetadata(O.Query, true);
    O.Call = SteamUGC()->SendQueryUGCRequest(O.Query); O.Stage = FPortableWorkshopOperation::EStage::DownloadDetails;
    if (!O.Call) { Reason = TEXT("Steam refused the item query"); Finish(false, Reason); return false; }
    Status = TEXT("Workshop map download pending"); return true;
#else
    return false;
#endif
}
void FPortableMapWorkshopClient::CancelDownload(UObject* Owner)
{
    if (!Owner) return;
    if (IsPending() && Operation->RequestOwner==Owner
        && (Operation->Stage==FPortableWorkshopOperation::EStage::Download || Operation->Stage==FPortableWorkshopOperation::EStage::DownloadDetails || Operation->Stage==FPortableWorkshopOperation::EStage::VerifyDownload || Operation->Stage==FPortableWorkshopOperation::EStage::Browse))
        Finish(false,TEXT("Map download cancelled because its owning request ended"));
}
void FPortableMapWorkshopClient::ReconcileDownloadContext(UObject* Owner)
{
    if (IsPending() && Operation->RequestOwner==Owner && Operation->IsContextCurrent && !Operation->IsContextCurrent()) CancelDownload(Owner);
}
void FPortableMapWorkshopClient::Finish(bool Success, const FString& Result,bool PreserveStaging)
{
    if (!Operation) return;
    auto& O = *Operation;
    bSuccessful=Success;
    Status = Result;
#if PORTABLE_STEAM
    if (O.Query && SteamUGC()) SteamUGC()->ReleaseQueryUGCRequest(O.Query);
    O.DownloadResult.Reset();
#endif
    O.Query = 0; O.Call = 0; O.Stage = FPortableWorkshopOperation::EStage::Idle;
    if (!O.Staging.IsEmpty() && !PreserveStaging)
    {
        IFileManager::Get().Delete(*(O.Staging/TEXT("map.tanukimap")));
        IFileManager::Get().DeleteDirectory(*O.Staging, false, false);
        O.Staging.Reset();
    }
    UE_LOG(LogTemp, Display, TEXT("PORTABLE_WORKSHOP_COMPLETE success=%d item=%llu result=%s"), Success, O.Item, *Result);
    auto Complete = MoveTemp(O.Complete);
    auto BrowseComplete=MoveTemp(O.BrowseComplete); auto Items=MoveTemp(O.BrowseItems);
    O.RequestOwner.Reset(); O.IsContextCurrent={};
    if (Complete) Complete(Success, Success ? O.InstalledFile : FString(), Result);
    if (BrowseComplete) BrowseComplete(Success,Items,Result);
}
void FPortableMapWorkshopClient::Tick()
{
#if PORTABLE_STEAM
    if (!IsPending()) return;
    if(AccessQuery && !AccessQuery()) {Finish(false,TEXT("Custom-map Workshop access was revoked")); return;}
    using EStage = FPortableWorkshopOperation::EStage;
    auto& O = *Operation;
    if (!SteamUGC() || !SteamUtils() || !SteamUser() || !SteamUser()->BLoggedOn()) { Finish(false, TEXT("Steam context was lost"),O.Stage==EStage::Submit); return; }
    if(O.IsContextCurrent && !O.IsContextCurrent()) {Finish(false,TEXT("Workshop request context changed")); return;}
    if (FPlatformTime::Seconds()-O.Started>180)
    { Finish(false,O.Stage==EStage::Submit ? TEXT("Steam upload timed out; it may complete remotely. Retry using the recorded item ID.") : TEXT("Steam Workshop operation timed out. Check the Steam client and retry."),O.Stage==EStage::Submit); return; }
    const auto Submit = [&]()
    {
        // Items are shared inside the developer-only Workshop. This never changes the Workshop's access policy.
        const UGCUpdateHandle_t Update = SteamUGC()->StartItemUpdate(SteamUtils()->GetAppID(), O.Item);
        const FString Metadata = TEXT("tanukimap/1:") + O.Hash;
        const bool Configured = SteamUGC()->SetItemTitle(Update,TCHAR_TO_UTF8(*O.Title))
            && SteamUGC()->SetItemContent(Update,TCHAR_TO_UTF8(*O.Staging))
            && SteamUGC()->SetItemMetadata(Update,TCHAR_TO_UTF8(*Metadata))
            && (!O.bCreateNew || SteamUGC()->SetItemVisibility(Update,k_ERemoteStoragePublishedFileVisibilityPublic))
            && (O.Preview.IsEmpty() || SteamUGC()->SetItemPreview(Update,TCHAR_TO_UTF8(*O.Preview)));
        if (!Configured) { Finish(false,TEXT("Steam refused upload metadata/content")); return; }
        // Persist the created ID before submission so a failed/uncertain submission can be updated rather than duplicated.
        FFileHelper::SaveStringToFile(FString::Printf(TEXT("%llu\n%s\n%s\n"),O.Item,*O.Hash,*O.Staging),*(FPaths::ProjectSavedDir()/TEXT("PortableMaps/last-upload.txt")));
        UE_LOG(LogTemp,Display,TEXT("PORTABLE_WORKSHOP_UPLOAD_ITEM item=%llu hash=%s"),O.Item,*O.Hash);
        O.Call=SteamUGC()->SubmitItemUpdate(Update,nullptr); O.Stage=EStage::Submit;
        if (!O.Call) Finish(false,TEXT("Steam refused submission"));
    };
    if (O.Stage==EStage::Download)
    {
        const int32 DownloadResult=O.DownloadResult ? O.DownloadResult->Result.Load() : 0;
        if (DownloadResult!=0 && DownloadResult!=int32(k_EResultOK))
        { Finish(false,FString::Printf(TEXT("Steam map download failed (result=%d). Check Steam's Workshop log and app/depot configuration."),DownloadResult)); return; }
        const uint32 State=SteamUGC()->GetItemState(O.Item);
        if (!(State & k_EItemStateInstalled) || (State & (k_EItemStateNeedsUpdate|k_EItemStateDownloading|k_EItemStateDownloadPending))) return;
        uint64 Size=0; uint32 Timestamp=0; char Folder[4096]={};
        if (!SteamUGC()->GetItemInstallInfo(O.Item,&Size,Folder,sizeof(Folder),&Timestamp)) { Finish(false,TEXT("Cannot resolve installed Workshop item")); return; }
        O.InstalledFile=FPaths::ConvertRelativePathToFull(UTF8_TO_TCHAR(Folder))/TEXT("map.tanukimap");
        const FString Installed=O.InstalledFile,Expected=O.Hash;
        // The SDK owns the installed file and can replace it. Publish only our verified immutable snapshot.
        O.InstalledFile=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("PortableMaps/Downloads")/FGuid::NewGuid().ToString(EGuidFormats::Digits)/TEXT("map.tanukimap"));
        const FString Snapshot=O.InstalledFile;
        O.Stage=EStage::VerifyDownload;
        O.Verification=Async(EAsyncExecution::ThreadPool,[Installed,Snapshot,Expected]
        {
            FPortableWorkshopOperation::FVerification Result; FPortableMapFileInfo Info;
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(Snapshot),true);
            Result.Success=IFileManager::Get().Copy(*Snapshot,*Installed,false)==COPY_OK && APortableMapScene::InspectFile(Snapshot,Info,Result.Error) && Info.FileHash.Equals(Expected,ESearchCase::IgnoreCase);
            if(!Result.Success && Result.Error.IsEmpty()) Result.Error=TEXT("Downloaded map could not be snapshotted or differs from selected revision");
            return Result;
        });
        return;
    }
    if(O.Stage==EStage::VerifyDownload)
    {
        if(!O.Verification.IsReady()) return;
        auto Result=O.Verification.Get(); Finish(Result.Success,Result.Success?O.InstalledFile:Result.Error); return;
    }
    bool Failed=false;
    if (!SteamUtils()->IsAPICallCompleted(O.Call,&Failed)) return;
    if (Failed) { Finish(false,TEXT("Steam API transport failure"),O.Stage==EStage::Submit); return; }
    const auto ReadResult=[&](auto& Result)
    {
        return SteamUtils()->GetAPICallResult(O.Call,&Result,sizeof(Result),Result.k_iCallback,&Failed) && !Failed;
    };
    if(O.Stage==EStage::Browse)
    {
        SteamUGCQueryCompleted_t Result={};
        if(!ReadResult(Result) || Result.m_eResult!=k_EResultOK) {Finish(false,TEXT("Steam could not read subscribed maps")); return;}
        for(uint32 I=0;I<Result.m_unNumResultsReturned;I++)
        {
            SteamUGCDetails_t D={}; char Metadata[512]={},Preview[4096]={};
            if(!SteamUGC()->GetQueryUGCResult(O.Query,I,&D) || D.m_eResult!=k_EResultOK || D.m_bBanned || D.m_nConsumerAppID!=SteamUtils()->GetAppID()
                || !SteamUGC()->GetQueryUGCMetadata(O.Query,I,Metadata,sizeof(Metadata))) continue;
            FString Data=UTF8_TO_TCHAR(Metadata);
            if(!Data.StartsWith(TEXT("tanukimap/1:")) || Data.Len()!=52) continue;
            FPortableMapWorkshopItem Item; Item.Id=D.m_nPublishedFileId; Item.Title=UTF8_TO_TCHAR(D.m_rgchTitle); Item.Hash=Data.Mid(12); Item.bOwnedByCurrentUser=SteamUser() && D.m_ulSteamIDOwner==SteamUser()->GetSteamID().ConvertToUint64();
            SteamUGC()->GetQueryUGCPreviewURL(O.Query,I,Preview,sizeof(Preview)); Item.PreviewUrl=UTF8_TO_TCHAR(Preview); O.BrowseItems.Add(MoveTemp(Item));
        }
        SteamUGC()->ReleaseQueryUGCRequest(O.Query); O.Query=0; O.BrowseOffset+=50;
        if(O.BrowseOffset>=O.BrowseIds.Num()) {Finish(true,TEXT("Subscribed maps loaded")); return;}
        TArray<PublishedFileId_t> Batch;
        for(int32 I=O.BrowseOffset;I<FMath::Min(O.BrowseOffset+50,O.BrowseIds.Num());I++) Batch.Add(O.BrowseIds[I]);
        O.Query=SteamUGC()->CreateQueryUGCDetailsRequest(Batch.GetData(),Batch.Num()); SteamUGC()->SetReturnMetadata(O.Query,true); O.Call=SteamUGC()->SendQueryUGCRequest(O.Query);
        if(!O.Call) Finish(false,TEXT("Steam refused the next map list query"));
    }
    else if (O.Stage==EStage::Create)
    {
        CreateItemResult_t Result={};
        if (!ReadResult(Result) || Result.m_eResult!=k_EResultOK) { Finish(false,TEXT("Steam item creation failed")); return; }
        O.Item=Result.m_nPublishedFileId;
        if (Result.m_bUserNeedsToAcceptWorkshopLegalAgreement) { Finish(false,TEXT("Accept the Steam Workshop agreement in Steam, then retry with this item ID")); return; }
        Submit();
    }
    else if (O.Stage==EStage::Submit)
    {
        SubmitItemUpdateResult_t Result={};
        const bool Read=ReadResult(Result);
        if (!Read || Result.m_eResult!=k_EResultOK)
        { Finish(false,FString::Printf(TEXT("Steam upload failed (result=%d, transport=%d). Retry using the recorded item ID; preview failures can require a Steam Cloud quota."),int32(Result.m_eResult),!Read),true); return; }
        if (Result.m_bUserNeedsToAcceptWorkshopLegalAgreement) { Finish(false,TEXT("Upload requires acceptance of the Steam Workshop agreement")); return; }
        O.Call=SteamUGC()->SubscribeItem(O.Item); O.Stage=EStage::Subscribe;
        if (!O.Call) Finish(true,TEXT("Map uploaded. Steam refused automatic subscription; use its recorded item ID to download."));
    }
    else if (O.Stage==EStage::Subscribe)
    {
        RemoteStorageSubscribePublishedFileResult_t Result={};
        const bool Subscribed=ReadResult(Result) && Result.m_eResult==k_EResultOK;
        Finish(true,Subscribed ? TEXT("Map uploaded and subscribed in Steam Workshop") : TEXT("Map uploaded, but subscription failed. Use the recorded item ID to download."));
    }
    else
    {
        SteamUGCQueryCompleted_t Result={}; SteamUGCDetails_t Details={};
        if (!ReadResult(Result) || Result.m_eResult!=k_EResultOK || Result.m_unNumResultsReturned!=1
            || !SteamUGC()->GetQueryUGCResult(O.Query,0,&Details) || Details.m_eResult!=k_EResultOK
            || Details.m_nPublishedFileId!=O.Item || Details.m_nConsumerAppID!=SteamUtils()->GetAppID() || Details.m_bBanned)
        { Finish(false,TEXT("Workshop item is inaccessible, banned or belongs to another app")); return; }
        if (O.Stage==EStage::UploadDetails)
        {
            if (Details.m_ulSteamIDOwner!=SteamUser()->GetSteamID().ConvertToUint64()) { Finish(false,TEXT("Only the item owner can update it")); return; }
            Submit(); return;
        }
        O.Owner=Details.m_ulSteamIDOwner;
        char Metadata[512]={};
        if (!SteamUGC()->GetQueryUGCMetadata(O.Query,0,Metadata,sizeof(Metadata))) { Finish(false,TEXT("Workshop item has no map metadata")); return; }
        const FString Data=UTF8_TO_TCHAR(Metadata);
        if (!Data.StartsWith(TEXT("tanukimap/1:")) || Data.Len()!=52) { Finish(false,TEXT("Unsupported Workshop map format")); return; }
        O.Hash=Data.Mid(12);
        if (!O.ExpectedHash.IsEmpty() && !O.Hash.Equals(O.ExpectedHash,ESearchCase::IgnoreCase)) { Finish(false,TEXT("Workshop revision changed since the host selected it")); return; }
        O.DownloadResult=MakeUnique<FPortableWorkshopDownloadResult>(O.Item,SteamUtils()->GetAppID());
        if (!SteamUGC()->DownloadItem(O.Item,true)) { Finish(false,TEXT("Steam refused map download")); return; }
        O.Stage=EStage::Download;
    }
#endif
}

FString FPortableMapWorkshopClient::GetHash() const { return Operation ? Operation->Hash : FString(); }
FString FPortableMapWorkshopClient::GetInstalledFile() const { return Operation ? Operation->InstalledFile : FString(); }
bool FPortableMapWorkshopClient::IsOwnedByCurrentUser() const
{
#if PORTABLE_STEAM
    return Operation && Operation->Owner && SteamUser() && SteamUser()->BLoggedOn() && Operation->Owner==SteamUser()->GetSteamID().ConvertToUint64();
#else
    return false;
#endif
}
