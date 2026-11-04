#include "PortableMapEnvironment.h"
#include "PortableMapAuthoring.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "ImageUtils.h"
#include "glTFRuntimeAsset.h"
#include "glTFRuntimeFunctionLibrary.h"

namespace PortableMapEnvironment
{
static const FName PreviewTag(TEXT("Tanuki.Authoring.Preview"));
static const FName EnvironmentTag(TEXT("Tanuki.Authoring.Environment"));
static const FName ReferenceTag(TEXT("Tanuki.Authoring.ScaleReference"));
static FLinearColor Color(const TArray<TSharedPtr<FJsonValue>>& V)
{return FLinearColor(V[0]->AsNumber(),V[1]->AsNumber(),V[2]->AsNumber());}
void ResetSun(APortableMapAuthoringRoot* Root)
{
    const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("PortableMaps"));if(!Plugin||!Root)return;
    FString Text;TSharedPtr<FJsonObject> Profiles;const TSharedPtr<FJsonObject>* Profile=nullptr;
    if(!FFileHelper::LoadFileToString(Text,*(Plugin->GetBaseDir()/TEXT("Resources/Authoring/lighting-profiles.json")))||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Profiles)||!Profiles->TryGetObjectField(Root->BaseLighting,Profile))return;
    const auto Sun=(*Profile)->GetArrayField(TEXT("suns"))[0]->AsObject();const auto& Q=Sun->GetArrayField(TEXT("quaternion"));
    Root->SunRotation=FQuat(Q[0]->AsNumber(),Q[1]->AsNumber(),Q[2]->AsNumber(),Q[3]->AsNumber()).Rotator();Root->SunIntensity=Sun->GetNumberField(TEXT("intensity"));Root->SunColor=Color(Sun->GetArrayField(TEXT("color")));Root->bSunShadows=true;
}

