#include "PortableMapPreviewGameMode.h"
#include "PortableMapScene.h"
#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/PlayerStart.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

APortableMapPreviewGameMode::APortableMapPreviewGameMode() { DefaultPawnClass=ADefaultPawn::StaticClass(); }
void APortableMapPreviewGameMode::InitGame(const FString& MapName,const FString& Options,FString& ErrorMessage)
{
    Super::InitGame(MapName,Options,ErrorMessage);
    FString Directory;
    if (!FParse::Value(FCommandLine::Get(),TEXT("PortableMap="),Directory)) { ErrorMessage=TEXT("Pass -PortableMap=<export directory>"); return; }
    auto* Scene=GetWorld()->SpawnActor<APortableMapScene>();
    const bool bLoaded=Scene && (FPaths::FileExists(Directory) ? Scene->LoadFile(Directory,ErrorMessage) : Scene->LoadDirectory(Directory,ErrorMessage));
    if (!bLoaded)
    {
        UE_LOG(LogTemp,Error,TEXT("PORTABLE_MAP_PREVIEW_FAILED %s"),*ErrorMessage); return;
    }
    Scene->SetContentActive(true);
    for (const auto& Marker : Scene->GetMarkers())
    {
        auto* Start=GetWorld()->SpawnActor<APlayerStart>(APlayerStart::StaticClass(),Marker.Transform);
        if (Start) Start->PlayerStartTag=Marker.SpawnTag;
    }
    auto* Light=GetWorld()->SpawnActor<ADirectionalLight>();
    Light->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    Light->SetActorRotation(FRotator(-45,-30,0));
    Light->GetLightComponent()->SetIntensity(5.f);
    UE_LOG(LogTemp,Display,TEXT("PORTABLE_MAP_PREVIEW_READY %s"),*Scene->GetContentId());
}
