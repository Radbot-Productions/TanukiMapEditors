#include "Modules/ModuleManager.h"
#include "PortableMapAuthoring.h"
#include "Editor.h"
#include "HAL/IConsoleManager.h"
#include "PortableMapEditorWindow.h"
#include "PortableMapPreviewCharacter.h"
#include "Misc/ConfigCacheIni.h"
#include "ToolMenus.h"
static FAutoConsoleCommand ImportMap(TEXT("PortableMaps.Authoring.Import"),TEXT("Import <map.tanukimap> <new /Game/folder> [Fork]. Creates assets without replacing the active editor world."),FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
{if(Args.Num()<2) return; UWorld* World=nullptr; FString Error; bool OK=PortableMapAuthoring::Import(Args[0],Args[1],Args.Contains(TEXT("Fork")),World,Error); UE_LOG(LogTemp,Display,TEXT("PORTABLE_EDITOR_IMPORT success=%d world=%s error=%s"),OK,*GetPathNameSafe(World),*Error);}));
static FAutoConsoleCommand ExportMap(TEXT("PortableMaps.Authoring.Export"),TEXT("Export the active editor map's authoring root to <map.tanukimap>."),FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
{if(Args.IsEmpty() || !GEditor) return; FString Error; bool OK=PortableMapAuthoring::Export(PortableMapAuthoring::FindRoot(GEditor->GetEditorWorldContext().World()),Args[0],Error); UE_LOG(LogTemp,Display,TEXT("PORTABLE_EDITOR_EXPORT success=%d error=%s"),OK,*Error);}));
class FPortableMapsEditorModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        if(IsRunningCommandlet())return;
        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this,&FPortableMapsEditorModule::RegisterWindow));
    }
    void RegisterWindow()
    {
        RegisterPortableMapEditorWindow();
        bool Shown=false;GConfig->GetBool(TEXT("Tanuki.MapEditor"),TEXT("WelcomeShown"),Shown,GEditorPerProjectIni);
        if(GetDefault<UPortableMapPreviewSettings>()->bOpenEditorOnStartup&&!Shown)
        {OpenPortableMapEditorWindow();GConfig->SetBool(TEXT("Tanuki.MapEditor"),TEXT("WelcomeShown"),true,GEditorPerProjectIni);GConfig->Flush(false,GEditorPerProjectIni);}
    }
    virtual void ShutdownModule() override
    {
        UToolMenus::UnRegisterStartupCallback(this);
        if(!IsRunningCommandlet())UnregisterPortableMapEditorWindow();
    }
};
IMPLEMENT_MODULE(FPortableMapsEditorModule,PortableMapsEditor)
