#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "PortableMapPreviewGameMode.generated.h"

/** Standalone diagnostic viewer. This does not replace a game's session/match coordinator. */
UCLASS()
class PORTABLEMAPS_API APortableMapPreviewGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    APortableMapPreviewGameMode();
    virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
};
