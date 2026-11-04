#include "PortableMapEditorPreview.h"
#include "PortableMapPreviewCharacter.h"
#include "PortableMapAuthoring.h"
#include "PortableMapEditorWindow.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/UICommandList.h"
#include "IAssetViewport.h"
#include "Kismet2/DebuggerCommands.h"
#include "LevelEditor.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/MessageDialog.h"
#include "UObject/StrongObjectPtr.h"

namespace PortableMapEditorPreview
{
namespace
{
TStrongObjectPtr<ULevelEditorPlaySettings> Settings;
TMap<TSharedPtr<FUICommandInfo>,FUIAction> OriginalActions;
TMap<TSharedPtr<FUICommandInfo>,FDelegateHandle> OwnedHandles;
FDelegateHandle EndHandle,InitializedHandle;
FString Status;
bool Owned=false;
void Cleanup(){FEditorDelegates::EndPIE.Remove(EndHandle);EndHandle.Reset();Settings.Reset();Owned=false;}
bool StartInMode(FString& Error,EPlayModeType Mode)
{
    if(!GEditor||Owned||GEditor->IsPlaySessionInProgress()||GEditor->IsPlaySessionRequestQueued())
    {Error=TEXT("Stop the current preview before starting another map");return false;}
    auto* Root=PortableMapAuthoring::FindRoot(GEditor->GetEditorWorldContext().World());
    if(!Root){Error=TEXT("Open or create a map first");return false;}
    const auto* Appearance=GetDefault<UPortableMapPreviewSettings>();
    if(!Appearance->Mesh.LoadSynchronous()) {Error=TEXT("The toolkit's Tanuki preview model is missing. Reinstall the complete authoring kit");return false;}
    for(const auto& Clip:{Appearance->Idle,Appearance->Walk,Appearance->Run,Appearance->Fall,Appearance->Glide,Appearance->ClimbIdle,Appearance->ClimbUp})
        if(!Clip.LoadSynchronous()){Error=TEXT("The toolkit's Tanuki preview animations are missing. Reinstall the complete authoring kit");return false;}
    TSharedPtr<IAssetViewport> Viewport;
    if(Mode==PlayMode_InViewPort)
    {
        Viewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        if(!Viewport.IsValid()||!FSlateApplication::Get().FindWidgetWindow(Viewport->AsWidget()).IsValid())
        {Error=TEXT("Show a level viewport before starting Selected Viewport preview");return false;}
    }
    // Validate the same scene that will be exported; the temporary file never becomes user content.
    const FString Validation=FPaths::ProjectSavedDir()/TEXT("PortableMaps/Validation")/(FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".tanukimap"));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Validation),true);
    ON_SCOPE_EXIT{IFileManager::Get().Delete(*Validation);};
    if(!PortableMapAuthoring::Export(Root,Validation,Error))return false;
    Settings.Reset(DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage()));
    Settings->SetPlayNetMode(PIE_Standalone);Settings->SetPlayNumberOfClients(1);Settings->SetRunUnderOneProcess(true);
    Settings->bLaunchSeparateServer=false;Settings->AutoRecompileBlueprints=false;
    FRequestPlaySessionParams Request;Request.EditorPlaySettings=Settings.Get();Request.bAllowOnlineSubsystem=false;
    Request.GameModeOverride=APortableMapAuthoringPreviewGameMode::StaticClass();
    if(Viewport.IsValid())Request.DestinationSlateViewport=Viewport;
    Owned=true;Status=TEXT("Starting Tanuki movement preview");
    EndHandle=FEditorDelegates::EndPIE.AddLambda([](bool){Cleanup();Status=TEXT("Preview stopped. Continue editing your map");});
    GEditor->RequestPlaySession(Request);
    UE_LOG(LogTemp,Display,TEXT("PORTABLE_AUTHORING_PREVIEW accepted=1 viewport=%d map=%s"),Viewport.IsValid(),*GetPathNameSafe(Root->GetWorld()));
    Error.Reset();return true;
}
}
bool StartEdited(FString& Error){return StartInMode(Error,GetDefault<ULevelEditorPlaySettings>()->LastExecutedPlayModeType);}
bool Start(const FString& File,FString& Error)
{
    if(!GEditor||GEditor->IsPlaySessionInProgress()||GEditor->IsPlaySessionRequestQueued()){Error=TEXT("Stop the current preview first");return false;}
    if(!OpenPortableMapEditorFile(File)){Error=TEXT("The map could not be opened for editing");return false;}
    return StartEdited(Error);
}
FString GetStatus(){return Status;}
void RegisterAuthoringPlay()
{
    if(!FPlayWorldCommands::IsRegistered())
    {
        if(!InitializedHandle.IsValid())InitializedHandle=FCoreDelegates::GetOnPostEngineInit().AddStatic(&RegisterAuthoringPlay);
        return;
    }
    FCoreDelegates::GetOnPostEngineInit().Remove(InitializedHandle);InitializedHandle.Reset();
    const auto Actions=FPlayWorldCommands::GlobalPlayWorldActions;
    for(const auto& Command:{FPlayWorldCommands::Get().RepeatLastPlay,FPlayWorldCommands::Get().PlayInViewport,FPlayWorldCommands::Get().PlayInEditorFloating})
    {
        const auto* Existing=Actions->GetActionForCommand(Command);if(!Existing)continue;
        if(const auto* Handle=OwnedHandles.Find(Command);Handle&&Existing->ExecuteAction.GetHandle()==*Handle)continue;
        const FUIAction Original=*Existing;OriginalActions.Add(Command,Original);FUIAction Action=Original;
        Action.ExecuteAction=FExecuteAction::CreateLambda([Original,Command]
        {
            if(!GEditor||!PortableMapAuthoring::FindRoot(GEditor->GetEditorWorldContext().World())||GEditor->IsPlaySessionInProgress())
            {Original.ExecuteAction.ExecuteIfBound();return;}
            const EPlayModeType Mode=Command==FPlayWorldCommands::Get().PlayInViewport?PlayMode_InViewPort:
                Command==FPlayWorldCommands::Get().PlayInEditorFloating?PlayMode_InEditorFloating:GetDefault<ULevelEditorPlaySettings>()->LastExecutedPlayModeType.GetValue();
            FString Error;
            if(!StartInMode(Error,Mode)){Status=Error;FMessageDialog::Open(EAppMsgType::Ok,FText::AsCultureInvariant(Error));return;}
            auto* Saved=GetMutableDefault<ULevelEditorPlaySettings>();Saved->LastExecutedPlayModeType=Mode;
            FPropertyChangedEvent Changed(ULevelEditorPlaySettings::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(ULevelEditorPlaySettings,LastExecutedPlayModeType)));
            Saved->PostEditChangeProperty(Changed);Saved->SaveConfig();
        });
        OwnedHandles.Add(Command,Action.ExecuteAction.GetHandle());Actions->UnmapAction(Command);Actions->MapAction(Command,Action);
    }
}
void Shutdown()
{
    Cleanup();FCoreDelegates::GetOnPostEngineInit().Remove(InitializedHandle);InitializedHandle.Reset();
    if(FPlayWorldCommands::GlobalPlayWorldActions.IsValid())for(const auto& Entry:OriginalActions)
        if(const auto* Current=FPlayWorldCommands::GlobalPlayWorldActions->GetActionForCommand(Entry.Key))
            if(const auto* Handle=OwnedHandles.Find(Entry.Key);Handle&&Current->ExecuteAction.GetHandle()==*Handle)
            {FPlayWorldCommands::GlobalPlayWorldActions->UnmapAction(Entry.Key);FPlayWorldCommands::GlobalPlayWorldActions->MapAction(Entry.Key,Entry.Value);}
    OriginalActions.Reset();OwnedHandles.Reset();
}
}
