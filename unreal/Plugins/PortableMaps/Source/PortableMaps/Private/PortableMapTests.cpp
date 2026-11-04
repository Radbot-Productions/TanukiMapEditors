#if WITH_DEV_AUTOMATION_TESTS
#include "PortableMapScene.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortableMapManifestTest,"Radbot.PortableMaps.ManifestRejectsUnsupportedData",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPortableMapManifestTest::RunTest(const FString&)
{
    FString Error;
    const FString Valid=TEXT(R"({"schemaVersion":1,"mapId":"test","sceneSha1":"0000000000000000000000000000000000000000","meshes":[{"id":"mesh","mesh":0,"visible":true,"collision":false}],"colliders":[],"markers":[{"id":"start","type":"playerStart","position":[100,200,300],"rotation":[0,0,0,1],"spawnTag":""}]})");
    TestTrue(TEXT("Known schema accepted"),APortableMapScene::ValidateManifest(Valid,Error));
    TestFalse(TEXT("Unknown schema rejected"),APortableMapScene::ValidateManifest(Valid.Replace(TEXT("\"schemaVersion\":1"),TEXT("\"schemaVersion\":99")),Error));
    TestFalse(TEXT("New playable schema requires a boundary"),APortableMapScene::ValidateManifest(Valid.Replace(TEXT("\"schemaVersion\":1"),TEXT("\"schemaVersion\":2")),Error));
    TestFalse(TEXT("Unknown marker rejected"),APortableMapScene::ValidateManifest(Valid.Replace(TEXT("playerStart"),TEXT("/Game/UntrustedClass")),Error));
    TestFalse(TEXT("Duplicate IDs rejected"),APortableMapScene::ValidateManifest(Valid.Replace(TEXT("\"id\":\"start\""),TEXT("\"id\":\"mesh\"")),Error));
    TestFalse(TEXT("Bad rotation rejected"),APortableMapScene::ValidateManifest(Valid.Replace(TEXT("[0,0,0,1]"),TEXT("[0,0,0,0]")),Error));
    TestFalse(TEXT("Fractional mesh index rejected"),APortableMapScene::ValidateManifest(Valid.Replace(TEXT("\"mesh\":0"),TEXT("\"mesh\":0.5")),Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortableMapUnityImportTest,"Radbot.PortableMaps.UnityExportImport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPortableMapUnityImportTest::RunTest(const FString&)
{
    FString Directory;
    if (!FParse::Value(FCommandLine::Get(),TEXT("PortableMapTest="),Directory)) { AddError(TEXT("Pass -PortableMapTest=<Unity sample export directory>; this test must not silently skip")); return false; }
    const UWorld::InitializationValues Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("PortableMapImportTest"),nullptr,true,ERHIFeatureLevel::Num,&Values);
    APortableMapScene* Scene=World->SpawnActor<APortableMapScene>();
    FString Error;
    const bool bLoaded=Scene->LoadDirectory(Directory,Error);
    TestTrue(*FString::Printf(TEXT("Actual Unity GLB imports: %s"),*Error),bLoaded);
    if (bLoaded)
    {
        TestEqual(TEXT("Two Unity prefab starts"),Scene->GetMarkers().Num(),2);
        if (Scene->GetMarkers().Num()==2)
        {
            TestTrue(TEXT("Unity marker axes and metres convert to UE centimetres"),Scene->GetMarkers()[0].Transform.GetLocation().Equals(FVector(-300,-100,100),.01));
            TestTrue(TEXT("Unity forward becomes UE forward"),Scene->GetMarkers()[0].Transform.GetRotation().GetForwardVector().Equals(FVector::ForwardVector,.001));
            TestTrue(TEXT("Rotated marker points along UE Y"),Scene->GetMarkers()[1].Transform.GetRotation().GetForwardVector().Equals(FVector::RightVector,.001));
        }
        TArray<UStaticMeshComponent*> Meshes; Scene->GetComponents(Meshes);
        TestEqual(TEXT("Visual and separate triangle-collider meshes"),Meshes.Num(),9);
        UStaticMeshComponent* Floor=nullptr;
        for (UStaticMeshComponent* Mesh : Meshes) if (Mesh->ComponentHasTag(TEXT("mesh-0"))) Floor=Mesh;
        TestNotNull(TEXT("Unity floor identified by its export ID"),Floor);
        if (Floor)
        {
            const FVector FloorSize=Floor->GetStaticMesh()->GetBounds().GetBox().GetSize();
            TestTrue(*FString::Printf(TEXT("Floor GLB geometry axes/units (actual %s)"),*FloorSize.ToString()),FloorSize.Equals(FVector(1200,1200,50),.1));
            TestNotNull(TEXT("Imported material exists"),Floor->GetMaterial(0));
            UTexture* Texture=nullptr;
            const UMaterialInterface* Material=Floor->GetMaterial(0);
            const bool bTexture=Material && Material->GetTextureParameterValue(FMaterialParameterInfo(TEXT("baseColorTexture")),Texture) && Texture;
            TestTrue(TEXT("Unity checker image is bound on the imported runtime material"),bTexture);
        }
        Scene->SetContentActive(true);
        FHitResult Hit; FCollisionQueryParams Query; Query.bTraceComplex=true;
        const bool bFloor=World->LineTraceSingleByChannel(Hit,FVector(0,0,200),FVector(0,0,-100),ECC_Visibility,Query);
        TestTrue(TEXT("Exported floor collision answers runtime world trace"),bFloor);
        if (bFloor) TestTrue(TEXT("Floor top matches Unity y=0"),FMath::IsNearlyZero(Hit.ImpactPoint.Z,.1));
        const bool bRamp=World->LineTraceSingleByChannel(Hit,FVector(100,-200,200),FVector(100,-200,-100),ECC_Visibility,Query);
        TestTrue(*FString::Printf(TEXT("Triangle-mesh ramp world trace (hit=%d point=%s component=%s)"),bRamp,*Hit.ImpactPoint.ToString(),*GetNameSafe(Hit.GetComponent())),bRamp && Hit.ImpactPoint.Z>40 && Hit.ImpactPoint.Z<80);
        const FString Content=Scene->GetContentId();
        TestFalse(TEXT("Failed replacement rejected"),Scene->LoadDirectory(Directory/TEXT("missing"),Error));
        TestEqual(TEXT("Failed replacement preserves content identity"),Scene->GetContentId(),Content);
        TestTrue(TEXT("Existing content reloads"),Scene->LoadDirectory(Directory,Error));
        TestTrue(TEXT("Single-file Unity map package loads"),Scene->LoadFile(Directory/TEXT("map.tanukimap"),Error));
        TestEqual(TEXT("Single file and folder have identical content identity"),Scene->GetContentId(),Content);
        TArray<UStaticMeshComponent*> Reloaded; Scene->GetComponents(Reloaded);
        TestEqual(TEXT("Reload does not accumulate components"),Reloaded.Num(),9);
        Scene->SetContentActive(false);
        TestFalse(TEXT("Inactive scene releases collision"),World->LineTraceSingleByChannel(Hit,FVector(0,0,200),FVector(0,0,-100),ECC_Visibility,Query));
        Scene->Clear(); TestFalse(TEXT("Clear retires scene"),Scene->IsLoaded());
    }
    World->DestroyWorld(false);
    return !HasAnyErrors();
}
#endif
