#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "PortableMapEnvironment.h"
#include "PortableMapAuthoring.h"
#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "Components/LightComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortableEnvironmentTest,"Radbot.PortableMaps.Editor.EnvironmentProfiles",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPortableEnvironmentTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Inactive,false);
    auto* Root=World->SpawnActor<APortableMapAuthoringRoot>();World->WorldType=EWorldType::Editor;
    for(const auto* Key:{TEXT("cartoon-street-day"),TEXT("cartoon-street-night"),TEXT("cartoon-street-day")})
    {
        Root->BaseLighting=Key;PortableMapEnvironment::Configure(Root);
        TArray<AActor*> Actors;Root->GetAttachedActors(Actors);int32 Suns=0,Skies=0,References=0;
        for(auto* Actor:Actors)if(IsValid(Actor))
        {
            if(Actor->Tags.Contains(TEXT("Tanuki.Authoring.ScaleReference")))++References;
            if(Actor->Tags.Contains(TEXT("Tanuki.Authoring.Environment")))
            {
                TestTrue(TEXT("Helper follows selected profile"),Actor->Tags.Contains(FName(Key)));
                if(auto* Sun=Cast<ADirectionalLight>(Actor)){++Suns;TestEqual(TEXT("Profile uses physical sun intensity"),Sun->GetLightComponent()->Intensity,Root->BaseLighting.EndsWith(TEXT("night"))?2.f:6.f);}
                if(Actor->GetActorLabel()==TEXT("Skybox")){++Skies;TestNotNull(TEXT("Skybox has native material"),CastChecked<AStaticMeshActor>(Actor)->GetStaticMeshComponent()->GetMaterial(0));}
            }
        }
        TestEqual(TEXT("One base sun"),Suns,1);TestEqual(TEXT("One linked skybox"),Skies,1);TestEqual(TEXT("Both scale references"),References,2);
    }
    World->WorldType=EWorldType::Inactive;World->DestroyWorld(false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortableReferencePlacementTest,"Radbot.PortableMaps.Editor.ReferencePlacement",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPortableReferencePlacementTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Inactive,false);
    auto* Root=World->SpawnActor<APortableMapAuthoringRoot>();
    Root->SetActorTransform(FTransform(FRotator(0,37,0),FVector(1500,-700,125),FVector(1.2,.8,1)));
    auto* Floor=World->SpawnActor<AStaticMeshActor>();
    Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Floor->AttachToActor(Root,FAttachmentTransformRules::KeepRelativeTransform);
    Floor->SetActorRelativeTransform(FTransform(FQuat::Identity,FVector(300,500,90),FVector(20,16,.2)));
    World->WorldType=EWorldType::Editor;
    const auto FloorBounds=PortableMapAuthoring::GetReferenceFloor(Root);
    TestTrue(TEXT("Rotated map keeps exact floor bounds in map space"),FloorBounds.Min.Equals(FVector(-700,-300,80),.1)&&FloorBounds.Max.Equals(FVector(1300,1300,100),.1));
    for(int32 Pass=0;Pass<2;++Pass)
    {
        PortableMapAuthoring::ConfigureScaleReferences(Root);
        TArray<AActor*> Attached;Root->GetAttachedActors(Attached);TArray<FBox> Bounds;
        for(auto* Actor:Attached)if(IsValid(Actor)&&Actor->Tags.Contains(TEXT("Tanuki.Authoring.ScaleReference")))
        {
            auto* Reference=Cast<AStaticMeshActor>(Actor);
            TestNotNull(TEXT("Scale dummy is a baked static mesh"),Reference);if(!Reference)continue;
            const auto* Component=Reference->GetStaticMeshComponent();
            const auto Box=Component->GetStaticMesh()->GetBoundingBox().TransformBy(Component->GetComponentTransform().GetRelativeTransform(Root->GetActorTransform()).ToMatrixWithScale());
            TestTrue(TEXT("Feet sit on the floor"),FMath::Abs(Box.Min.Z-FloorBounds.Max.Z)<.1);
            TestTrue(TEXT("Dummy stays inside floor footprint"),Box.Min.X>=FloorBounds.Min.X&&Box.Max.X<=FloorBounds.Max.X&&Box.Min.Y>=FloorBounds.Min.Y&&Box.Max.Y<=FloorBounds.Max.Y);
            TestTrue(TEXT("Dummy stays near clear floor edge"),Box.Max.X<FloorBounds.GetCenter().X);
            Bounds.Add(Box);
        }
        TestEqual(TEXT("Repeated refresh retains exactly two references"),Bounds.Num(),2);
        if(Bounds.Num()==2)TestFalse(TEXT("Characters do not overlap"),Bounds[0].Intersect(Bounds[1]));
    }
    World->WorldType=EWorldType::Inactive;World->DestroyWorld(false);return true;
}
#endif