void Configure(APortableMapAuthoringRoot* Root)
{
    if(!Root||!Root->GetWorld()||Root->GetWorld()->WorldType!=EWorldType::Editor)return;
    const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("PortableMaps"));if(!Plugin)return;
    const FString Directory=Plugin->GetBaseDir()/TEXT("Resources/Authoring");
    FString Text;TSharedPtr<FJsonObject> Profiles;
    if(!FFileHelper::LoadFileToString(Text,*(Directory/TEXT("lighting-profiles.json")))||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Profiles))return;
    const TSharedPtr<FJsonObject>* Entry=nullptr;if(!Profiles->TryGetObjectField(Root->BaseLighting,Entry))return;
    const auto Profile=*Entry;
    if(!Root->bOverrideSun)ResetSun(Root);
    auto ApplySun=[&](ADirectionalLight* Actor)
    {auto* Light=Actor->GetLightComponent();Light->SetMobility(EComponentMobility::Movable);Actor->SetActorRelativeRotation(Root->SunRotation);Light->SetIntensity(Root->SunIntensity);Light->SetLightColor(Root->SunColor);Light->SetCastShadows(Root->bSunShadows);};
    TArray<AActor*> Attached;Root->GetAttachedActors(Attached);
    bool HasEnvironment=false,HasReferences=false;
    for(auto* Actor:Attached)
    {
        HasReferences|=Actor->Tags.Contains(ReferenceTag);
        if(Actor->Tags.Contains(EnvironmentTag))
        {if(Actor->Tags.Contains(FName(*Root->BaseLighting))){HasEnvironment=true;if(auto* Sun=Cast<ADirectionalLight>(Actor))ApplySun(Sun);}else Actor->Destroy();}
    }
    auto Spawn=[&](UClass* Class,const FString& Label,FName Kind)->AActor*
    {
        FActorSpawnParameters Params;Params.Owner=Root;Params.OverrideLevel=Root->GetLevel();Params.ObjectFlags|=RF_Transient;
        auto* Actor=Root->GetWorld()->SpawnActor<AActor>(Class,FTransform::Identity,Params);
        if(Actor){Actor->Tags.Add(PreviewTag);Actor->Tags.Add(Kind);if(Kind==EnvironmentTag)Actor->Tags.Add(FName(*Root->BaseLighting));Actor->SetActorLabel(Label);Actor->AttachToActor(Root,FAttachmentTransformRules::KeepWorldTransform);}
        return Actor;
    };
    if(!HasEnvironment)
    {
        for(const auto& Value:Profile->GetArrayField(TEXT("suns")))
        {
            const auto Sun=Value->AsObject();auto* Actor=Cast<ADirectionalLight>(Spawn(ADirectionalLight::StaticClass(),TEXT("Base Sun"),EnvironmentTag));if(!Actor)continue;
            auto* Component=CastChecked<UDirectionalLightComponent>(Actor->GetLightComponent());Component->SetMobility(EComponentMobility::Movable);
            Component->SetIntensity(Sun->GetNumberField(TEXT("intensity")));Component->SetLightColor(Color(Sun->GetArrayField(TEXT("color"))));
            const auto& Q=Sun->GetArrayField(TEXT("quaternion"));Actor->SetActorRotation(FQuat(Q[0]->AsNumber(),Q[1]->AsNumber(),Q[2]->AsNumber(),Q[3]->AsNumber()));
            ApplySun(Actor);
        }
        auto* Sky=Cast<AStaticMeshActor>(Spawn(AStaticMeshActor::StaticClass(),TEXT("Skybox"),EnvironmentTag));
        if(Sky)
        {
            auto* Mesh=Sky->GetStaticMeshComponent();Mesh->SetMobility(EComponentMobility::Movable);Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetCastShadow(false);
            Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));Sky->SetActorScale3D(FVector(2000));
            auto* Texture=FImageUtils::ImportFileAsTexture2D(Directory/(Root->BaseLighting+TEXT(".hdr")));
            if(Texture)
            {
                Texture->SRGB=false;Texture->UpdateResource();
                auto* Material=NewObject<UMaterial>(Sky,NAME_None,RF_Transient);Material->SetShadingModel(MSM_Unlit);Material->TwoSided=true;Material->bIsSky=true;
                auto* Sample=NewObject<UMaterialExpressionTextureSample>(Material);Sample->Texture=Texture;Sample->SamplerType=SAMPLERTYPE_LinearColor;
                Material->GetExpressionCollection().AddExpression(Sample);Material->GetEditorOnlyData()->EmissiveColor.Connect(0,Sample);Material->PostEditChange();Mesh->SetMaterial(0,Material);
            }
        }
        auto* Ambient=Cast<ASkyLight>(Spawn(ASkyLight::StaticClass(),TEXT("Base Skylight"),EnvironmentTag));
        if(Ambient){auto* Component=Ambient->GetLightComponent();Component->SetMobility(EComponentMobility::Movable);Component->SetIntensity(Profile->GetNumberField(TEXT("skyIntensity")));Component->SetLightColor(Color(Profile->GetArrayField(TEXT("skyColor"))));Component->SetRealTimeCaptureEnabled(true);}
        auto* Post=Cast<APostProcessVolume>(Spawn(APostProcessVolume::StaticClass(),TEXT("Base Lighting Exposure"),EnvironmentTag));
        if(Post){Post->bUnbound=true;auto& S=Post->Settings;S.bOverride_AutoExposureMinBrightness=true;S.bOverride_AutoExposureMaxBrightness=true;S.bOverride_AutoExposureBias=true;S.AutoExposureMinBrightness=Profile->GetNumberField(TEXT("minEV100"));S.AutoExposureMaxBrightness=Profile->GetNumberField(TEXT("maxEV100"));S.AutoExposureBias=Profile->GetNumberField(TEXT("exposureBias"));}
    }
    PortableMapAuthoring::ConfigureScaleReferences(Root);
}
}

