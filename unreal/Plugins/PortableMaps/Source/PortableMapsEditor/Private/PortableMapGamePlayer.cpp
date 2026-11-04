#include "PortableMapGamePlayer.h"
#include "PortableMapAuthoring.h"
#include "Editor.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/ConfigCacheIni.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Containers/Ticker.h"

namespace PortableMapGamePlayer
{
namespace
{
struct FPreview {FProcHandle Process;FString Directory;};
TArray<FPreview> Running;
FTSTicker::FDelegateHandle PollHandle;
FString Status;
bool ReadObject(const FString& File,TSharedPtr<FJsonObject>& Object)
{FString Text;return FFileHelper::LoadFileToString(Text,*File)&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Object)&&Object.IsValid();}
void RemoveSetupFiles(const FString& Directory)
{for(const TCHAR* Name:{TEXT("setup-request.json"),TEXT("setup-result.json"),TEXT("setup-progress.json"),TEXT("cancel")})IFileManager::Get().Delete(*(Directory/Name));}
bool Poll(float)
{
    for(int32 I=Running.Num()-1;I>=0;--I)
    {
        auto& Item=Running[I];FString Log;FFileHelper::LoadFileToString(Log,*(Item.Directory/TEXT("preview.log")),FFileHelper::EHashOptions::None,FILEREAD_AllowWrite);
        if(FPlatformProcess::IsProcRunning(Item.Process))
        {
            if(Log.Contains(TEXT("TANUKI_PREVIEW_READY")))Status=TEXT("Tanuki preview is running. Escape opens its menu.");
            else
            {
                TSharedPtr<FJsonObject> Progress;
                if(ReadObject(Item.Directory/TEXT("setup-progress.json"),Progress))
                {Progress->TryGetStringField(TEXT("phase"),Status);double Bytes=0,Total=0;Progress->TryGetNumberField(TEXT("bytes"),Bytes);Progress->TryGetNumberField(TEXT("total"),Total);if(Total>1)Status+=FString::Printf(TEXT(" (%.0f / %.0f MB)"),Bytes/1048576.,Total/1048576.);}
            }
            continue;
        }
        int32 Exit=1;FPlatformProcess::GetProcReturnCode(Item.Process,&Exit);FPlatformProcess::CloseProc(Item.Process);
        IFileManager::Get().Delete(*(Item.Directory/TEXT("map.tanukimap")));
        if(Exit==0&&Log.Contains(TEXT("TANUKI_PREVIEW_READY")))
        {IFileManager::Get().Delete(*(Item.Directory/TEXT("preview.log")));RemoveSetupFiles(Item.Directory);IFileManager::Get().DeleteDirectory(*Item.Directory);Status=TEXT("Preview closed. Play again to include current scene edits.");}
        else
        {
            TSharedPtr<FJsonObject> Result;FString Reason;
            if(ReadObject(Item.Directory/TEXT("setup-result.json"),Result))Result->TryGetStringField(TEXT("error"),Reason);
            Status=Reason.IsEmpty()?TEXT("Preview closed before readiness; diagnostics: ")+Item.Directory:Reason;
        }
        Running.RemoveAt(I);
    }
    return true;
}
bool Launch(const FString& Directory,FString& Error)
{
    const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("PortableMaps"));
    const FString Setup=Plugin?Plugin->GetBaseDir()/TEXT("Resources/PreviewSetup/TanukiPreviewSetup.exe"):FString();
    if(!FPaths::FileExists(Setup)){Error=TEXT("The preview setup helper is missing. Reinstall the current Tanuki map editor package.");return false;}
    FString Previous;GConfig->GetString(TEXT("Tanuki.MapEditor"),TEXT("GameplayPreviewExecutable"),Previous,GEditorPerProjectIni);
    auto Request=MakeShared<FJsonObject>();Request->SetNumberField(TEXT("requiredSchema"),5);Request->SetStringField(TEXT("candidateExecutable"),Previous);Request->SetStringField(TEXT("mapFile"),Directory/TEXT("map.tanukimap"));Request->SetStringField(TEXT("logFile"),Directory/TEXT("preview.log"));
    FString Json;FJsonSerializer::Serialize(Request,TJsonWriterFactory<>::Create(&Json));
    const FString File=Directory/TEXT("setup-request.json");
    if(!FFileHelper::SaveStringToFile(Json,*File)){Error=TEXT("Cannot create the preview setup request.");return false;}
    const FString Args=FString::Printf(TEXT("\"%s\""),*File);
    auto Process=FPlatformProcess::CreateProc(*Setup,*Args,true,true,true,nullptr,0,*FPaths::GetPath(Setup),nullptr);
    if(!Process.IsValid()){Error=TEXT("Could not start Tanuki preview setup.");RemoveSetupFiles(Directory);return false;}
    Running.Add({Process,Directory});if(!PollHandle.IsValid())PollHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Poll),1);
    Status=TEXT("Preparing Tanuki preview. The player installs automatically on first use.");return true;
}
FString NewDirectory()
{FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("PortableMapPreviews")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*Directory,true);return Directory;}
}
bool Start(const FString& File,FString& Error)
{
    const FString Directory=NewDirectory();
    if(IFileManager::Get().Copy(*(Directory/TEXT("map.tanukimap")),*File)!=COPY_OK){Error=TEXT("Could not snapshot the map file.");IFileManager::Get().DeleteDirectory(*Directory);return false;}
    if(Launch(Directory,Error))return true;
    IFileManager::Get().Delete(*(Directory/TEXT("map.tanukimap")));IFileManager::Get().DeleteDirectory(*Directory);return false;
}
bool StartEdited(FString& Error)
{
    auto* Root=GEditor?PortableMapAuthoring::FindRoot(GEditor->GetEditorWorldContext().World()):nullptr;
    if(!Root){Error=TEXT("The active scene must contain exactly one Portable Map Root.");return false;}
    const FString Directory=NewDirectory();
    if(PortableMapAuthoring::Export(Root,Directory/TEXT("map.tanukimap"),Error)&&Launch(Directory,Error))return true;
    IFileManager::Get().Delete(*(Directory/TEXT("map.tanukimap")));IFileManager::Get().DeleteDirectory(*Directory);return false;
}
FString GetStatus(){return Status;}
void Shutdown()
{
    FTSTicker::GetCoreTicker().RemoveTicker(PollHandle);PollHandle.Reset();
    for(auto& Item:Running)
    {
        if(FPlatformProcess::IsProcRunning(Item.Process))
        {FFileHelper::SaveStringToFile(TEXT(""),*(Item.Directory/TEXT("cancel")));const double Deadline=FPlatformTime::Seconds()+5;while(FPlatformProcess::IsProcRunning(Item.Process)&&FPlatformTime::Seconds()<Deadline)FPlatformProcess::Sleep(.05f);if(FPlatformProcess::IsProcRunning(Item.Process))FPlatformProcess::TerminateProc(Item.Process,true);FPlatformProcess::WaitForProc(Item.Process);}
        FPlatformProcess::CloseProc(Item.Process);IFileManager::Get().Delete(*(Item.Directory/TEXT("map.tanukimap")));
    }
    Running.Reset();
}
}
