#include "PortableMapWorkshopEditor.h"
#include "PortableMapScene.h"
#include "PortableMapWorkshopSubsystem.h"
#include "PortableMapWorkshopClient.h"
#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/SecureHash.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/ScopeExit.h"
#include "PortableMapAuthoring.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "UObject/StrongObjectPtr.h"
#include "Editor.h"
#include "PortableMapEditorHost.h"
#if PLATFORM_WINDOWS && STEAM_SDK_INSTALLED
#include "SteamSharedModule.h"
#include "OnlineSubsystem.h"
THIRD_PARTY_INCLUDES_START
#include "steam/steam_api.h"
THIRD_PARTY_INCLUDES_END
#define TANUKI_EDITOR_STEAM 1
#else
#define TANUKI_EDITOR_STEAM 0
#endif

namespace PortableMapWorkshopEditor
{
namespace
{
TUniquePtr<FPortableMapWorkshopClient> Client;
FString Status,Account,UploadContent,Directory;
bool Pending=false;
FTSTicker::FDelegateHandle TickHandle;
TFunction<void(const FPortableMapWorkshopEditorResult&)> Completion;
#if TANUKI_EDITOR_STEAM
TSharedPtr<FSteamClientInstanceHandler> SteamHandle;
#endif
FString ItemKey(const FString& ContentId)
{
    const FString Id=ContentId.Len()>41 ? ContentId.LeftChop(41) : ContentId;
    FTCHARToUTF8 Utf8(*Id); FSHAHash Hash; FSHA1::HashBuffer(Utf8.Get(),Utf8.Length(),Hash.Hash);
    return TEXT("WorkshopItem.")+Hash.ToString();
}
void RememberItem(uint64 Id)
{
    if (UploadContent.IsEmpty() || !Id) return;
    const FString Key=ItemKey(UploadContent),Value=LexToString(Id);
    FString Old; GConfig->GetString(TEXT("Tanuki.PortableMapViewer"),*Key,Old,GEditorPerProjectIni);
    if (Old!=Value) { GConfig->SetString(TEXT("Tanuki.PortableMapViewer"),*Key,*Value,GEditorPerProjectIni); GConfig->Flush(false,GEditorPerProjectIni); }
}
void Finish(FPortableMapWorkshopEditorResult Result)
{
    Pending=false; Status=Result.Message;
    uint64 Id=0;
    if (LexTryParseString(Id,*Result.ItemId)) RememberItem(Id);
    auto Callback=MoveTemp(Completion);
    UE_LOG(LogTemp,Display,TEXT("PORTABLE_WORKSHOP_EDITOR_RESULT direct=1 success=%d item=%s message=%s"),Result.Success,*Result.ItemId,*Result.Message);
    if (Callback) Callback(Result);
}
FPortableMapWorkshopEditorResult CurrentResult(bool Success,const FString& Message)
{
    FPortableMapWorkshopEditorResult Result; Result.Success=Success; Result.Message=Message;
    Result.ItemId=LexToString(Client->GetItemId()); Result.Hash=Client->GetHash(); Result.File=Client->GetInstalledFile();
    return Result;
}
}
bool IsPending() { return Pending; }
FString GetStatus() { return Status; }
bool IsConnected()
{
#if TANUKI_EDITOR_STEAM
    const auto* Settings=GetDefault<UPortableMapWorkshopSettings>();
    return Settings->bDeveloperOnlyWorkshopVerified && SteamHandle.IsValid() && SteamUser() && SteamUser()->BLoggedOn() && SteamUtils() && SteamUtils()->GetAppID()==Settings->ConsumerAppId;
#else
    return false;
#endif
}
FString GetAccount() { return !IsConnected() || Account.IsEmpty() ? TEXT("Steam: not connected") : Account; }
FString GetRememberedItem(const FString& ContentId)
{ FString Item; GConfig->GetString(TEXT("Tanuki.PortableMapViewer"),*ItemKey(ContentId),Item,GEditorPerProjectIni); return Item; }
bool RememberOwnedMap(const FString& File)
{
    if(Pending || !Client || !Client->IsOwnedByCurrentUser()) return false;
    FPortableMapFileInfo Info; FString Error; if(!APortableMapScene::InspectFile(File,Info,Error)) return false;
    GConfig->SetString(TEXT("Tanuki.PortableMapViewer"),*ItemKey(Info.ContentId),*LexToString(Client->GetItemId()),GEditorPerProjectIni); GConfig->Flush(false,GEditorPerProjectIni); return true;
}
bool Connect(FString& Reason)
{
    if(!PortableMapEditorHost::HasAccess()) {Reason=TEXT("Enable full-game and development features in the existing PIE entitlement settings"); return false;}
    const auto* Settings=GetDefault<UPortableMapWorkshopSettings>();
    if (!Settings->bDeveloperOnlyWorkshopVerified || Settings->ConsumerAppId<=0)
    { Reason=TEXT("Developer-only Workshop configuration has not been verified"); return false; }
#if TANUKI_EDITOR_STEAM
    if (SteamHandle.IsValid() && (!SteamUser() || !SteamUser()->BLoggedOn()))
    {
        if (Pending) { Reason=TEXT("Steam disconnected during the current operation"); return false; }
        Client.Reset(); SteamHandle.Reset(); Account.Reset();
    }
    if (SteamUtils() && SteamUtils()->GetAppID()!=Settings->ConsumerAppId)
    { Reason=TEXT("This editor already has a Steam session for another app; use a Tanuki editor session"); return false; }
    if (!SteamHandle.IsValid())
    {
        if (SteamUtils() && !IOnlineSubsystem::DoesInstanceExist(FName(TEXT("STEAM"))))
        { Reason=TEXT("Steam is initialized outside the engine's shared session; cannot safely take ownership"); return false; }
        // The engine may be installed in a protected directory where its app-ID
        // text file cannot be written. Supply only this process's init context.
        const FString OldApp=FPlatformMisc::GetEnvironmentVariable(TEXT("SteamAppId"));
        const FString OldGame=FPlatformMisc::GetEnvironmentVariable(TEXT("SteamGameId"));
        const FString App=LexToString(Settings->ConsumerAppId);
        FPlatformMisc::SetEnvironmentVar(TEXT("SteamAppId"),*App);
        FPlatformMisc::SetEnvironmentVar(TEXT("SteamGameId"),*App);
        ON_SCOPE_EXIT { FPlatformMisc::SetEnvironmentVar(TEXT("SteamAppId"),*OldApp); FPlatformMisc::SetEnvironmentVar(TEXT("SteamGameId"),*OldGame); };
        SteamHandle=FSteamSharedModule::Get().ObtainSteamClientInstanceHandle();
        if (!SteamHandle.IsValid() || !SteamHandle->IsInitialized())
        { SteamHandle.Reset(); Reason=TEXT("Run Steam and sign into an account with Tanuki access, then reconnect"); return false; }
    }
    if (!SteamUser() || !SteamUGC() || !SteamFriends() || !SteamUtils() || !SteamUser()->BLoggedOn())
    { Reason=TEXT("Steam is not logged in; sign into the Steam client and reconnect"); return false; }
    if (SteamUtils()->GetAppID()!=Settings->ConsumerAppId)
    { Reason=TEXT("Steam connected to a different app; check the project's Steam app configuration"); return false; }
    Account=FString::Printf(TEXT("Steam: %s"),UTF8_TO_TCHAR(SteamFriends()->GetPersonaName()));
    if (!Client) {Client=MakeUnique<FPortableMapWorkshopClient>(); Client->SetAccessQuery([]{return PortableMapEditorHost::HasAccess();});}
    if (!TickHandle.IsValid()) TickHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float) { Poll(); return true; }));
    Reason.Reset();
    UE_LOG(LogTemp,Display,TEXT("PORTABLE_WORKSHOP_EDITOR_CONNECTED direct=1 app=%u account=%s"),SteamUtils()->GetAppID(),*Account);
    return true;