namespace PortableMapAuthoring
{
FBox GetReferenceFloor(APortableMapAuthoringRoot* Root)
{
    FBox Best(FVector(-1000,-1000,-20),FVector(1000,1000,0));double Area=0;
    TArray<AActor*> Actors;Root->GetAttachedActors(Actors,true,true);
    for(auto* Actor:Actors)
    {
        if(!IsValid(Actor)||Actor->Tags.Contains(TEXT("Tanuki.Authoring.Preview"))||Actor->Tags.Contains(TEXT("Tanuki.Authoring.ScaleReference"))||Actor->Tags.Contains(TEXT("Tanuki.Authoring.BaseLighting")))continue;
        const auto* Mesh=Cast<AStaticMeshActor>(Actor);if(!Mesh||!Mesh->GetStaticMeshComponent()->GetStaticMesh())continue;
        // Transform mesh bounds directly into map space. Undoing a world AABB
        // inflates rotated floors and moves the references away from their edge.
        const auto* Component=Mesh->GetStaticMeshComponent();
        const auto Relative=Component->GetComponentTransform().GetRelativeTransform(Root->GetActorTransform());
        const auto Box=Component->GetStaticMesh()->GetBoundingBox().TransformBy(Relative.ToMatrixWithScale());const auto Size=Box.GetSize();
        const double Candidate=Size.X*Size.Y;if(Size.Z>FMath::Min(Size.X,Size.Y)*.3||Candidate<=Area)continue;Best=Box;Area=Candidate;
    }
    return Best;
}
void ConfigureScaleReferences(APortableMapAuthoringRoot* Root)
{
    if(!Root||!Root->GetWorld()||Root->GetWorld()->WorldType!=EWorldType::Editor)return;
    const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("PortableMaps"));if(!Plugin)return;
    TArray<AActor*> Attached;Root->GetAttachedActors(Attached);
    const FName Tag(TEXT("Tanuki.Authoring.ScaleReference")),Version(TEXT("Tanuki.Reference.Static.v3"));const auto Floor=GetReferenceFloor(Root);
    for(int32 Index=0;Index<2;++Index)
    {
        const FString Name=Index?TEXT("Tengu"):TEXT("Tanuki");AStaticMeshActor* Reference=nullptr;
        for(auto* Actor:Attached)if(IsValid(Actor)&&Actor->Tags.Contains(Tag)&&Actor->GetActorLabel().StartsWith(Name))
        {if(!Reference&&Actor->Tags.Contains(Version)&&Actor->IsA<AStaticMeshActor>())Reference=Cast<AStaticMeshActor>(Actor);else Actor->Destroy();}
        if(!Reference)
        {
            FglTFRuntimeConfig Config;Config.bAllowExternalFiles=false;
            auto* Asset=UglTFRuntimeFunctionLibrary::glTFLoadAssetFromFilename(Plugin->GetBaseDir()/TEXT("Resources/Authoring")/(Name+TEXT(".glb")),false,Config);if(!Asset)continue;
            FglTFRuntimeStaticMeshConfig Settings;auto* Mesh=Asset->LoadStaticMeshRecursive(TEXT(""),{},Settings);if(!Mesh)continue;
            FActorSpawnParameters Params;Params.Owner=Root;Params.OverrideLevel=Root->GetLevel();Params.ObjectFlags|=RF_Transient;
            Reference=Root->GetWorld()->SpawnActor<AStaticMeshActor>(Params);Reference->Tags.Add(Tag);Reference->Tags.Add(Version);Reference->Tags.Add(TEXT("Tanuki.Authoring.Preview"));
            Reference->AttachToActor(Root,FAttachmentTransformRules::KeepRelativeTransform);Reference->SetActorLabel(Name+TEXT(" (scale reference)"));Reference->GetStaticMeshComponent()->SetStaticMesh(Mesh);Reference->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
        const double Margin=FMath::Min(150.,Floor.GetSize().X*.15),Spacing=FMath::Min(150.,Floor.GetSize().Y*.2);
        const double Bottom=Reference->GetStaticMeshComponent()->GetStaticMesh()->GetBoundingBox().Min.Z;
        Reference->SetActorRelativeTransform(FTransform(FQuat::Identity,FVector(Floor.Min.X+Margin,Floor.GetCenter().Y+(Index?Spacing:-Spacing),Floor.Max.Z-Bottom)));
    }
}
}
