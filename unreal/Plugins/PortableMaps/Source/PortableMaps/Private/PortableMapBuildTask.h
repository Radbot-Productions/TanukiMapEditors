#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Containers/Ticker.h"
#include "PortableMapBuildTask.generated.h"
class APortableMapScene;
class UglTFRuntimeAsset;
class UPrimitiveComponent;
class ULightComponent;
class UStaticMesh;
struct FPortableMapBuildState;
struct FStreamableHandle;

/** Owned resource work, independent of participant presentation phase. */
UCLASS()
class UPortableMapBuildTask : public UObject
{
    GENERATED_BODY()
public:
    void Start(APortableMapScene* Scene,const FString& File,const FString& ExpectedHash,TFunction<void(bool,const FString&)> Complete,TFunction<bool()> Current);
    void Copy(APortableMapScene* Scene,APortableMapScene* Source,TFunction<void(bool,const FString&)> Complete,TFunction<bool()> Current);
    void Cancel();
private:
    UPROPERTY() TObjectPtr<APortableMapScene> Target;
    UPROPERTY() TObjectPtr<APortableMapScene> Source;
    UPROPERTY() TObjectPtr<UglTFRuntimeAsset> Asset;
    UPROPERTY() TObjectPtr<UStaticMesh> PendingMesh;
    UPROPERTY() TArray<TObjectPtr<UPrimitiveComponent>> Components;
    UPROPERTY() TArray<TObjectPtr<ULightComponent>> Lights;
    TSharedPtr<FPortableMapBuildState,ESPMode::ThreadSafe> Work;
    TSharedPtr<FStreamableHandle> MaterialHandle;
    void ReadFile(const FString& File,const FString& ExpectedHash);
    TFunction<void(bool,const FString&)> Completion;
    TFunction<bool()> IsCurrent;
    FTSTicker::FDelegateHandle TickHandle;
    int32 MeshIndex=0,ColliderIndex=0,LightIndex=0;
    bool bMeshPending=false,bCancelled=false;
    double Started=0,MaxSlice=0;
    bool Tick(float Delta);
    bool Current() const;
    void Finish(bool Success,const FString& Error);
    void Register(UPrimitiveComponent* Component);
    UFUNCTION() void MeshReady(UStaticMesh* Mesh);
    void CompleteMesh(UStaticMesh* Mesh);
};
