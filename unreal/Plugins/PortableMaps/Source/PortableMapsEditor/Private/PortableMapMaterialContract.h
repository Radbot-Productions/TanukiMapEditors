#pragma once
#include "CoreMinimal.h"
class UMaterialInterface;
class UMaterialInstanceConstant;
class UMaterialInstance;
class UTexture2D;
class FJsonObject;
class UPortableMapMaterialData;
namespace PortableMapMaterialContract
{
    UMaterialInstanceConstant* Create(UObject* Outer,FName Name,const FString& Profile);
    void CopyParameters(UMaterialInstance* Source,UMaterialInstanceConstant* Destination);
    bool Validate(UMaterialInterface* Material,FString& Error);
    bool Export(UMaterialInterface* Material,TSharedPtr<FJsonObject>& Record,TFunctionRef<int32(UTexture2D*)> AddTexture,FString& Error);
    FString Classify(UMaterialInterface* Material);
    bool ApplyInputs(UMaterialInstanceConstant* Material,UPortableMapMaterialData* Data,FString& Error);
    UPortableMapMaterialData* GetInputs(UMaterialInstanceConstant* Material,FString& Error);
}
