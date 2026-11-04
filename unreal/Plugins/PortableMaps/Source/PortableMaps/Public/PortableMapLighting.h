#pragma once
#include "CoreMinimal.h"
class ULightComponent;
class USceneComponent;
class FJsonObject;

/** Portable light frame points along +X. Linear RGB; cd for point/spot, lux for directional.
 * Range is centimetres; cone values are half angles in degrees. No asset paths or scripts. */
struct PORTABLEMAPS_API FPortableMapLight
{
    FString Id,Type;
    FTransform Frame;
    FLinearColor Color=FLinearColor::White;
    float Intensity=1000,Range=1000,InnerCone=20,OuterCone=35;
    bool bShadows=true;
};
/** Optional replacement of the selected profile's sun; never creates an additional light. */
struct PORTABLEMAPS_API FPortableMapSun
{
    bool bOverride=false;
    FQuat Rotation=FQuat::Identity;
    FLinearColor Color=FLinearColor::White;
    float Intensity=6;
    bool bShadows=true;
};
namespace PortableMapLighting
{
    PORTABLEMAPS_API bool ReadSun(const TSharedPtr<FJsonObject>& Json,FPortableMapSun& Out);
    PORTABLEMAPS_API TSharedPtr<FJsonObject> WriteSun(const FPortableMapSun& Sun);
    PORTABLEMAPS_API bool Read(const TSharedPtr<FJsonObject>& Json,FPortableMapLight& Out);
    PORTABLEMAPS_API TSharedPtr<FJsonObject> Write(const FPortableMapLight& Light);
    PORTABLEMAPS_API ULightComponent* Create(UObject* Outer,const FPortableMapLight& Light);
    PORTABLEMAPS_API bool Capture(ULightComponent* Component,const FTransform& Root,FPortableMapLight& Out,FString& Error);
}
