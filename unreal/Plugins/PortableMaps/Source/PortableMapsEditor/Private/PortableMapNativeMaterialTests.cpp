#include "PortableMapNativeMaterials.h"
#include "PortableMapMaterialContract.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MaterialShared.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortableNativeMaterialTest,"Radbot.PortableMaps.NativeMaterialCollection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPortableNativeMaterialTest::RunTest(const FString&)
{
    for(const FString Profile:{TEXT("PBR"),TEXT("Cutout"),TEXT("Transparent")})
    {
        const FString Root=TEXT("/PortableMaps/Materials/");
        auto* Sample=LoadObject<UMaterialInstanceConstant>(nullptr,*(Root+TEXT("MI_Tanuki_")+Profile+TEXT(".MI_Tanuki_")+Profile));
        if(!TestNotNull(TEXT("Plugin sample exists"),Sample))return false;
        auto* Base=Sample->GetMaterial();
        TestTrue(TEXT("Own native Tanuki shader"),PortableMapNativeMaterials::IsBase(Base));TestFalse(TEXT("Shader compilation completed"),Base->IsCompiling());
        if(auto* Resource=Base->GetMaterialResource(GMaxRHIShaderPlatform))for(const FString& Error:Resource->GetCompileErrors())AddError(Error);
        TArray<FMaterialParameterInfo> Parameters;TArray<FGuid> Ids;Base->GetAllScalarParameterInfo(Parameters,Ids);
        TSet<FName> Expected={TEXT("roughnessFactor"),TEXT("metallicFactor"),TEXT("normalStrength"),TEXT("uvTilingX"),TEXT("uvTilingY"),TEXT("uvOffsetX"),TEXT("uvOffsetY")};
        if(Profile==TEXT("PBR"))Expected.Add(TEXT("emissiveStrength"));if(Profile==TEXT("Cutout"))Expected.Add(TEXT("alphaCutoff"));
        for(const auto& Parameter:Parameters)TestTrue(TEXT("Only export-supported scalar inputs"),Expected.Remove(Parameter.Name)>0);
        TestEqual(TEXT("Every supported scalar is exposed"),Expected.Num(),0);
        auto* Material=NewObject<UMaterialInstanceConstant>();Material->SetParentEditorOnly(Sample);
        Material->SetScalarParameterValueEditorOnly(TEXT("roughnessFactor"),.31f);Material->SetScalarParameterValueEditorOnly(TEXT("metallicFactor"),.67f);
        Material->SetScalarParameterValueEditorOnly(TEXT("normalStrength"),.65f);Material->SetScalarParameterValueEditorOnly(TEXT("uvTilingX"),2);Material->SetScalarParameterValueEditorOnly(TEXT("uvTilingY"),3);
        Material->SetScalarParameterValueEditorOnly(TEXT("uvOffsetX"),.1f);Material->SetScalarParameterValueEditorOnly(TEXT("uvOffsetY"),-.2f);
        Material->SetVectorParameterValueEditorOnly(TEXT("baseColorFactor"),FLinearColor(.2f,.4f,.6f,.42f));
        for(const TCHAR* Name:{TEXT("baseColorTexture"),TEXT("metallicRoughnessTexture"),TEXT("normalTexture")})
        {UTexture* Texture=nullptr;Sample->GetTextureParameterValue(FMaterialParameterInfo(Name),Texture);TestNotNull(TEXT("Native texture default exists"),Texture);Material->SetTextureParameterValueEditorOnly(Name,Texture);}
        if(Profile==TEXT("PBR")){Material->SetVectorParameterValueEditorOnly(TEXT("emissiveFactor"),FLinearColor(.1f,.3f,.5f));Material->SetScalarParameterValueEditorOnly(TEXT("emissiveStrength"),4);}
        if(Profile==TEXT("Cutout"))Material->SetScalarParameterValueEditorOnly(TEXT("alphaCutoff"),.36f);
        TSharedPtr<FJsonObject> Record;FString Error;
        if(!TestTrue(TEXT("Native parameters export without a recipe"),PortableMapMaterialContract::Export(Material,Record,[](UTexture2D*){return 0;},Error))){AddError(Error);return false;}
        auto PBR=Record->GetObjectField(TEXT("pbrMetallicRoughness"));TestEqual(TEXT("Roughness preserved"),PBR->GetNumberField(TEXT("roughnessFactor")),double(.31f));TestEqual(TEXT("Metallic preserved"),PBR->GetNumberField(TEXT("metallicFactor")),double(.67f));
        auto Normal=Record->GetObjectField(TEXT("normalTexture"));TestEqual(TEXT("Normal strength preserved"),Normal->GetNumberField(TEXT("scale")),double(.65f));
        const auto UV=Normal->GetObjectField(TEXT("extensions"))->GetObjectField(TEXT("KHR_texture_transform"));TestEqual(TEXT("Shared UV tiling preserved"),UV->GetArrayField(TEXT("scale"))[1]->AsNumber(),3.0);
        TestEqual(TEXT("Shared UV offset preserved"),UV->GetArrayField(TEXT("offset"))[0]->AsNumber(),double(.1f));
        if(Profile==TEXT("PBR"))TestEqual(TEXT("Optional PBR emission preserved"),Record->GetObjectField(TEXT("extensions"))->GetObjectField(TEXT("KHR_materials_emissive_strength"))->GetNumberField(TEXT("emissiveStrength")),4.0);
        if(Profile==TEXT("Cutout")){TestEqual(TEXT("Cutout surface preserved"),Record->GetStringField(TEXT("alphaMode")),FString(TEXT("MASK")));TestEqual(TEXT("Cutoff preserved"),Record->GetNumberField(TEXT("alphaCutoff")),double(.36f));}
        if(Profile==TEXT("Transparent"))TestEqual(TEXT("Transparent surface preserved"),Record->GetStringField(TEXT("alphaMode")),FString(TEXT("BLEND")));
    }
    return !HasAnyErrors();
}
#endif
