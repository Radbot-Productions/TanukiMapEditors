#if WITH_DEV_AUTOMATION_TESTS
#include "PortableMapAuthoring.h"
#include "PortableMapScene.h"
#include "Components/LightComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortableLightEditorRoundTripTest,"Radbot.PortableMaps.LightEditorRoundTrip",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPortableLightEditorRoundTripTest::RunTest(const FString&)
{
    const auto* Variable=IConsoleManager::Get().FindConsoleVariable(TEXT("PortableMaps.LightTestFile"));
    const FString File=Variable?Variable->GetString():TEXT("");if(File.IsEmpty()){AddError(TEXT("Set PortableMaps.LightTestFile first"));return false;}
    const FString Folder=TEXT("/Game/Development/Automation/PortableLight_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UWorld* World=nullptr;FString Error;
    const bool Imported=PortableMapAuthoring::Import(File,Folder,false,World,Error,false);
    if(TestTrue(*FString::Printf(TEXT("Import Blender light map: %s"),*Error),Imported))
    {
        auto* Root=PortableMapAuthoring::FindRoot(World);int32 Count=0;
        TArray<AActor*> Children;Root->GetAttachedActors(Children,true,true);
        for(auto* Actor:Children){TArray<ULightComponent*> Lights;Actor->GetComponents(Lights);Count+=Lights.Num();}
        TestEqual(TEXT("Three editable lights under map root"),Count,3);
        const FString Output=FPaths::GetPath(File)/TEXT("lights-unreal.tanukimap");
        const bool Exported=PortableMapAuthoring::Export(Root,Output,Error);
        TestTrue(*FString::Printf(TEXT("Export light map: %s"),*Error),Exported);
        FPortableMapFileInfo Info;if(Exported)TestTrue(TEXT("Runtime accepts editor reexport"),APortableMapScene::InspectFile(Output,Info,Error));
    }
    if(World)World->DestroyWorld(false);
    // Only packages created by this exact fixture are retired. No disk asset is saved.
    for(TObjectIterator<UObject> It;It;++It) if(It->GetOutermost()->GetName().StartsWith(Folder+TEXT("/")))
    {It->ClearFlags(RF_Public|RF_Standalone);It->GetOutermost()->SetDirtyFlag(false);}
    return !HasAnyErrors();
}
#endif
