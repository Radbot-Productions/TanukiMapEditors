#include "PortableMapPreviewCharacter.h"
#include "PortableMapAuthoring.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

APortableMapAuthoringPreviewGameMode::APortableMapAuthoringPreviewGameMode()
{
    DefaultPawnClass=APortableMapPreviewCharacter::StaticClass();
}
APortableMapPreviewCharacter::APortableMapPreviewCharacter()
{
    PrimaryActorTick.bCanEverTick=true;
    GetCapsuleComponent()->InitCapsuleSize(21,30);
    bUseControllerRotationYaw=false;
    Boom=CreateDefaultSubobject<USpringArmComponent>(TEXT("PreviewCameraBoom"));
    Boom->SetupAttachment(RootComponent);Boom->SetRelativeLocation(FVector(0,0,45.46));
    Boom->TargetArmLength=325;Boom->ProbeSize=8;Boom->bUsePawnControlRotation=true;
    Camera=CreateDefaultSubobject<UCameraComponent>(TEXT("PreviewCamera"));Camera->SetupAttachment(Boom);
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
void APortableMapPreviewCharacter::BeginPlay()
{
    Super::BeginPlay();
    const auto* Settings=GetDefault<UPortableMapPreviewSettings>();
    GetCapsuleComponent()->SetCapsuleSize(Settings->CapsuleRadius,Settings->CapsuleHalfHeight,true);
    auto* Movement=GetCharacterMovement();
    Movement->MaxWalkSpeed=Settings->WalkSpeed;Movement->JumpZVelocity=Settings->JumpSpeed;
    Movement->MaxAcceleration=Settings->Acceleration;Movement->BrakingDecelerationWalking=Settings->Braking;
    Movement->BrakingDecelerationFalling=Settings->AirBraking;Movement->AirControl=Settings->AirControl;
    Movement->GroundFriction=Settings->GroundFriction;Movement->BrakingFrictionFactor=Settings->BrakingFrictionFactor;
    Movement->MaxStepHeight=20;Movement->bOrientRotationToMovement=true;Movement->RotationRate=FRotator(0,540,0);
    Movement->SetWalkableFloorAngle(70);
    GetMesh()->SetSkeletalMesh(Settings->Mesh.LoadSynchronous());
    GetMesh()->SetRelativeLocationAndRotation(Settings->MeshOffset,Settings->MeshRotation);
    GetMesh()->SetRelativeScale3D(Settings->MeshScale);
    GetMesh()->SetDisablePostProcessBlueprint(true);
    const TPair<FName,TSoftObjectPtr<UAnimSequence>> Sources[]={
        {TEXT("Idle"),Settings->Idle},{TEXT("Walk"),Settings->Walk},{TEXT("Run"),Settings->Run},
        {TEXT("Fall"),Settings->Fall},{TEXT("Glide"),Settings->Glide},
        {TEXT("ClimbIdle"),Settings->ClimbIdle},{TEXT("ClimbUp"),Settings->ClimbUp}};
    for(const auto& Source:Sources) Clips.Add(Source.Key,Source.Value.LoadSynchronous());
    for(TActorIterator<APortableMapAuthoringBoundary> It(GetWorld());It;++It){Boundary=*It;break;}
    if(Controller) Controller->SetControlRotation(FRotator(-10,GetActorRotation().Yaw,0));
    UpdateAnimation();
}
void APortableMapPreviewCharacter::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    Input->BindKey(EKeys::W,IE_Pressed,this,&ThisClass::ForwardPressed);Input->BindKey(EKeys::W,IE_Released,this,&ThisClass::ForwardReleased);
    Input->BindKey(EKeys::S,IE_Pressed,this,&ThisClass::BackPressed);Input->BindKey(EKeys::S,IE_Released,this,&ThisClass::BackReleased);
    Input->BindKey(EKeys::A,IE_Pressed,this,&ThisClass::LeftPressed);Input->BindKey(EKeys::A,IE_Released,this,&ThisClass::LeftReleased);
    Input->BindKey(EKeys::D,IE_Pressed,this,&ThisClass::RightPressed);Input->BindKey(EKeys::D,IE_Released,this,&ThisClass::RightReleased);
    Input->BindAxisKey(EKeys::MouseX,this,&ThisClass::LookYaw);Input->BindAxisKey(EKeys::MouseY,this,&ThisClass::LookPitch);
    Input->BindKey(EKeys::SpaceBar,IE_Pressed,this,&ThisClass::JumpPressed);
    Input->BindKey(EKeys::SpaceBar,IE_Released,this,&ThisClass::ReleasePreviewJump);
    Input->BindKey(EKeys::LeftShift,IE_Pressed,this,&ThisClass::SprintPressed);
    Input->BindKey(EKeys::LeftShift,IE_Released,this,&ThisClass::SprintReleased);
}
void APortableMapPreviewCharacter::SetMoveInput(FVector2D Input){MoveInput=Input.GetClampedToMaxSize(1);}
void APortableMapPreviewCharacter::Forward(float V){ForwardKey=V;SetMoveInput(FVector2D(RightKey-LeftKey,ForwardKey-BackKey));}
void APortableMapPreviewCharacter::Back(float V){BackKey=V;SetMoveInput(FVector2D(RightKey-LeftKey,ForwardKey-BackKey));}
void APortableMapPreviewCharacter::Left(float V){LeftKey=V;SetMoveInput(FVector2D(RightKey-LeftKey,ForwardKey-BackKey));}
void APortableMapPreviewCharacter::Right(float V){RightKey=V;SetMoveInput(FVector2D(RightKey-LeftKey,ForwardKey-BackKey));}
void APortableMapPreviewCharacter::LookYaw(float V){AddControllerYawInput(V);}
void APortableMapPreviewCharacter::LookPitch(float V){AddControllerPitchInput(-V);}
void APortableMapPreviewCharacter::JumpPressed(){SetPreviewJumpHeld(true);if(CanPreviewJump())RequestPreviewJump();}
void APortableMapPreviewCharacter::SprintPressed(){SetPreviewSprint(true);}
void APortableMapPreviewCharacter::SprintReleased(){SetPreviewSprint(false);}
void APortableMapPreviewCharacter::SetPreviewSprint(bool Enabled){bSprint=Enabled;if(bSprint&&bClimbing)EndClimb();}
bool APortableMapPreviewCharacter::CanPreviewJump() const {return !bMantling&&(bClimbing||CanJump());}
bool APortableMapPreviewCharacter::RequestPreviewJump()
{
    if(!CanPreviewJump()) return false;
    SetPreviewJumpHeld(true);
    if(bClimbing){const FVector Away=WallNormal;EndClimb();LaunchCharacter(Away*200+FVector::UpVector*GetDefault<UPortableMapPreviewSettings>()->JumpSpeed,true,true);}
    else Jump();
    return true;
}
void APortableMapPreviewCharacter::SetPreviewJumpHeld(bool Held){bJumpHeld=Held;JumpHeldSeconds=0;}
void APortableMapPreviewCharacter::ReleasePreviewJump(){SetPreviewJumpHeld(false);StopJumping();}
bool APortableMapPreviewCharacter::CanPreviewGlide() const
{
    const auto* Settings=GetDefault<UPortableMapPreviewSettings>();
    if(bClimbing||!GetCharacterMovement()->IsFalling()||!bJumpHeld||JumpHeldSeconds<Settings->GlideHoldDelay)return false;
    FHitResult Ground;FCollisionQueryParams Query(SCENE_QUERY_STAT(PortablePreviewGround),false,this);
    return !GetWorld()->LineTraceSingleByChannel(Ground,GetActorLocation(),GetActorLocation()-FVector::UpVector*(Settings->GlideMinimumHeight+GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),ECC_Visibility,Query);
}
bool APortableMapPreviewCharacter::FindWall(FVector Direction,FHitResult& Hit) const
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(PortablePreviewWall),false,this);
    if(!GetWorld()->LineTraceSingleByChannel(Hit,GetActorLocation(),GetActorLocation()+Direction*(GetCapsuleComponent()->GetScaledCapsuleRadius()+22),ECC_Visibility,Query))return false;
    const float MinZ=FMath::Cos(FMath::DegreesToRadians(GetDefault<UPortableMapPreviewSettings>()->MaximumClimbAngle));
    return Hit.bBlockingHit&&Hit.ImpactNormal.Z>=MinZ&&Hit.ImpactNormal.Z<.7;
}
void APortableMapPreviewCharacter::EndClimb()
{
    bClimbing=false;bMantling=false;ClimbCooldown=.3f;
    GetCharacterMovement()->SetMovementMode(MOVE_Falling);GetCharacterMovement()->bOrientRotationToMovement=true;
}
bool APortableMapPreviewCharacter::TryMantle()
{
    const float Half=GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),Radius=GetCapsuleComponent()->GetScaledCapsuleRadius();
    const FVector Above=GetActorLocation()+FVector::UpVector*(Half*2+20)-WallNormal*(Radius*2+20);
    FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(PortablePreviewLedge),false,this);
    if(!GetWorld()->LineTraceSingleByChannel(Hit,Above,Above-FVector::UpVector*(Half*3+20),ECC_Visibility,Query)||Hit.ImpactNormal.Z<GetCharacterMovement()->GetWalkableFloorZ())return false;
    const FVector Target=Hit.ImpactPoint+FVector::UpVector*(Half+3);
    if(Target.Z<GetActorLocation().Z-Half||GetWorld()->OverlapBlockingTestByChannel(Target,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Radius,Half),Query))return false;
    MantleTarget=Target;bMantling=true;GetCharacterMovement()->StopMovementImmediately();return true;
}
void APortableMapPreviewCharacter::Tick(float Dt)
{
    Super::Tick(Dt);
    auto* Movement=GetCharacterMovement();const auto* Settings=GetDefault<UPortableMapPreviewSettings>();
    ClimbCooldown=FMath::Max(0.f,ClimbCooldown-Dt);
    if(bJumpHeld)JumpHeldSeconds+=Dt;
    const FVector Forward=FRotator(0,Controller?Controller->GetControlRotation().Yaw:GetActorRotation().Yaw,0).Vector();
    FHitResult Wall;
    if(bMantling)
    {
        FVector Target=MantleTarget;
        if(GetActorLocation().Z<MantleTarget.Z-1)Target=FVector(GetActorLocation().X,GetActorLocation().Y,MantleTarget.Z);
        FHitResult Block;SetActorLocation(GetActorLocation()+((Target-GetActorLocation()).GetClampedToMaxSize(270*Dt)),true,&Block);
        if(Block.bBlockingHit)EndClimb();else if(FVector::DistSquared(GetActorLocation(),MantleTarget)<4)EndClimb();
    }
    else
    {
        const bool Contact=FindWall(bClimbing?-WallNormal:Forward,Wall);
        if(!bClimbing&&!bSprint&&ClimbCooldown<=0&&MoveInput.Y>.1&&Contact)
        {bClimbing=true;WallNormal=Wall.ImpactNormal;Movement->StopMovementImmediately();Movement->SetMovementMode(MOVE_Flying);Movement->bOrientRotationToMovement=false;}
        if(bClimbing)
        {
            if(Contact)WallNormal=Wall.ImpactNormal;
            else if(!(MoveInput.Y>.1&&TryMantle()))EndClimb();
            if(bClimbing&&!bMantling)
            {
                const FVector Up=FVector::VectorPlaneProject(FVector::UpVector,WallNormal).GetSafeNormal();
                const FVector Right=FVector::CrossProduct(WallNormal,Up);
                Movement->MaxFlySpeed=Settings->ClimbSpeed;Movement->Velocity=(Up*MoveInput.Y+Right*MoveInput.X).GetClampedToMaxSize(1)*Settings->ClimbSpeed;
                SetActorRotation(FRotator(0,(-WallNormal).Rotation().Yaw,0));
            }
        }
        else
        {
            AddMovementInput(Forward,MoveInput.Y);AddMovementInput(FVector::CrossProduct(FVector::UpVector,Forward),MoveInput.X);
        }
    }
    bGliding=CanPreviewGlide();
    Movement->GravityScale=bGliding?0:1;Movement->AirControl=bGliding?1:Settings->AirControl;
    Movement->MaxWalkSpeed=bGliding?Settings->GlideSpeed:bSprint?Settings->RunSpeed:Settings->WalkSpeed;
    if(bGliding)Movement->Velocity.Z=FMath::FInterpTo(Movement->Velocity.Z,-Settings->GlideDescent,Dt,7);
    if(Movement->IsMovingOnGround())JumpHeldSeconds=0;
    ConstrainBoundary();UpdateAnimation();
}
void APortableMapPreviewCharacter::ConstrainBoundary()
{
    if(!Boundary.IsValid())return;
    const FTransform Frame=Boundary->GetActorTransform();const FVector Scale=Frame.GetScale3D().GetAbs();
    if(Scale.GetMin()<=UE_SMALL_NUMBER)return;
    FVector P=Frame.InverseTransformPosition(GetActorLocation());
    const float Radius=GetCapsuleComponent()->GetScaledCapsuleRadius(),Half=GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const FVector Up=Frame.InverseTransformVector(FVector::UpVector*(Half-Radius)).GetAbs();
    const FVector Margin=FVector(Radius)/Scale+Up;
    P.Z=FMath::Clamp(P.Z,-50+Margin.Z,50-Margin.Z);
    if(Boundary->Shape==EPortableMapBoundaryShape::Box){P.X=FMath::Clamp(P.X,-50+Margin.X,50-Margin.X);P.Y=FMath::Clamp(P.Y,-50+Margin.Y,50-Margin.Y);}
    else{FVector2D R(P.X,P.Y);R=R.GetClampedToMaxSize(FMath::Max(0.,50-FMath::Max(Margin.X,Margin.Y)));P.X=R.X;P.Y=R.Y;}
    const FVector Target=Frame.TransformPosition(P);if(!Target.Equals(GetActorLocation(),.01))SetActorLocation(Target,true);
}
void APortableMapPreviewCharacter::UpdateAnimation()
{
    const float Speed=GetVelocity().Size2D();
    const FName Key=bClimbing?(MoveInput.IsNearlyZero()?TEXT("ClimbIdle"):TEXT("ClimbUp")):bGliding?TEXT("Glide"):
        GetCharacterMovement()->IsFalling()?TEXT("Fall"):Speed<5?TEXT("Idle"):bSprint?TEXT("Run"):TEXT("Walk");
    auto* Clip=Clips.FindRef(Key).Get();if(!Clip||Clip==CurrentClip)return;
    CurrentClip=Clip;GetMesh()->PlayAnimation(Clip,true);
    if(auto* Instance=GetMesh()->GetSingleNodeInstance())Instance->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
}
