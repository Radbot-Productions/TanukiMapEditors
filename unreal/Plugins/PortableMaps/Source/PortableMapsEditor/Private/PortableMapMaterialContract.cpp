#include "PortableMapMaterialContract.h"
#include "PortableMapNativeMaterials.h"
#include "PortableMapAuthoring.h"
#include "glTFRuntimeAsset.h"
#include "glTFRuntimeFunctionLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Engine/Texture2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "ImageUtils.h"
#include "ImageCore.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "HAL/IConsoleManager.h"
#include "PortableMapScene.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"
#endif

namespace
{
    using JObject=TSharedPtr<FJsonObject>;
    TArray<TSharedPtr<FJsonValue>> Numbers(std::initializer_list<double> Values)
    {TArray<TSharedPtr<FJsonValue>> Result;for(double Value:Values)Result.Add(MakeShared<FJsonValueNumber>(Value));return Result;}
    UMaterialInstanceDynamic* Source(const FString& Profile)
    {
        static TMap<FString,TWeakObjectPtr<UMaterialInstanceDynamic>> Cache;
        if(auto* Found=Cache.Find(Profile);Found&&Found->IsValid())return Found->Get();
        if(Profile!=TEXT("PBR")&&Profile!=TEXT("Emissive")&&Profile!=TEXT("Cutout")&&Profile!=TEXT("Transparent"))return nullptr;
        const FString Extra=Profile==TEXT("Emissive")?TEXT(",\"emissiveFactor\":[1,1,1]"):Profile==TEXT("Cutout")?TEXT(",\"alphaMode\":\"MASK\",\"alphaCutoff\":0.5"):Profile==TEXT("Transparent")?TEXT(",\"alphaMode\":\"BLEND\""):TEXT("");
        const TCHAR* Color=Profile==TEXT("Transparent")?TEXT("0.3185468,0.6038274,1,0.4"):TEXT("0.3185468,0.3185468,0.3185468,1");
        const FString Json=FString::Printf(TEXT("{\"asset\":{\"version\":\"2.0\"},\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorFactor\":[%s],\"metallicFactor\":0,\"roughnessFactor\":0.7}%s}]}"),Color,*Extra);
        FglTFRuntimeConfig Config;Config.bAllowExternalFiles=false;Config.bNoArchive=true;
        auto* Asset=UglTFRuntimeFunctionLibrary::glTFLoadAssetFromString(Json,Config);FglTFRuntimeMaterialsConfig Materials;
        auto* Result=Asset?Cast<UMaterialInstanceDynamic>(Asset->LoadMaterial(0,Materials,false)):nullptr;if(Result)Cache.Add(Profile,Result);return Result;
    }
    float Scalar(UMaterialInterface* Material,const TCHAR* Name,float Fallback)
    {Material->GetScalarParameterValue(FMaterialParameterInfo(Name),Fallback);return Fallback;}
    FLinearColor Vector(UMaterialInterface* Material,const TCHAR* Name,FLinearColor Fallback)
    {Material->GetVectorParameterValue(FMaterialParameterInfo(Name),Fallback);return Fallback;}
    UTexture2D* Texture(UMaterialInterface* Material,const FName Name)
    {
        for(auto* Instance=Cast<UMaterialInstance>(Material);Instance;Instance=Cast<UMaterialInstance>(Instance->Parent))
            for(const auto& Value:Instance->TextureParameterValues)if(Value.ParameterInfo.Name==Name)return Cast<UTexture2D>(Value.ParameterValue);
        return nullptr;
    }
    struct FSimpleInput
    {bool Valid=false;FLinearColor Factor=FLinearColor::White;UTexture2D* Image=nullptr;FVector2D Scale=FVector2D(1,1);};
    FSimpleInput ReadExpression(UMaterialInterface* Owner,UMaterialExpression* Expression,int32 Depth=0)
    {
        FSimpleInput Result;if(!Expression||Depth>12)return Result;
        if(auto* ConstantColor=Cast<UMaterialExpressionConstant3Vector>(Expression)){Result.Valid=true;Result.Factor=ConstantColor->Constant;}
        else if(auto* ConstantScalar=Cast<UMaterialExpressionConstant>(Expression)){Result.Valid=true;Result.Factor=FLinearColor(ConstantScalar->R,ConstantScalar->R,ConstantScalar->R,ConstantScalar->R);}
        else if(auto* VectorParameter=Cast<UMaterialExpressionVectorParameter>(Expression))
        {if(VectorParameter->bUseCustomPrimitiveData)return Result;Result.Valid=true;Result.Factor=VectorParameter->DefaultValue;Owner->GetVectorParameterValue(FMaterialParameterInfo(VectorParameter->ParameterName),Result.Factor);}
        else if(auto* ScalarParameter=Cast<UMaterialExpressionScalarParameter>(Expression))
        {if(ScalarParameter->bUseCustomPrimitiveData)return Result;float Number=ScalarParameter->DefaultValue;Owner->GetScalarParameterValue(FMaterialParameterInfo(ScalarParameter->ParameterName),Number);Result.Valid=true;Result.Factor=FLinearColor(Number,Number,Number,Number);}
        else if(auto* Sample=Cast<UMaterialExpressionTextureSample>(Expression))
        {
            if(Sample->TextureObject.Expression)return Result;
            UTexture* Image=Sample->Texture;
            if(auto* Parameter=Cast<UMaterialExpressionTextureSampleParameter>(Sample))Owner->GetTextureParameterValue(FMaterialParameterInfo(Parameter->ParameterName),Image);
            Result.Image=Cast<UTexture2D>(Image);if(!Result.Image)return Result;
            if(Sample->Coordinates.Expression)
            {auto* UV=Cast<UMaterialExpressionTextureCoordinate>(Sample->Coordinates.Expression);if(!UV||UV->CoordinateIndex!=0)return FSimpleInput();Result.Scale=FVector2D(UV->UTiling,UV->VTiling);}
            else if(Sample->ConstCoordinate!=0)return FSimpleInput();
            Result.Valid=true;
        }
        else if(auto* Product=Cast<UMaterialExpressionMultiply>(Expression))
        {
            auto A=ReadExpression(Owner,Product->A.Expression,Depth+1),B=ReadExpression(Owner,Product->B.Expression,Depth+1);
            if(!Product->A.Expression){A.Valid=true;A.Factor=FLinearColor(Product->ConstA,Product->ConstA,Product->ConstA,Product->ConstA);}
            if(!Product->B.Expression){B.Valid=true;B.Factor=FLinearColor(Product->ConstB,Product->ConstB,Product->ConstB,Product->ConstB);}
            if(!A.Valid||!B.Valid||(A.Image&&B.Image))return Result;
            Result.Valid=true;Result.Factor=A.Factor*B.Factor;Result.Image=A.Image?A.Image:B.Image;Result.Scale=A.Image?A.Scale:B.Scale;
        }
        return Result;
    }
    FSimpleInput ReadProperty(UMaterialInterface* Material,EMaterialProperty Property)
    {
        auto* Base=Material->GetMaterial();if(!Base)return FSimpleInput();
        if(auto* Input=Base->GetExpressionInputForProperty(Property);Input&&Input->Expression)return ReadExpression(Material,Input->Expression);
        auto* Data=Base->GetEditorOnlyData();FSimpleInput Result;
        if(!Data)return Result;
        if(Property==MP_BaseColor&&Data->BaseColor.UseConstant){Result.Valid=true;Result.Factor=FLinearColor(Data->BaseColor.Constant);}
        if(Property==MP_EmissiveColor&&Data->EmissiveColor.UseConstant){Result.Valid=true;Result.Factor=FLinearColor(Data->EmissiveColor.Constant);}
        if(Property==MP_Metallic&&Data->Metallic.UseConstant){Result.Valid=true;float Value=Data->Metallic.Constant;Result.Factor=FLinearColor(Value,Value,Value,Value);}
        if(Property==MP_Roughness&&Data->Roughness.UseConstant){Result.Valid=true;float Value=Data->Roughness.Constant;Result.Factor=FLinearColor(Value,Value,Value,Value);}
        if(Property==MP_Opacity&&Data->Opacity.UseConstant){Result.Valid=true;float Value=Data->Opacity.Constant;Result.Factor=FLinearColor(Value,Value,Value,Value);}
        return Result;
    }
}
void PortableMapMaterialContract::CopyParameters(UMaterialInstance* Source,UMaterialInstanceConstant* Destination)
{
    // Copy authored overrides only; inherited fallback textures are not authored
    // texture slots and must not become explicit portable image references.
    for(const auto& Value:Source->ScalarParameterValues)Destination->SetScalarParameterValueEditorOnly(Value.ParameterInfo,Value.ParameterValue);
    for(const auto& Value:Source->VectorParameterValues)Destination->SetVectorParameterValueEditorOnly(Value.ParameterInfo,Value.ParameterValue);
    for(const auto& Value:Source->TextureParameterValues)Destination->SetTextureParameterValueEditorOnly(Value.ParameterInfo,Value.ParameterValue);
}
UMaterialInstanceConstant* PortableMapMaterialContract::Create(UObject* Outer,FName Name,const FString& Profile)
{
    auto* Dynamic=Source(Profile);if(!Dynamic)return nullptr;
    auto* Material=NewObject<UMaterialInstanceConstant>(Outer,Name,RF_Transactional);
    Material->SetParentEditorOnly(Dynamic->Parent);CopyParameters(Dynamic,Material);
    auto* Data=NewObject<UPortableMapMaterialData>(Material);Data->Profile=Profile;Data->bMasked=Profile==TEXT("Cutout");Material->AddAssetUserData(Data);Material->PostEditChange();return Material;
}
FString PortableMapMaterialContract::Classify(UMaterialInterface* Material)
{
    if(Material)if(auto* Data=Cast<UPortableMapMaterialData>(Material->GetAssetUserDataOfClass(UPortableMapMaterialData::StaticClass()));Data&&Data->bSharedInputs)return Data->Profile;
    if(Material->GetBlendMode()==BLEND_Masked)return TEXT("Cutout");
    if(Material->GetBlendMode()!=BLEND_Opaque)return TEXT("Transparent");
    auto Emission=Vector(Material,TEXT("emissiveFactor"),FLinearColor::Black);
    if(FMath::Max3(Emission.R,Emission.G,Emission.B)>0)return TEXT("Emissive");
    for(const TCHAR* Name:{TEXT("EmissiveColor"),TEXT("EmissionColor")})
    {Emission=Vector(Material,Name,FLinearColor::Black);if(FMath::Max3(Emission.R,Emission.G,Emission.B)>0)return TEXT("Emissive");}
    auto Graph=ReadProperty(Material,MP_EmissiveColor);if(Graph.Valid&&FMath::Max3(Graph.Factor.R,Graph.Factor.G,Graph.Factor.B)>0)return TEXT("Emissive");
    return TEXT("PBR");
}
UPortableMapMaterialData* PortableMapMaterialContract::GetInputs(UMaterialInstanceConstant* Material,FString& Error)
{
    if(!Validate(Material,Error))return nullptr;
    auto* Data=Cast<UPortableMapMaterialData>(Material->GetAssetUserDataOfClass(UPortableMapMaterialData::StaticClass()));
    if(!Data){Data=NewObject<UPortableMapMaterialData>(Material);Data->Profile=Classify(Material);Data->bMasked=Material->GetBlendMode()==BLEND_Masked;Material->AddAssetUserData(Data);}
    if(Data->bSharedInputs)return Data;
    // A packed imported material remains editable through the native instance editor.
    // Do not silently lose its channels when adopting separate source-image slots.
    if(Texture(Material,TEXT("metallicRoughnessTexture"))){Error=TEXT("Create a new Tanuki preset to use separate roughness/metallic source images; this imported material already has packed channels.");return nullptr;}
    Data->Modify();Data->Surface=Data->Profile==TEXT("Emissive")?EPortableMapMaterialSurface::Emissive:Data->Profile==TEXT("Cutout")?EPortableMapMaterialSurface::Cutout:Data->Profile==TEXT("Transparent")?EPortableMapMaterialSurface::Transparent:EPortableMapMaterialSurface::PBR;
    Data->BaseColor=Vector(Material,TEXT("baseColorFactor"),FLinearColor::White);Data->Opacity=Data->BaseColor.A;
    Data->Roughness=Scalar(Material,TEXT("roughnessFactor"),.7f);Data->Metallic=Scalar(Material,TEXT("metallicFactor"),0);
    Data->BaseColorTexture=Texture(Material,TEXT("baseColorTexture"));Data->NormalTexture=Texture(Material,TEXT("normalTexture"));Data->AOTexture=Texture(Material,TEXT("occlusionTexture"));Data->EmissionTexture=Texture(Material,TEXT("emissiveTexture"));
    Data->NormalStrength=Vector(Material,TEXT("normalTexScale"),FLinearColor::White).R;
    Data->EmissionColor=Vector(Material,TEXT("emissiveFactor"),FLinearColor::White);Data->EmissionStrength=Scalar(Material,TEXT("emissiveStrength"),1);Data->AlphaCutoff=Scalar(Material,TEXT("alphaCutoff"),.5f);
    auto Scale=Vector(Material,TEXT("baseColorScale"),FLinearColor(1,1,0,0)),Offset=Vector(Material,TEXT("baseColorOffset"),FLinearColor::Black);
    Data->UVTiling=FVector2D(Scale.R,Scale.G);Data->UVOffset=FVector2D(Offset.R,Offset.G);Data->bSharedInputs=true;Data->MarkPackageDirty();return Data;
}
bool PortableMapMaterialContract::ApplyInputs(UMaterialInstanceConstant* Material,UPortableMapMaterialData* Data,FString& Error)
{
    if(!Material||!Data){Error=TEXT("Choose a Tanuki material.");return false;}
    auto Read=[&](UTexture2D* Texture,FImage& Image)
    {
        if(!Texture)return true;
        if(!FImageUtils::GetTexture2DSourceImage(Texture,Image)||Image.SizeX>8192||Image.SizeY>8192){Error=TEXT("Use source-backed 2D textures up to 8192 pixels: ")+Texture->GetName();return false;}
        // Data channels are raw values, regardless of the source asset's sRGB flag.
        Image.ChangeFormat(ERawImageFormat::BGRA8,Image.GammaSpace);return true;
    };
    FImage Rough,Metal,Normal,AO;
    if(!Read(Data->RoughnessTexture,Rough)||!Read(Data->MetallicTexture,Metal)||!Read(Data->NormalTexture,Normal)||!Read(Data->AOTexture,AO))return false;
    const FString Profile=StaticEnum<EPortableMapMaterialSurface>()->GetNameStringByValue((int64)Data->Surface);
    auto* Dynamic=Source(Profile);if(!Dynamic){Error=TEXT("Unsupported material surface.");return false;}
    auto Pack=[&](const TCHAR* Label,const FImage& Image,TextureCompressionSettings Compression)->UTexture2D*
    {
        auto* Result=NewObject<UTexture2D>(Material,MakeUniqueObjectName(Material,UTexture2D::StaticClass(),FName(Label)),RF_Transactional);
        Result->Source.Init(Image.SizeX,Image.SizeY,1,1,TSF_BGRA8,Image.RawData.GetData());Result->SRGB=false;Result->CompressionSettings=Compression;Result->PostEditChange();return Result;
    };
    UTexture2D* Packed=nullptr;
    if(Data->RoughnessTexture||Data->MetallicTexture)
    {
        FImage Image;Image.Init(FMath::Max(Rough.SizeX,Metal.SizeX),FMath::Max(Rough.SizeY,Metal.SizeY),ERawImageFormat::BGRA8,EGammaSpace::Linear);
        auto Red=[](const FImage& Source,int32 X,int32 Y,int32 W,int32 H)->uint8
        {if(Source.SizeX==0)return 255;return Source.AsBGRA8()[FMath::Min(Source.SizeY-1,Y*Source.SizeY/H)*Source.SizeX+FMath::Min(Source.SizeX-1,X*Source.SizeX/W)].R;};
        for(int32 Y=0;Y<Image.SizeY;++Y)for(int32 X=0;X<Image.SizeX;++X)Image.AsBGRA8()[Y*Image.SizeX+X]=FColor(255,Red(Rough,X,Y,Image.SizeX,Image.SizeY),Red(Metal,X,Y,Image.SizeX,Image.SizeY),255);
        Packed=Pack(TEXT("TanukiMetalRough"),Image,TC_Masks);
    }
    Data->Modify();Material->Modify();Data->PackedMetalRough=Packed;
    Data->PackedNormal=Data->NormalTexture?Pack(TEXT("TanukiNormal"),Normal,TC_Normalmap):nullptr;
    if(Data->AOTexture){for(auto& Pixel:AO.AsBGRA8())Pixel=FColor(Pixel.R,Pixel.R,Pixel.R,255);Data->PackedAO=Pack(TEXT("TanukiAO"),AO,TC_Masks);}else Data->PackedAO=nullptr;
    Data->bSharedInputs=true;Data->Profile=Profile;Data->bMasked=Profile==TEXT("Cutout");
    Material->SetParentEditorOnly(Dynamic->Parent);Material->ClearParameterValuesEditorOnly();CopyParameters(Dynamic,Material);
    FLinearColor Color=Data->BaseColor;Color.A=Profile==TEXT("Cutout")||Profile==TEXT("Transparent")?Data->Opacity:1;
    Material->SetVectorParameterValueEditorOnly(TEXT("baseColorFactor"),Color);
    Material->SetScalarParameterValueEditorOnly(TEXT("roughnessFactor"),Data->RoughnessTexture?1:Data->Roughness);
    Material->SetScalarParameterValueEditorOnly(TEXT("metallicFactor"),Data->MetallicTexture?1:Data->Metallic);
    Material->SetScalarParameterValueEditorOnly(TEXT("alphaCutoff"),Data->AlphaCutoff);
    Material->SetVectorParameterValueEditorOnly(TEXT("normalTexScale"),FLinearColor(Data->NormalStrength,Data->NormalStrength,1,1));
    Material->SetVectorParameterValueEditorOnly(TEXT("emissiveFactor"),Profile==TEXT("Emissive")?Data->EmissionColor:FLinearColor::Black);
    Material->SetScalarParameterValueEditorOnly(TEXT("emissiveStrength"),Data->EmissionStrength);
    const TArray<TPair<FString,UTexture2D*>> Textures={{TEXT("baseColor"),Data->BaseColorTexture.Get()},{TEXT("metallicRoughness"),Data->PackedMetalRough.Get()},{TEXT("normal"),Data->PackedNormal.Get()},{TEXT("occlusion"),Data->PackedAO.Get()},{TEXT("emissive"),Profile==TEXT("Emissive")?Data->EmissionTexture.Get():nullptr}};
    for(const auto& Entry:Textures)if(Entry.Value)
    {
        Material->SetTextureParameterValueEditorOnly(FName(Entry.Key+TEXT("Texture")),Entry.Value);
        Material->SetVectorParameterValueEditorOnly(FName(Entry.Key+TEXT("TexCoord")),FLinearColor(1,0,0,0));
        Material->SetVectorParameterValueEditorOnly(FName(Entry.Key+TEXT("Scale")),FLinearColor(Data->UVTiling.X,Data->UVTiling.Y,0,0));
        Material->SetVectorParameterValueEditorOnly(FName(Entry.Key+TEXT("Offset")),FLinearColor(Data->UVOffset.X,Data->UVOffset.Y,0,0));
    }
    Material->PostEditChange();Material->MarkPackageDirty();return Validate(Material,Error);
}
bool PortableMapMaterialContract::Validate(UMaterialInterface* Material,FString& Error)
{
    auto Fail=[&](const TCHAR* Reason){Error=FString::Printf(TEXT("Material '%s': %s. Use Tanuki material presets or Repair Materials."),*GetNameSafe(Material),Reason);return false;};
    auto* Instance=Cast<UMaterialInstanceConstant>(Material);
    auto* Data=Material?Cast<UPortableMapMaterialData>(Material->GetAssetUserDataOfClass(UPortableMapMaterialData::StaticClass())):nullptr;
    const bool Native=Material&&PortableMapNativeMaterials::IsBase(Material->GetMaterial());
    if(!Instance||(!Data&&!Native))return Fail(TEXT("unsupported material or shader graph"));
    FString Profile=Data?(Data->bMasked?TEXT("Cutout"):Data->Profile):Material->GetBlendMode()==BLEND_Masked?TEXT("Cutout"):Material->GetBlendMode()==BLEND_Opaque?TEXT("PBR"):TEXT("Transparent");
    auto* Expected=Native?nullptr:Source(Profile);
    if(!Native&&(!Expected||Material->GetMaterial()!=Expected->GetMaterial()))return Fail(TEXT("the material parent does not match its portable profile"));
    if(Instance->HasOverridenBaseProperties()||Instance->HasStaticParameters())return Fail(TEXT("custom base-property or static-switch overrides are unsupported"));
    const auto Surface=[](const FString& Value){return Value==TEXT("Emissive")?FString(TEXT("PBR")):Value;};
    if(Surface(Profile)!=Surface(Classify(Material)))return Fail(TEXT("emission/alpha settings do not match the portable profile"));
    auto Color=Vector(Material,TEXT("baseColorFactor"),FLinearColor::White);
    for(float Value:{Color.R,Color.G,Color.B,Color.A})if(!FMath::IsFinite(Value)||Value<0||Value>1)return Fail(TEXT("base-color components must be finite and within 0-1"));
    auto Emission=Vector(Material,TEXT("emissiveFactor"),FLinearColor::Black);
    for(float Value:{Emission.R,Emission.G,Emission.B})if(!FMath::IsFinite(Value)||Value<0)return Fail(TEXT("emission components must be finite and non-negative"));
    if(Profile!=TEXT("PBR")&&Profile!=TEXT("Emissive")&&FMath::Max3(Emission.R,Emission.G,Emission.B)>0)return Fail(TEXT("emission requires the opaque PBR surface"));
    float Strength=Scalar(Material,TEXT("emissiveStrength"),1);if(!FMath::IsFinite(Strength)||Strength<0)return Fail(TEXT("emission strength must be finite and non-negative"));
    for(const TCHAR* Name:{TEXT("clearcoatFactor"),TEXT("transmissionFactor"),TEXT("thicknessFactor")})if(Scalar(Material,Name,0)!=0)return Fail(TEXT("extra material surface features are unsupported"));
    for(const TCHAR* Name:{TEXT("metallicFactor"),TEXT("roughnessFactor"),TEXT("alphaCutoff")})
    {float Value=Scalar(Material,Name,FCString::Strcmp(Name,TEXT("alphaCutoff"))==0?.5f:1.f);if(!FMath::IsFinite(Value)||Value<0||Value>1)return Fail(TEXT("material factors must be finite and within 0-1"));}
    return true;
}
bool PortableMapMaterialContract::Export(UMaterialInterface* Material,JObject& Record,TFunctionRef<int32(UTexture2D*)> AddTexture,FString& Error)
{
    if(!Validate(Material,Error))return false;
    Record=MakeShared<FJsonObject>();Record->SetStringField(TEXT("name"),Material->GetName());
    const FString Profile=Classify(Material);auto Extras=MakeShared<FJsonObject>();Extras->SetStringField(TEXT("tanukiProfile"),Profile);Record->SetObjectField(TEXT("extras"),Extras);
    auto Color=Vector(Material,TEXT("baseColorFactor"),FLinearColor::White);auto Pbr=MakeShared<FJsonObject>();
    Pbr->SetArrayField(TEXT("baseColorFactor"),Numbers({Color.R,Color.G,Color.B,Color.A}));Pbr->SetNumberField(TEXT("metallicFactor"),Scalar(Material,TEXT("metallicFactor"),1));Pbr->SetNumberField(TEXT("roughnessFactor"),Scalar(Material,TEXT("roughnessFactor"),1));
    Record->SetObjectField(TEXT("pbrMetallicRoughness"),Pbr);Record->SetBoolField(TEXT("doubleSided"),false);
    if(Profile==TEXT("Cutout")){Record->SetStringField(TEXT("alphaMode"),TEXT("MASK"));Record->SetNumberField(TEXT("alphaCutoff"),Scalar(Material,TEXT("alphaCutoff"),.5f));}
    if(Profile==TEXT("Transparent"))Record->SetStringField(TEXT("alphaMode"),TEXT("BLEND"));
    TOptional<FVector4f> SharedUV;
    for(const TCHAR* Name:{TEXT("baseColor"),TEXT("metallicRoughness"),TEXT("normal"),TEXT("occlusion"),TEXT("emissive")})
    {
        FString Prefix(Name);auto* Image=Texture(Material,FName(Prefix+TEXT("Texture")));if(!Image)continue;
        auto TexCoord=Vector(Material,*(Prefix+TEXT("TexCoord")),FLinearColor(1,0,0,0));
        const bool Native=PortableMapNativeMaterials::IsBase(Material->GetMaterial());
        auto Scale=Native?FLinearColor(Scalar(Material,TEXT("uvTilingX"),1),Scalar(Material,TEXT("uvTilingY"),1),0,0):Vector(Material,*(Prefix+TEXT("Scale")),FLinearColor(1,1,0,0));
        auto Offset=Native?FLinearColor(Scalar(Material,TEXT("uvOffsetX"),0),Scalar(Material,TEXT("uvOffsetY"),0),0,0):Vector(Material,*(Prefix+TEXT("Offset")),FLinearColor(0,0,0,0));
        if(!TexCoord.Equals(FLinearColor(1,0,0,0),.00001f)||!FMath::IsNearlyZero(Scalar(Material,*(Prefix+TEXT("Rotation")),0)))
        {Error=TEXT("Portable material textures require UV0 with scale/offset only: ")+Material->GetName();return false;}
        FVector4f UV(Scale.R,Scale.G,Offset.R,Offset.G);
        if(SharedUV.IsSet()&&!SharedUV.GetValue().Equals(UV,.00001f)){Error=TEXT("Material textures must share UV0 tiling/offset: ")+Material->GetName();return false;}SharedUV=UV;
        int32 Index=AddTexture(Image);if(Index<0){Error=TEXT("Cannot encode material texture: ")+Image->GetName();return false;}
        auto Ref=MakeShared<FJsonObject>();Ref->SetNumberField(TEXT("index"),Index);
        if(UV!=FVector4f(1,1,0,0))
        {
            auto Transform=MakeShared<FJsonObject>();Transform->SetArrayField(TEXT("scale"),Numbers({Scale.R,Scale.G}));Transform->SetArrayField(TEXT("offset"),Numbers({Offset.R,Offset.G}));
            auto Extensions=MakeShared<FJsonObject>();Extensions->SetObjectField(TEXT("KHR_texture_transform"),Transform);Ref->SetObjectField(TEXT("extensions"),Extensions);
        }
        if(Prefix==TEXT("normal"))Ref->SetNumberField(TEXT("scale"),Native?Scalar(Material,TEXT("normalStrength"),1):Vector(Material,TEXT("normalTexScale"),FLinearColor::White).R);
        if(Prefix==TEXT("baseColor")||Prefix==TEXT("metallicRoughness"))Pbr->SetObjectField(Prefix+TEXT("Texture"),Ref);else Record->SetObjectField(Prefix+TEXT("Texture"),Ref);
    }
    if(Profile==TEXT("Emissive"))
    {
        auto Emission=Vector(Material,TEXT("emissiveFactor"),FLinearColor::Black);
        float Normalizer=FMath::Max(1.f,FMath::Max3(Emission.R,Emission.G,Emission.B));
        Record->SetArrayField(TEXT("emissiveFactor"),Numbers({Emission.R/Normalizer,Emission.G/Normalizer,Emission.B/Normalizer}));
        float Strength=Scalar(Material,TEXT("emissiveStrength"),1)*Normalizer;
        if(Strength!=1){auto Value=MakeShared<FJsonObject>();Value->SetNumberField(TEXT("emissiveStrength"),Strength);auto Extensions=MakeShared<FJsonObject>();Extensions->SetObjectField(TEXT("KHR_materials_emissive_strength"),Value);Record->SetObjectField(TEXT("extensions"),Extensions);}
    }
    return true;
}
UMaterialInstanceConstant* PortableMapAuthoring::CreateMaterial(UObject* Outer,const FString& Profile)
{return PortableMapNativeMaterials::Create(Outer,MakeUniqueObjectName(Outer,UMaterialInstanceConstant::StaticClass(),FName(*(TEXT("Tanuki_")+Profile))),Profile);}
bool PortableMapAuthoring::CreateMaterialPresets(const FString& Folder,FString& Error)
{
    if(Folder==TEXT("/PortableMaps/Materials"))return PortableMapNativeMaterials::EnsureCollection(Error);
    if(!Folder.StartsWith(TEXT("/Game/"))){Error=TEXT("Choose a material folder under /Game.");return false;}
    for(const TCHAR* Profile:{TEXT("PBR"),TEXT("Emissive"),TEXT("Cutout"),TEXT("Transparent")})
    {
        FString Name=FString(TEXT("Tanuki_"))+Profile,Path=Folder/Name;
        if(FPackageName::DoesPackageExist(Path))continue;
        if(FindPackage(nullptr,*Path)){Error=TEXT("An unsaved package already occupies the material preset path: ")+Path;return false;}
        auto* Package=CreatePackage(*Path);auto* Material=PortableMapMaterialContract::Create(Package,FName(Name),Profile);
        if(!Material){Error=TEXT("Could not construct the portable material.");return false;}
        Material->SetFlags(RF_Public|RF_Standalone);FAssetRegistryModule::AssetCreated(Material);Package->MarkPackageDirty();
        FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension())),true);
        if(!UPackage::SavePackage(Package,Material,*FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension()),Args)){Error=TEXT("Could not save portable material preset.");return false;}
    }
    return true;
}
bool PortableMapAuthoring::RepairMaterials(APortableMapAuthoringRoot* Root,FString& Report)
{
    if(!Root){Report=TEXT("Select a map first.");return false;}
    FScopedTransaction Transaction(NSLOCTEXT("PortableMapMaterials","RepairTransaction","Repair Map Materials"));
    TArray<AActor*> Actors;Root->GetAttachedActors(Actors,false,true);TMap<UMaterialInterface*,UMaterialInstanceConstant*> Converted;TArray<FString> Notes;
    for(auto* Actor:Actors)if(auto* Mesh=Actor->FindComponentByClass<UStaticMeshComponent>())for(int32 Slot=0;Slot<Mesh->GetNumMaterials();++Slot)
    {
        auto* Original=Mesh->GetMaterial(Slot);FString Error;if(PortableMapMaterialContract::Validate(Original,Error))continue;
        if(!Original)
        {auto* Default=PortableMapAuthoring::CreateMaterial(Root,TEXT("PBR"));if(!Default){Report=TEXT("Could not create replacement material.");return false;}Mesh->Modify();Mesh->SetMaterial(Slot,Default);Notes.Add(Actor->GetActorLabel()+TEXT(": missing material replaced with PBR."));continue;}
        UMaterialInstanceConstant* Copy=Converted.FindRef(Original);
        if(!Copy)
        {
            FString Profile=PortableMapMaterialContract::Classify(Original);Copy=PortableMapMaterialContract::Create(Root,MakeUniqueObjectName(Root,UMaterialInstanceConstant::StaticClass(),TEXT("RepairedMaterial")),Profile);
            if(!Copy){Report=TEXT("Could not create repaired material.");return false;}
            auto CopyColor=[&](const TCHAR* Target,std::initializer_list<const TCHAR*> Names){for(auto Name:Names){FLinearColor Value;if(Original->GetVectorParameterValue(FMaterialParameterInfo(Name),Value)){Copy->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(Target),Value);return true;}}return false;};
            auto CopyScalar=[&](const TCHAR* Target,std::initializer_list<const TCHAR*> Names){for(auto Name:Names){float Value;if(Original->GetScalarParameterValue(FMaterialParameterInfo(Name),Value)){Copy->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(Target),Value);return true;}}return false;};
            auto CopyTexture=[&](const TCHAR* Target,std::initializer_list<const TCHAR*> Names){for(auto Name:Names){UTexture* Value=nullptr;if(Original->GetTextureParameterValue(FMaterialParameterInfo(Name),Value)&&Value){Copy->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(Target),Value);return true;}}return false;};
            bool ColorFound=CopyColor(TEXT("baseColorFactor"),{TEXT("baseColorFactor"),TEXT("BaseColor"),TEXT("Base Color"),TEXT("Tint"),TEXT("Color")});
            auto Color=ReadProperty(Original,MP_BaseColor);
            if(Color.Valid)
            {
                Color.Factor.A=Profile==TEXT("Transparent")?ReadProperty(Original,MP_Opacity).Factor.R:1;
                Copy->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("baseColorFactor")),Color.Factor);ColorFound=true;
                if(Color.Image)
                {Copy->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("baseColorTexture")),Color.Image);Copy->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("baseColorScale")),FLinearColor(Color.Scale.X,Color.Scale.Y,0,0));}
            }
            if(Profile==TEXT("Emissive"))
            {
                CopyColor(TEXT("emissiveFactor"),{TEXT("emissiveFactor"),TEXT("EmissiveColor"),TEXT("EmissionColor")});
                auto Emission=ReadProperty(Original,MP_EmissiveColor);
                if(Emission.Valid){Copy->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("emissiveFactor")),Emission.Factor);if(Emission.Image)Copy->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("emissiveTexture")),Emission.Image);}
            }
            for(auto Property:{MP_Metallic,MP_Roughness})
            {auto Value=ReadProperty(Original,Property);if(Value.Valid&&!Value.Image)Copy->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(Property==MP_Metallic?TEXT("metallicFactor"):TEXT("roughnessFactor")),FMath::Clamp(Value.Factor.R,0.f,1.f));}
            for(const TCHAR* Name:{TEXT("metallicFactor"),TEXT("roughnessFactor"),TEXT("alphaCutoff"),TEXT("emissiveStrength")})
            {float Value;if(Original->GetScalarParameterValue(FMaterialParameterInfo(Name),Value))Copy->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(Name),Value);}
            CopyScalar(TEXT("metallicFactor"),{TEXT("metallicFactor"),TEXT("Metallic"),TEXT("Metalness")});
            CopyScalar(TEXT("roughnessFactor"),{TEXT("roughnessFactor"),TEXT("Roughness")});
            if(Profile==TEXT("Cutout"))Copy->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("alphaCutoff")),Original->GetOpacityMaskClipValue());
            if(Profile==TEXT("Emissive"))CopyScalar(TEXT("emissiveStrength"),{TEXT("emissiveStrength"),TEXT("EmissiveIntensity"),TEXT("EmissionStrength")});
            CopyTexture(TEXT("baseColorTexture"),{TEXT("baseColorTexture"),TEXT("BaseColorTexture"),TEXT("BaseColor"),TEXT("Base Color"),TEXT("Albedo"),TEXT("Diffuse")});
            CopyTexture(TEXT("normalTexture"),{TEXT("normalTexture"),TEXT("NormalTexture"),TEXT("Normal"),TEXT("NormalMap")});
            CopyTexture(TEXT("metallicRoughnessTexture"),{TEXT("metallicRoughnessTexture"),TEXT("ORM"),TEXT("OcclusionRoughnessMetallic")});
            CopyTexture(TEXT("occlusionTexture"),{TEXT("occlusionTexture"),TEXT("AmbientOcclusion"),TEXT("AO"),TEXT("ORM")});
            if(Profile==TEXT("Emissive"))CopyTexture(TEXT("emissiveTexture"),{TEXT("emissiveTexture"),TEXT("EmissiveTexture"),TEXT("EmissionTexture")});
            for(const TCHAR* Name:{TEXT("baseColorTexture"),TEXT("metallicRoughnessTexture"),TEXT("normalTexture"),TEXT("occlusionTexture"),TEXT("emissiveTexture")})
            {UTexture* Value=nullptr;if(Original->GetTextureParameterValue(FMaterialParameterInfo(Name),Value))Copy->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(Name),Value);}
            Copy->PostEditChange();Converted.Add(Original,Copy);
            Notes.Add(FString::Printf(TEXT("%s -> %s. Recognized parameters copied; custom graph/procedural/refraction behavior omitted.%s"),*Original->GetName(),*Profile,ColorFound?TEXT(""):TEXT(" Base color could not be evaluated; neutral defaults used.")));
        }
        Mesh->Modify();Mesh->SetMaterial(Slot,Copy);
    }
    Report=Notes.IsEmpty()?TEXT("Materials already satisfy the portable contract."):FString::Join(Notes,TEXT("\n"));return true;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortableSharedMaterialTest,"Radbot.PortableMaps.SharedMaterialInputs",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPortableSharedMaterialTest::RunTest(const FString&)
{
    FString Error;
    for(const FString Profile:{TEXT("PBR"),TEXT("Emissive"),TEXT("Cutout"),TEXT("Transparent")})
    {
        auto* Material=PortableMapMaterialContract::Create(GetTransientPackage(),NAME_None,Profile);
        auto* Data=PortableMapMaterialContract::GetInputs(Material,Error);
        if(!TestNotNull(TEXT("Preset exposes shared inputs"),Data))return false;
        Data->Roughness=.31f;Data->Metallic=.67f;Data->NormalStrength=.65f;Data->Opacity=.42f;Data->EmissionStrength=0;
        if(!TestTrue(*Error,PortableMapMaterialContract::ApplyInputs(Material,Data,Error)))return false;
        TSharedPtr<FJsonObject> Record;
        TestTrue(TEXT("Scalar material exports"),PortableMapMaterialContract::Export(Material,Record,[](UTexture2D*){return 0;},Error));
        auto Pbr=Record->GetObjectField(TEXT("pbrMetallicRoughness"));
        TestTrue(TEXT("Exact roughness fallback"),FMath::IsNearlyEqual((float)Pbr->GetNumberField(TEXT("roughnessFactor")),.31f));
        TestTrue(TEXT("Exact metallic fallback"),FMath::IsNearlyEqual((float)Pbr->GetNumberField(TEXT("metallicFactor")),.67f));
        TestEqual(TEXT("Surface identity including zero emission"),PortableMapMaterialContract::Classify(Material),Profile);
        auto Image=[&](FColor Pixel)
        {auto* Texture=NewObject<UTexture2D>(Material);Texture->Source.Init(1,1,1,1,TSF_BGRA8,reinterpret_cast<const uint8*>(&Pixel));Texture->SRGB=false;return Texture;};
        Data->RoughnessTexture=Image(FColor(64,200,230));Data->MetallicTexture=Image(FColor(192,40,60));Data->NormalTexture=Image(FColor(128,128,255));Data->AOTexture=Image(FColor(153,80,40));
        if(!TestTrue(*Error,PortableMapMaterialContract::ApplyInputs(Material,Data,Error)))return false;
        FImage Packed;TestTrue(TEXT("Packed source retained for portable export"),FImageUtils::GetTexture2DSourceImage(Data->PackedMetalRough,Packed));
        TestEqual(TEXT("Roughness reads source red into glTF green"),Packed.AsBGRA8()[0].G,(uint8)64);
        TestEqual(TEXT("Metallic reads source red into glTF blue"),Packed.AsBGRA8()[0].B,(uint8)192);
        TestTrue(TEXT("Textured material exports"),PortableMapMaterialContract::Export(Material,Record,[](UTexture2D*){return 0;},Error));
        TestEqual(TEXT("Mapped roughness factor"),Record->GetObjectField(TEXT("pbrMetallicRoughness"))->GetNumberField(TEXT("roughnessFactor")),1.0);
        TestTrue(TEXT("Normal strength survives export"),FMath::IsNearlyEqual((float)Record->GetObjectField(TEXT("normalTexture"))->GetNumberField(TEXT("scale")),.65f));
        Data->RoughnessTexture=nullptr;
        TestTrue(TEXT("Removing texture restores retained scalar"),PortableMapMaterialContract::ApplyInputs(Material,Data,Error));
        TestTrue(TEXT("Retained fallback applied"),FMath::IsNearlyEqual(Scalar(Material,TEXT("roughnessFactor"),1),.31f));
    }
    return !HasAnyErrors();
}
static TAutoConsoleVariable<FString> SharedMaterialFixtureDirectory(TEXT("PortableMaps.MaterialFixtureDirectory"),TEXT(""),TEXT("Owned directory containing cross-editor material fixtures."));
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortableSharedMaterialRoundTripTest,"Radbot.PortableMaps.SharedMaterialRoundTrip",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPortableSharedMaterialRoundTripTest::RunTest(const FString&)
{
    const FString Directory=SharedMaterialFixtureDirectory.GetValueOnGameThread();
    if(Directory.IsEmpty()){AddError(TEXT("Set PortableMaps.MaterialFixtureDirectory to generated material fixtures."));return false;}
    for(const FString Name:{TEXT("unity-materials"),TEXT("godot-materials"),TEXT("shared-textured")})
    {
        const FString Folder=TEXT("/Game/Development/Automation/PortableMaterial_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
        UWorld* World=nullptr;FString Error;const bool Imported=PortableMapAuthoring::Import(Directory/(Name+TEXT(".tanukimap")),Folder,false,World,Error,false);
        if(TestTrue(*FString::Printf(TEXT("Import %s: %s"),*Name,*Error),Imported))
        {
            const FString Output=Directory/(Name+TEXT("-unreal.tanukimap"));
            const bool Exported=PortableMapAuthoring::Export(PortableMapAuthoring::FindRoot(World),Output,Error);
            TestTrue(*FString::Printf(TEXT("Export %s: %s"),*Name,*Error),Exported);
            FPortableMapFileInfo Info;if(Exported)TestTrue(TEXT("Runtime accepts material reexport"),APortableMapScene::InspectFile(Output,Info,Error));
        }
        if(World)World->DestroyWorld(false);
        for(TObjectIterator<UObject> It;It;++It)if(It->GetOutermost()->GetName().StartsWith(Folder+TEXT("/"))){It->ClearFlags(RF_Public|RF_Standalone);It->GetOutermost()->SetDirtyFlag(false);}
    }
    return !HasAnyErrors();
}
#endif
