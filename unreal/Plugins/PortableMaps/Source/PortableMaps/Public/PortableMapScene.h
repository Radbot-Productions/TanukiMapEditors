#pragma once

#include "CoreMinimal.h"
#include "PortableMapLighting.h"
#include "GameFramework/Actor.h"
#include "Engine/DataAsset.h"
#include "PortableMapScene.generated.h"

class UglTFRuntimeAsset;
class UPrimitiveComponent;
class APlayerStart;
class UPortableMapBuildTask;
class ULightComponent;

USTRUCT(BlueprintType)
struct PORTABLEMAPS_API FPortableMapBoundary
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly,Category="Portable Map") bool bPresent=false;
    UPROPERTY(BlueprintReadOnly,Category="Portable Map") FString Shape;
    UPROPERTY(BlueprintReadOnly,Category="Portable Map") FTransform Frame;
    UPROPERTY(BlueprintReadOnly,Category="Portable Map") FVector Size=FVector::ZeroVector;
};

struct PORTABLEMAPS_API FPortableMapFileInfo
{
    FString ContentId;
    FString BaseLighting=TEXT("cartoon-street-day");
    FPortableMapSun BaseSun;
    FString FileHash;
    int32 PlayerStarts = 0;
};

/** Only supported fields cross the authoring boundary. Never deserialize arbitrary UObject properties. */
USTRUCT(BlueprintType)
struct PORTABLEMAPS_API FPortableMapMarker
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly,Category="Portable Map") FString Id;
    UPROPERTY(BlueprintReadOnly,Category="Portable Map") FName Type;
    UPROPERTY(BlueprintReadOnly,Category="Portable Map") FTransform Transform;
    UPROPERTY(BlueprintReadOnly,Category="Portable Map") FName SpawnTag;
};

/** Project-authored, cook-visible class mapping. External packages contain no Unreal class/asset paths. */
UCLASS(BlueprintType)
class PORTABLEMAPS_API UPortableMapCatalog : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly,Category="Portable Map") TSubclassOf<APlayerStart> PlayerStartClass;
};

/** Owns a flat, static GLB scene and its collider components. Loading is transactional; failure keeps the old scene. */
UCLASS(BlueprintType)
class PORTABLEMAPS_API APortableMapScene : public AActor
{
    GENERATED_BODY()
public:
    APortableMapScene();
    static void SetAccessPolicy(TFunction<bool(const UObject*)> Policy);
    static bool IsAccessAllowed(const UObject* Context);
    /** Prototype accepts a directory containing map.json and scene.glb. Does not execute scripts. */
    UFUNCTION(BlueprintCallable, Category="Portable Map")
    bool LoadDirectory(const FString& Directory, FString& OutError);
    UFUNCTION(BlueprintCallable, Category="Portable Map")
    bool LoadFile(const FString& Filename, FString& OutError);
    /** Runtime preparation: disk/digests/parser/mesh work runs asynchronously; components are assembled over frames. */
    void LoadFileAsync(const FString& Filename,const FString& ExpectedHash,TFunction<void(bool,const FString&)> Complete,TFunction<bool()> IsCurrent={});
    /** Reuses immutable prepared meshes/materials/collision instead of rebuilding the GLB on match entry. */
    void CopyPreparedAsync(APortableMapScene* Source,TFunction<void(bool,const FString&)> Complete,TFunction<bool()> IsCurrent={});
    void CancelPreparation();
    UFUNCTION(BlueprintCallable, Category="Portable Map") void Clear();
    UFUNCTION(BlueprintPure, Category="Portable Map") bool IsLoaded() const { return !ContentId.IsEmpty(); }
    UFUNCTION(BlueprintPure, Category="Portable Map") FString GetContentId() const { return ContentId; }
    const TArray<FPortableMapMarker>& GetMarkers() const { return Markers; }
    const FString& GetBaseLighting() const { return BaseLighting; }
    const FPortableMapSun& GetBaseSun() const {return BaseSun;}
    const FPortableMapBoundary& GetBoundary() const { return Boundary; }
    /** Presentation/physics activation belongs to the integrating session owner. */
    void SetContentActive(bool bActive);
    static bool ValidateManifest(const FString& Json, FString& OutError);
    /** Validates the container, schema and embedded scene digest without realizing meshes. */
    static bool InspectFile(const FString& Filename, FPortableMapFileInfo& OutInfo, FString& OutError);
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    friend class UPortableMapBuildTask;
    UPROPERTY(Transient) TObjectPtr<UPortableMapBuildTask> Preparation;
    UPROPERTY(Transient) TObjectPtr<APortableMapScene> PreparedSource;
    UPROPERTY(Transient) TObjectPtr<UglTFRuntimeAsset> Asset;
    UPROPERTY(Transient) TArray<TObjectPtr<UPrimitiveComponent>> Realized;
    UPROPERTY(Transient) TArray<TObjectPtr<ULightComponent>> Lights;
    UPROPERTY(Transient) TArray<FPortableMapMarker> Markers;
    UPROPERTY(Transient) FPortableMapBoundary Boundary;
    FString ContentId;
    FString BaseLighting=TEXT("cartoon-street-day");
    FPortableMapSun BaseSun;
    bool bContentActive = false;
    bool LoadBytes(const FString& Json, const TArray<uint8>& Data, FString& OutError);
};
