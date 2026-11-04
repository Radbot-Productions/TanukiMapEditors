#pragma once
#include "CoreMinimal.h"
#include "PortableMapLighting.h"
#include "GameFramework/Actor.h"
#include "Engine/AssetUserData.h"
#include "Components/PrimitiveComponent.h"
#include "PortableMapAuthoring.generated.h"
class UMaterialInstanceConstant;
class UTexture2D;
UENUM()
enum class EPortableMapMaterialSurface : uint8 { PBR, Emissive, Cutout, Transparent };

/** Marks the portable material parameter contract; values remain editable on the material instance. */
UCLASS()
class PORTABLEMAPSEDITOR_API UPortableMapMaterialData : public UAssetUserData
{
    GENERATED_BODY()
public:
    UPROPERTY() bool bMasked=false;
    UPROPERTY() FString Profile=TEXT("PBR");
    UPROPERTY() bool bSharedInputs=false;
    UPROPERTY() EPortableMapMaterialSurface Surface=EPortableMapMaterialSurface::PBR;
    UPROPERTY() FLinearColor BaseColor=FLinearColor::White;
    UPROPERTY() TObjectPtr<UTexture2D> BaseColorTexture;
    UPROPERTY() float Roughness=.7f;
    UPROPERTY() TObjectPtr<UTexture2D> RoughnessTexture;
    UPROPERTY() float Metallic=0;
    UPROPERTY() TObjectPtr<UTexture2D> MetallicTexture;
    UPROPERTY() TObjectPtr<UTexture2D> NormalTexture;
    UPROPERTY() float NormalStrength=1;
    UPROPERTY() TObjectPtr<UTexture2D> AOTexture;
    UPROPERTY() FLinearColor EmissionColor=FLinearColor::White;
    UPROPERTY() float EmissionStrength=1;
    UPROPERTY() TObjectPtr<UTexture2D> EmissionTexture;
    UPROPERTY() float Opacity=1;
    UPROPERTY() float AlphaCutoff=.5f;
    UPROPERTY() FVector2D UVTiling=FVector2D(1,1);
    UPROPERTY() FVector2D UVOffset=FVector2D::ZeroVector;
    UPROPERTY() TObjectPtr<UTexture2D> PackedMetalRough;
    UPROPERTY() TObjectPtr<UTexture2D> PackedNormal;
    UPROPERTY() TObjectPtr<UTexture2D> PackedAO;
    virtual bool IsEditorOnly() const override {return true;}
};

/** Attach supported static meshes, collider actors and PlayerStarts beneath this root. Editor-only assets. */
UCLASS()
class PORTABLEMAPSEDITOR_API APortableMapAuthoringRoot : public AActor
{
    GENERATED_BODY()
public:
    APortableMapAuthoringRoot();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
    UPROPERTY(EditAnywhere,Category="Portable Map|Lighting",meta=(GetOptions="GetLightingProfiles")) FString BaseLighting=TEXT("cartoon-street-day");
    UPROPERTY(EditAnywhere,Category="Portable Map|Lighting") bool bOverrideSun=false;
    UPROPERTY(EditAnywhere,Category="Portable Map|Lighting",meta=(EditCondition="bOverrideSun")) FRotator SunRotation;
    UPROPERTY(EditAnywhere,Category="Portable Map|Lighting",meta=(EditCondition="bOverrideSun",ClampMin="0",ClampMax="10000000",Units="lx")) float SunIntensity=6;
    UPROPERTY(EditAnywhere,Category="Portable Map|Lighting",meta=(EditCondition="bOverrideSun",HideAlphaChannel)) FLinearColor SunColor=FLinearColor::White;
    UPROPERTY(EditAnywhere,Category="Portable Map|Lighting",meta=(EditCondition="bOverrideSun")) bool bSunShadows=true;
    FPortableMapSun GetSun() const {FPortableMapSun S;S.bOverride=bOverrideSun;S.Rotation=SunRotation.Quaternion();S.Intensity=SunIntensity;S.Color=SunColor;S.bShadows=bSunShadows;return S;}
    UFUNCTION() TArray<FString> GetLightingProfiles() const {return {TEXT("cartoon-street-day"),TEXT("cartoon-street-night")};}
    virtual void PostActorCreated() override;
    UPROPERTY(EditAnywhere,Category="Portable Map") FString MapId;
    UPROPERTY(VisibleAnywhere,Category="Portable Map") FString WorkshopItemId;
    UPROPERTY(VisibleAnywhere,Category="Portable Map") FString ExportFile;
};
UENUM()
enum class EPortableMapBoundaryShape : uint8 { Box, Cylinder };
UCLASS()
class PORTABLEMAPSEDITOR_API UPortableMapBoundaryPreview : public UPrimitiveComponent
{
    GENERATED_BODY()
public:
    virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
};

/** Unit dimensions are one metre; edit the actor transform to size the playable area. */
UCLASS()
class PORTABLEMAPSEDITOR_API APortableMapAuthoringBoundary : public AActor
{
    GENERATED_BODY()
public:
    APortableMapAuthoringBoundary();
    UPROPERTY(EditAnywhere,Category="Map Boundary") EPortableMapBoundaryShape Shape=EPortableMapBoundaryShape::Box;
    virtual void OnConstruction(const FTransform& Transform) override;
private:
    UPROPERTY() TObjectPtr<UPortableMapBoundaryPreview> Preview;
};
namespace PortableMapAuthoring
{
    PORTABLEMAPSEDITOR_API FBox GetReferenceFloor(APortableMapAuthoringRoot* Root);
    PORTABLEMAPSEDITOR_API void ConfigureScaleReferences(APortableMapAuthoringRoot* Root);
    PORTABLEMAPSEDITOR_API UMaterialInstanceConstant* CreateMaterial(UObject* Outer,const FString& Profile);
    PORTABLEMAPSEDITOR_API bool CreateMaterialPresets(const FString& Folder,FString& Error);
    PORTABLEMAPSEDITOR_API bool CanCreateMaterialInCurrentFolder(const FString& Profile,FString& Reason);
    PORTABLEMAPSEDITOR_API bool CreateMaterialInCurrentFolder(const FString& Profile,FString& Reason);
    PORTABLEMAPSEDITOR_API bool RepairMaterials(APortableMapAuthoringRoot* Root,FString& Report);
    PORTABLEMAPSEDITOR_API APortableMapAuthoringRoot* CreateStarter(UWorld* World);
    PORTABLEMAPSEDITOR_API bool Import(const FString& File,const FString& NewFolder,bool bFork,UWorld*& OutWorld,FString& Error,bool bSaveAssets=true);
    PORTABLEMAPSEDITOR_API bool Export(APortableMapAuthoringRoot* Root,const FString& File,FString& Error);
    PORTABLEMAPSEDITOR_API APortableMapAuthoringRoot* FindRoot(UWorld* World);
}