#else
    Reason=TEXT("Editor Workshop publishing currently supports Windows"); return false;
#endif
}
bool Start(const FString& Action,const FString& File,const FString& Title,const FString& Preview,const FString& Item,
    TFunction<void(const FPortableMapWorkshopEditorResult&)> Complete,FString& Reason)
{
    if (Pending) { Reason=TEXT("A Workshop operation is already pending"); return false; }
    if (Action!=TEXT("upload") && Action!=TEXT("download") && Action!=TEXT("list")) { Reason=TEXT("Unknown Workshop action"); return false; }
    uint64 Id=0;
    if ((!Item.IsEmpty() && (!LexTryParseString(Id,*Item) || !Id)) || (Action==TEXT("download") && !Id))
    { Reason=TEXT("Enter a positive numeric Workshop item ID"); return false; }
    FPortableMapFileInfo Info;
    if (Action==TEXT("upload") && !APortableMapScene::InspectFile(File,Info,Reason)) return false;
    if (!Connect(Reason) || !Client->CanBegin(Reason)) return false;
    UploadContent=Action==TEXT("upload") ? Info.ContentId : FString();
    Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("PortableMaps/EditorJobs")/FGuid::NewGuid().ToString(EGuidFormats::Digits));
    Completion=MoveTemp(Complete); Pending=true; Status=TEXT("Steam Workshop operation pending");
    if (Action==TEXT("list"))
    {
        const bool Accepted=Client->BrowseSubscribed([](bool Success,const TArray<FPortableMapWorkshopItem>& Items,const FString& Message)
        {FPortableMapWorkshopEditorResult Result;Result.Success=Success;Result.Message=Message;
            for(const auto& Item:Items){Result.Items.Add(LexToString(Item.Id));Result.Titles.Add(Item.Title);if(Item.bOwnedByCurrentUser){Result.OwnedItems.Add(LexToString(Item.Id));Result.OwnedTitles.Add(Item.Title);}}Finish(MoveTemp(Result));},Reason);
        if(!Accepted){Pending=false;Completion={};Status=Reason;}return Accepted;
    }
    bool Accepted=false;
    if (Action==TEXT("upload")) Accepted=Client->Upload(File,Title,Preview,Id,Reason);
    else Accepted=Client->Download(Id,TEXT(""),[](bool Success,const FString& File,const FString& Message)
    {
        auto Result=CurrentResult(Success,Message);
        Result.Owned=Success && Client->IsOwnedByCurrentUser();
        if (Success)
        {
            const FString Snapshot=Directory/TEXT("map.tanukimap");
            IFileManager::Get().MakeDirectory(*Directory,true);
            FPortableMapFileInfo Info; FString Error;
            if (IFileManager::Get().Copy(*Snapshot,*File,false)!=COPY_OK || !APortableMapScene::InspectFile(Snapshot,Info,Error) || Info.FileHash!=Result.Hash)
            { Result.Success=false; Result.Message=TEXT("Cannot snapshot the verified Workshop revision"); Result.File.Reset(); }
            else Result.File=Snapshot;
        }
        Finish(MoveTemp(Result));
    },Reason);
    if (!Accepted) { Pending=false; Completion={}; Status=Reason; return false; }
    UE_LOG(LogTemp,Display,TEXT("PORTABLE_WORKSHOP_EDITOR_STARTED direct=1 action=%s item=%llu"),*Action,Id);
    return true;
}
void Poll()
{
#if TANUKI_EDITOR_STEAM
    // An active OSS Steam instance owns its pump. Otherwise this editor lease does.
    if (SteamHandle.IsValid() && !IOnlineSubsystem::DoesInstanceExist(FName(TEXT("STEAM")))) SteamAPI_RunCallbacks();
    if (!SteamUser() || !SteamUser()->BLoggedOn()) Account.Reset();
#endif
    if (!Pending || !Client) return;
    Client->Tick();
    RememberItem(Client->GetItemId());
    if (!Pending) return;
    Status=Client->GetStatus();
    if (!Client->IsPending()) Finish(CurrentResult(Client->WasSuccessful(),Status));
}
void Shutdown()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickHandle); TickHandle.Reset();
    if (Client) RememberItem(Client->GetItemId());
    Completion={}; Client.Reset(); Pending=false;
#if TANUKI_EDITOR_STEAM
    SteamHandle.Reset();
#endif
    Account.Reset();
}
static FAutoConsoleCommand ConnectCommand(TEXT("Tanuki.Workshop.Editor.Connect"),TEXT("Connect the editor directly to the logged-in Steam client."),
    FConsoleCommandDelegate::CreateLambda([] { FString Reason; const bool Connected=Connect(Reason); if (!Connected) Status=Reason; UE_LOG(LogTemp,Display,TEXT("PORTABLE_WORKSHOP_EDITOR_CONNECT accepted=%d reason=%s"),Connected,*Reason); }));
static FAutoConsoleCommand UploadCommand(TEXT("Tanuki.Workshop.Editor.Upload"),TEXT("Direct editor upload: <map> <title> <preview> [existing item ID]."),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
    { if (Args.Num()<3) return; FString Reason; const bool Accepted=Start(TEXT("upload"),Args[0],Args[1],Args[2],Args.Num()>3?Args[3]:FString(),{},Reason); UE_LOG(LogTemp,Display,TEXT("PORTABLE_WORKSHOP_EDITOR_UPLOAD accepted=%d reason=%s"),Accepted,*Reason); }));
}
