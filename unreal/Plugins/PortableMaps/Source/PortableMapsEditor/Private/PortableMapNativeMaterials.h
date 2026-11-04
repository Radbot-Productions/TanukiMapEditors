#pragma once
#include "CoreMinimal.h"
class UMaterialInterface;
class UMaterialInstanceConstant;
namespace PortableMapNativeMaterials
{
    bool EnsureCollection(FString& Error);
    bool IsBase(const UMaterialInterface* Material);
    UMaterialInstanceConstant* Create(UObject* Outer,FName Name,const FString& Profile);
}
