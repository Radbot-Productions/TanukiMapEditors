#if WITH_DEV_AUTOMATION_TESTS
#include "PortableMapAuthoring.h"
#include "PortableMapEditorWindow.h"
#include "PortableMapEditorPreview.h"
#include "PortableMapPreviewCharacter.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Tests/AutomationEditorCommon.h"

namespace
{
// Only run in a disposable, standalone kit project. This fixture never replaces
// a person's level or uses the private game's session/ability implementation.
class FKitPreviewCheck final : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    int32 Map=0,Step=0;
    double Since=0,Deadline=0;
    float StartX=0,StartZ=0,HighestJumpZ=0;
    bool SawGlide=false,SawClimb=false;
    TWeakObjectPtr<APortableMapPreviewCharacter> Character;
    void Next(int32 Value,double Limit=8){Step=Value;Since=FPlatformTime::Seconds();Deadline=Since+Limit;}
    bool Fail(const TCHAR* Message)
    {
        Test->AddError(Message);
        if(auto* C=Character.Get())Test->AddInfo(FString::Printf(TEXT("Pawn position=%s velocity=%s mode=%d climbing=%d gliding=%d worldTime=%.2f"),*C->GetActorLocation().ToString(),*C->GetVelocity().ToString(),int32(C->GetCharacterMovement()->MovementMode),C->IsClimbing(),C->IsGliding(),C->GetWorld()->TimeSeconds));
        if(GEditor->PlayWorld)GEditor->RequestEndPlayMap();return true;
    }
public:
    explicit FKitPreviewCheck(FAutomationTestBase* InTest):Test(InTest){}
    virtual bool Update() override
    {
        const double Now=FPlatformTime::Seconds();
        if(Step&&Now>Deadline)return Fail(*FString::Printf(TEXT("Kit preview timeout: map=%d step=%d"),Map,Step));
        if(Step==0)
        {
            FString Error;
            if(Map==0)
            {
                UWorld* World=FAutomationEditorCommonUtils::CreateNewMap();
                auto* Root=PortableMapAuthoring::CreateStarter(World);
                if(!Root)return Fail(TEXT("New Empty Map failed"));
                auto* Wall=World->SpawnActor<AStaticMeshActor>();
                Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
                Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
                for(TActorIterator<AStaticMeshActor> It(World);It;++It)
                    if(*It!=Wall){Wall->GetStaticMeshComponent()->SetMaterial(0,It->GetStaticMeshComponent()->GetMaterial(0));break;}
                Wall->AttachToActor(Root,FAttachmentTransformRules::KeepWorldTransform);
                Wall->SetActorTransform(FTransform(FQuat::Identity,FVector(600,0,150),FVector(1,5,3)));
            }
            else
            {
                const FString Sample=IPluginManager::Get().FindPlugin(TEXT("PortableMaps"))->GetBaseDir()/TEXT("Samples")/(Map==1?TEXT("Room.tanukimap"):TEXT("Courtyard.tanukimap"));
                if(!OpenPortableMapEditorFile(Sample))return Fail(TEXT("Sample could not open through the editor's production API"));
            }
            if(!PortableMapEditorPreview::StartEdited(Error))return Fail(*Error);
            FString DuplicateError;
            Test->TestFalse(TEXT("Pending preview rejects a duplicate launch"),PortableMapEditorPreview::StartEdited(DuplicateError));
            Next(1,45);return false;
        }
        if(Step==1)
        {
            if(!GEditor->PlayWorld)return false;
            for(TActorIterator<APortableMapPreviewCharacter> It(GEditor->PlayWorld);It;++It)Character=*It;
            if(!Character.IsValid()||!Character->GetCharacterMovement()->IsMovingOnGround())return false;
            auto* C=Character.Get();
            Test->TestTrue(TEXT("Tanuki model loaded from the public plugin"),C->GetMesh()->GetSkeletalMeshAsset()&&C->GetMesh()->GetSkeletalMeshAsset()->GetPathName().StartsWith(TEXT("/TanukiAuthoringPreview/")));
            Test->TestNotNull(TEXT("Tanuki animation is playing"),C->GetMesh()->GetSingleNodeInstance());
            Test->TestEqual(TEXT("Normal Tanuki capsule radius"),C->GetCapsuleComponent()->GetUnscaledCapsuleRadius(),21.f);
            Test->TestEqual(TEXT("Normal Tanuki capsule half height"),C->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(),30.f);
            if(Map){GEditor->RequestEndPlayMap();Next(9);return false;}
            C->SetActorLocation(FVector(0,0,32),false,nullptr,ETeleportType::TeleportPhysics);
            C->GetController()->SetControlRotation(FRotator::ZeroRotator);
            StartX=C->GetActorLocation().X;C->SetMoveInput(FVector2D(0,1));Next(2);return false;
        }
        auto* C=Character.Get();
        if(Step<9&&!C)return Fail(TEXT("Preview character disappeared"));
        if(Step==2&&Now-Since>.7)
        {
            Test->TestTrue(TEXT("Walk input advances the character"),C->GetActorLocation().X>StartX+80);
            Test->TestTrue(TEXT("Normal Tanuki walking speed"),FMath::IsNearlyEqual(C->GetVelocity().Size2D(),270.f,12.f));
            C->SetPreviewSprint(true);Next(3);return false;
        }
        if(Step==3&&Now-Since>.5)
        {
            Test->TestTrue(TEXT("Normal Tanuki running speed"),FMath::IsNearlyEqual(C->GetVelocity().Size2D(),450.f,15.f));
            C->SetPreviewSprint(false);C->SetMoveInput(FVector2D(0,-1));Next(4);return false;
        }
        if(Step==4&&Now-Since>.5)
        {
            Test->TestTrue(TEXT("Reversal responds within half a second"),C->GetVelocity().X<-200);
            C->SetMoveInput(FVector2D::ZeroVector);
            const bool Ready=C->CanPreviewJump();const bool Accepted=C->RequestPreviewJump();
            Test->TestEqual(TEXT("Jump readiness matches production command admission"),Accepted,Ready);
            if(!Accepted)return Fail(TEXT("Grounded jump was unavailable"));
            StartZ=HighestJumpZ=C->GetActorLocation().Z;Next(5);return false;
        }
        if(Step==5)
        {
            SawGlide|=C->IsGliding();
            HighestJumpZ=FMath::Max(HighestJumpZ,C->GetActorLocation().Z);
            if(Now-Since<1.2)return false;
            Test->TestTrue(TEXT("Jump gains height"),HighestJumpZ>StartZ+100);
            Test->TestTrue(TEXT("Holding jump engages glide"),SawGlide);
            C->ReleasePreviewJump();Next(6);return false;
        }
        if(Step==6)
        {
            if(Now-Since<.15)return false;
            Test->TestFalse(TEXT("Releasing jump disengages glide"),C->IsGliding());
            C->GetCharacterMovement()->StopMovementImmediately();
            C->SetActorLocation(FVector(520,0,32),false,nullptr,ETeleportType::TeleportPhysics);
            C->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
            C->GetController()->SetControlRotation(FRotator::ZeroRotator);
            C->SetMoveInput(FVector2D(0,1));StartZ=C->GetActorLocation().Z;Next(7,12);return false;
        }
        if(Step==7)
        {
            SawClimb|=C->IsClimbing();
            if(Now-Since<1.2)return false;
            Test->TestTrue(TEXT("Movement attaches to the wall"),SawClimb);
            Test->TestTrue(TEXT("Climbing gains height"),C->GetActorLocation().Z>StartZ+60);
            Next(8,12);return false;
        }
        if(Step==8)
        {
            if(C->IsClimbing()||!C->GetCharacterMovement()->IsMovingOnGround())return false;
            Test->TestTrue(TEXT("Ledge transition lands above the wall"),C->GetActorLocation().Z>320);
            C->SetMoveInput(FVector2D::ZeroVector);GEditor->RequestEndPlayMap();Next(9);return false;
        }
        if(Step==9)
        {
            if(GEditor->PlayWorld||GEditor->IsPlaySessionRequestQueued())return false;
            Character.Reset();if(++Map<3){Step=0;return false;}
            return true;
        }
        return false;
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortableMapKitPreviewTest,"Tanuki.Editor.AuthoringKit.StandaloneMovement",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPortableMapKitPreviewTest::RunTest(const FString&)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("PortableAuthoringKitTest"))||FModuleManager::Get().IsModuleLoaded(TEXT("Tanuki")))
    {AddError(TEXT("Requires an isolated public kit project and -PortableAuthoringKitTest; never run in the private game editor"));return false;}
    if(GEditor->PlayWorld||GEditor->IsPlaySessionRequestQueued()){AddError(TEXT("Caller-owned PIE is active"));return false;}
    auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.ScanPathsSynchronous({TEXT("/TanukiAuthoringPreview")},true);
    TArray<FAssetData> Assets;Registry.GetAssetsByPath(TEXT("/TanukiAuthoringPreview"),Assets,true);
    TestEqual(TEXT("Eleven movement-only preview assets ship"),Assets.Num(),11);
    for(const auto& Asset:Assets)
    {
        TArray<FName> Dependencies;Registry.GetDependencies(Asset.PackageName,Dependencies);
        for(const auto Dependency:Dependencies)
        {
            const FString Path=Dependency.ToString();
            TestTrue(*FString::Printf(TEXT("Public asset %s has a standalone dependency: %s"),*Asset.AssetName.ToString(),*Path),
                Path.StartsWith(TEXT("/TanukiAuthoringPreview/"))||Path.StartsWith(TEXT("/Engine/"))||Path.StartsWith(TEXT("/Script/"))||Path.StartsWith(TEXT("/glTFRuntime/")));
        }
    }
    ADD_LATENT_AUTOMATION_COMMAND(FKitPreviewCheck(this));return true;
}
#endif
