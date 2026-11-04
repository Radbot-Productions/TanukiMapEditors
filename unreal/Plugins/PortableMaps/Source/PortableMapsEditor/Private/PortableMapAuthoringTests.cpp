#if WITH_DEV_AUTOMATION_TESTS
#include "PortableMapAuthoring.h"
#include "PortableMapScene.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortableMapEditorRoundTripTest,"Radbot.PortableMaps.EditorRoundTrip",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPortableMapEditorRoundTripTest::RunTest(const FString&)
{
    FString Error; UWorld* World=nullptr;
    const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("PortableMaps"));
    if(!Plugin.IsValid()) {AddError(TEXT("PortableMaps plugin is not registered")); return false;}
    const FString Input=Plugin->GetBaseDir()/TEXT("Tests/Fixtures/roundtrip.tanukimap");
    IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("Automation/MapImport")),true);
    const FString Folder=TEXT("/Game/Development/Automation/PortableMapImport_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if(!TestTrue(TEXT("Import supported metadata map into new editable assets"),PortableMapAuthoring::Import(Input,Folder,false,World,Error))) {AddError(Error); return false;}
    auto* Root=PortableMapAuthoring::FindRoot(World); if(!TestNotNull(TEXT("Editable map root"),Root)) return false;
    TestEqual(TEXT("Continue stable identity"),Root->MapId,FString(TEXT("roundtrip-proof")));
    int32 Meshes=0,Starts=0; AStaticMeshActor* Visible=nullptr;
    for(TActorIterator<AStaticMeshActor> It(World);It;++It) {Meshes++; if(!It->ActorHasTag(TEXT("PortableCollisionOnly"))) Visible=*It; TestNotNull(TEXT("Durable mesh source description"),It->GetStaticMeshComponent()->GetStaticMesh()->GetMeshDescription(0));}
    for(TActorIterator<APlayerStart> It(World);It;++It) Starts++;
    TestEqual(TEXT("Render and collision mesh actors"),Meshes,2); TestEqual(TEXT("Player starts"),Starts,1);
    const FString Output=FPaths::ProjectSavedDir()/TEXT("Automation/MapImport/unreal-reexport.tanukimap");
    if(!TestTrue(TEXT("Export editable assets as one runtime map"),PortableMapAuthoring::Export(Root,Output,Error))) {AddError(Error); return false;}
    FPortableMapFileInfo Info; TestTrue(TEXT("Runtime accepts exported file"),APortableMapScene::InspectFile(Output,Info,Error)); TestEqual(TEXT("Export retains start"),Info.PlayerStarts,1);
    if(Visible) Visible->AddActorWorldOffset(FVector(100,0,0));
    TestTrue(TEXT("Export after normal actor edit"),PortableMapAuthoring::Export(Root,FPaths::ProjectSavedDir()/TEXT("Automation/MapImport/unreal-edited.tanukimap"),Error));
    UWorld* Duplicate=nullptr; TestFalse(TEXT("Existing asset folder is never overwritten"),PortableMapAuthoring::Import(Input,Folder,false,Duplicate,Error));
    return !HasAnyErrors();
}
#endif
