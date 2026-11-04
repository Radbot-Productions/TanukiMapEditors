#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "PortableMapPreviewCharacter.generated.h"

class USkeletalMesh;
class UAnimSequence;
class USpringArmComponent;
class UCameraComponent;
class APortableMapAuthoringBoundary;

/** Editor-preview assets and normal Tanuki tuning. No game-project or ability-system dependency. */
UCLASS(Config=Game,DefaultConfig)
class PORTABLEMAPSEDITOR_API UPortableMapPreviewSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UPROPERTY(Config,EditAnywhere,Category="Onboarding") bool bOpenEditorOnStartup=false;
    UPROPERTY(Config,EditAnywhere,Category="Character") TSoftObjectPtr<USkeletalMesh> Mesh;
    UPROPERTY(Config,EditAnywhere,Category="Character") FVector MeshOffset=FVector(0,0,-30);
    UPROPERTY(Config,EditAnywhere,Category="Character") FRotator MeshRotation=FRotator(0,270,0);
    UPROPERTY(Config,EditAnywhere,Category="Character") FVector MeshScale=FVector(.5);
    UPROPERTY(Config,EditAnywhere,Category="Animation") TSoftObjectPtr<UAnimSequence> Idle;
    UPROPERTY(Config,EditAnywhere,Category="Animation") TSoftObjectPtr<UAnimSequence> Walk;
    UPROPERTY(Config,EditAnywhere,Category="Animation") TSoftObjectPtr<UAnimSequence> Run;
    UPROPERTY(Config,EditAnywhere,Category="Animation") TSoftObjectPtr<UAnimSequence> Fall;
    UPROPERTY(Config,EditAnywhere,Category="Animation") TSoftObjectPtr<UAnimSequence> Glide;
    UPROPERTY(Config,EditAnywhere,Category="Animation") TSoftObjectPtr<UAnimSequence> ClimbIdle;
    UPROPERTY(Config,EditAnywhere,Category="Animation") TSoftObjectPtr<UAnimSequence> ClimbUp;
    UPROPERTY(Config,EditAnywhere,Category="Movement") float CapsuleRadius=21;
    UPROPERTY(Config,EditAnywhere,Category="Movement") float CapsuleHalfHeight=30;
    UPROPERTY(Config,EditAnywhere,Category="Movement") float WalkSpeed=270;
    UPROPERTY(Config,EditAnywhere,Category="Movement") float RunSpeed=450;
    UPROPERTY(Config,EditAnywhere,Category="Movement") float JumpSpeed=700;
    UPROPERTY(Config,EditAnywhere,Category="Movement") float ClimbSpeed=127.5;
    UPROPERTY(Config,EditAnywhere,Category="Movement") float Acceleration=2048;
    UPROPERTY(Config,EditAnywhere,Category="Movement") float Braking=2000;
    UPROPERTY(Config,EditAnywhere,Category="Movement") float AirBraking=1500;
    UPROPERTY(Config,EditAnywhere,Category="Movement") float AirControl=.35f;
    UPROPERTY(Config,EditAnywhere,Category="Movement") float GroundFriction=8;
    UPROPERTY(Config,EditAnywhere,Category="Movement") float BrakingFrictionFactor=2;
    UPROPERTY(Config,EditAnywhere,Category="Glide") float GlideSpeed=1000;
    UPROPERTY(Config,EditAnywhere,Category="Glide") float GlideDescent=150;
    UPROPERTY(Config,EditAnywhere,Category="Glide") float GlideHoldDelay=.2f;
    UPROPERTY(Config,EditAnywhere,Category="Glide") float GlideMinimumHeight=100;
    UPROPERTY(Config,EditAnywhere,Category="Climbing") float MaximumClimbAngle=140;
};

/** Standalone authoring preview only. Scripts, controller and visuals never enter portable map data. */
UCLASS(NotBlueprintable)
class PORTABLEMAPSEDITOR_API APortableMapPreviewCharacter : public ACharacter
{
    GENERATED_BODY()
public:
    APortableMapPreviewCharacter();
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
    void SetMoveInput(FVector2D Input);
    bool CanPreviewJump() const;
    bool CanPreviewGlide() const;
    void SetPreviewJumpHeld(bool Held);
    bool RequestPreviewJump();
    void ReleasePreviewJump();
    void SetPreviewSprint(bool bEnabled);
    bool IsClimbing() const { return bClimbing; }
    bool IsGliding() const { return bGliding; }
protected:
    virtual void BeginPlay() override;
private:
    UPROPERTY() TObjectPtr<USpringArmComponent> Boom;
    UPROPERTY() TObjectPtr<UCameraComponent> Camera;
    UPROPERTY() TMap<FName,TObjectPtr<UAnimSequence>> Clips;
    UPROPERTY() TObjectPtr<UAnimSequence> CurrentClip;
    UPROPERTY() TWeakObjectPtr<APortableMapAuthoringBoundary> Boundary;
    FVector2D MoveInput=FVector2D::ZeroVector;
    FVector WallNormal=FVector::ZeroVector,MantleTarget=FVector::ZeroVector;
    bool bClimbing=false,bGliding=false,bJumpHeld=false,bSprint=false,bMantling=false;
    float JumpHeldSeconds=0,ClimbCooldown=0;
    float ForwardKey=0,BackKey=0,LeftKey=0,RightKey=0;
    void Forward(float Value);void Back(float Value);void Left(float Value);void Right(float Value);
    void ForwardPressed(){Forward(1);} void ForwardReleased(){Forward(0);}
    void BackPressed(){Back(1);} void BackReleased(){Back(0);}
    void LeftPressed(){Left(1);} void LeftReleased(){Left(0);}
    void RightPressed(){Right(1);} void RightReleased(){Right(0);}
    void LookYaw(float Value);void LookPitch(float Value);
    void JumpPressed();void SprintPressed();void SprintReleased();
    bool FindWall(FVector Direction,FHitResult& Hit) const;
    bool TryMantle();
    void EndClimb();
    void UpdateAnimation();
    void ConstrainBoundary();
};

/** Uses authored PlayerStarts and the stock game-mode spawn executor in an isolated PIE world. */
UCLASS(NotBlueprintable)
class PORTABLEMAPSEDITOR_API APortableMapAuthoringPreviewGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    APortableMapAuthoringPreviewGameMode();
};
