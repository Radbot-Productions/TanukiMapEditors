#include "PortableMapNativeMaterials.h"
#include "PortableMapAuthoring.h"
#include "PortableMapMaterialContract.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionNormalize.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "ContentBrowserItemPath.h"
#include "IContentBrowserSingleton.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"

namespace
{
    const FString Folder=TEXT("/PortableMaps/Materials");
    bool Supported(const FString& Profile){return Profile==TEXT("PBR")||Profile==TEXT("Cutout")||Profile==TEXT("Transparent");}
    FString BasePath(const FString& Profile){return Folder/TEXT("M_Tanuki_")+Profile;}
    FString SamplePath(const FString& Profile){return Folder/TEXT("MI_Tanuki_")+Profile;}
    bool Save(UObject* Asset,FString& Error)
    {
        auto* Package=Asset->GetOutermost();FAssetRegistryModule::AssetCreated(Asset);Package->MarkPackageDirty();
        const FString File=FPackageName::LongPackageNameToFilename(Package->GetName(),FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;
        if(!UPackage::SavePackage(Package,Asset,*File,Args)){Error=TEXT("Could not save native Tanuki material: ")+Package->GetName();return false;}return true;
    }
    bool Fresh(const FString& Path,FString& Error)
    {
        if(FindPackage(nullptr,*Path)&&!FPackageName::DoesPackageExist(Path)){Error=TEXT("An unsaved package occupies the native material path: ")+Path;return false;}return true;
    }
    UTexture2D* DefaultTexture(const FString& Name,FColor Color,bool SRGB,TextureCompressionSettings Compression,FString& Error)
    {
        const FString Path=Folder/Name;
        if(FPackageName::DoesPackageExist(Path))return LoadObject<UTexture2D>(nullptr,*(Path+TEXT(".")+Name));
        if(!Fresh(Path,Error))return nullptr;
        FCreateTexture2DParameters Params;Params.bSRGB=SRGB;Params.CompressionSettings=Compression;Params.bDeferCompression=true;
        auto* Texture=FImageUtils::CreateTexture2D(1,1,TArray<FColor>{Color},CreatePackage(*Path),Name,RF_Public|RF_Standalone,Params);
        return Texture&&Save(Texture,Error)?Texture:nullptr;
    }
    template<typename T>T* Node(UMaterial* Material)
    {auto* Value=NewObject<T>(Material);Material->GetExpressionCollection().AddExpression(Value);return Value;}
    UMaterial* MakeBase(const FString& Profile,UTexture2D* ColorWhite,UTexture2D* DataWhite,UTexture2D* Normal,FString& Error)
    {
        const FString Path=BasePath(Profile),Name=TEXT("M_Tanuki_")+Profile;
        if(FPackageName::DoesPackageExist(Path))return LoadObject<UMaterial>(nullptr,*(Path+TEXT(".")+Name));
        if(!Fresh(Path,Error))return nullptr;
        auto* Material=NewObject<UMaterial>(CreatePackage(*Path),FName(Name),RF_Public|RF_Standalone);
        Material->SetShadingModel(MSM_DefaultLit);Material->TwoSided=false;
        Material->BlendMode=Profile==TEXT("Cutout")?BLEND_Masked:Profile==TEXT("Transparent")?BLEND_Translucent:BLEND_Opaque;
        Material->OpacityMaskClipValue=0;Material->TranslucencyLightingMode=TLM_SurfacePerPixelLighting;
        auto Vector=[&](const TCHAR* Name,FLinearColor Value,const TCHAR* Group)
        {auto* N=Node<UMaterialExpressionVectorParameter>(Material);N->ParameterName=Name;N->DefaultValue=Value;N->Group=Group;return N;};
        auto Scalar=[&](const TCHAR* Name,float Value,const TCHAR* Group)
        {auto* N=Node<UMaterialExpressionScalarParameter>(Material);N->ParameterName=Name;N->DefaultValue=Value;N->Group=Group;return N;};
        auto Product=[&](UMaterialExpression* A,int32 AO,UMaterialExpression* B,int32 BO)
        {auto* N=Node<UMaterialExpressionMultiply>(Material);N->A.Connect(AO,A);N->B.Connect(BO,B);return N;};
        auto RG=[&](UMaterialExpression* Value)
        {auto* N=Node<UMaterialExpressionComponentMask>(Material);N->Input.Connect(0,Value);N->R=N->G=true;N->B=N->A=false;return N;};
        auto* UV=Node<UMaterialExpressionTextureCoordinate>(Material);UV->CoordinateIndex=0;
        auto Append=[&](UMaterialExpression* A,int32 AO,UMaterialExpression* B,int32 BO)
        {auto* N=Node<UMaterialExpressionAppendVector>(Material);N->A.Connect(AO,A);N->B.Connect(BO,B);return N;};
        auto* UVScale=Append(Scalar(TEXT("uvTilingX"),1,TEXT("06 UV")),0,Scalar(TEXT("uvTilingY"),1,TEXT("06 UV")),0);
        auto* UVOffset=Append(Scalar(TEXT("uvOffsetX"),0,TEXT("06 UV")),0,Scalar(TEXT("uvOffsetY"),0,TEXT("06 UV")),0);
        auto* Transform=Node<UMaterialExpressionAdd>(Material);Transform->A.Connect(0,Product(UV,0,UVScale,0));Transform->B.Connect(0,UVOffset);
        auto Texture=[&](const TCHAR* Name,UTexture2D* Default,EMaterialSamplerType Sampler,const TCHAR* Group)
        {auto* N=Node<UMaterialExpressionTextureSampleParameter2D>(Material);N->ParameterName=Name;N->Texture=Default;N->SamplerType=Sampler;N->Group=Group;N->Coordinates.Connect(0,Transform);return N;};
        auto* Color=Vector(TEXT("baseColorFactor"),FLinearColor::White,TEXT("01 Base Color"));
        auto* ColorMap=Texture(TEXT("baseColorTexture"),ColorWhite,SAMPLERTYPE_Color,TEXT("01 Base Color"));
        auto* MR=Texture(TEXT("metallicRoughnessTexture"),DataWhite,SAMPLERTYPE_LinearColor,TEXT("02 Surface"));
        auto* Roughness=Scalar(TEXT("roughnessFactor"),.7f,TEXT("02 Surface"));auto* Metal=Scalar(TEXT("metallicFactor"),0,TEXT("02 Surface"));
        auto* NormalMap=Texture(TEXT("normalTexture"),Normal,SAMPLERTYPE_Normal,TEXT("03 Normal"));
        auto* NormalScale=Scalar(TEXT("normalStrength"),1,TEXT("03 Normal"));NormalScale->SliderMin=0;NormalScale->SliderMax=10;
        auto* Normalized=Node<UMaterialExpressionNormalize>(Material);Normalized->VectorInput.Connect(0,Append(Product(RG(NormalMap),0,NormalScale,0),0,NormalMap,3));
        auto* AO=Texture(TEXT("occlusionTexture"),DataWhite,SAMPLERTYPE_LinearColor,TEXT("04 Occlusion"));
        auto* Data=Material->GetEditorOnlyData();Data->BaseColor.Connect(0,Product(ColorMap,0,Color,0));
        Data->Roughness.Connect(0,Product(MR,2,Roughness,0));Data->Metallic.Connect(0,Product(MR,3,Metal,0));Data->Normal.Connect(0,Normalized);Data->AmbientOcclusion.Connect(1,AO);
        if(Profile==TEXT("PBR"))
        {
            auto* Emission=Vector(TEXT("emissiveFactor"),FLinearColor::Black,TEXT("05 Optional Emission"));
            auto* Strength=Scalar(TEXT("emissiveStrength"),1,TEXT("05 Optional Emission"));
            auto* Image=Texture(TEXT("emissiveTexture"),ColorWhite,SAMPLERTYPE_Color,TEXT("05 Optional Emission"));
            Data->EmissiveColor.Connect(0,Product(Product(Image,0,Emission,0),0,Strength,0));
        }
        if(Profile==TEXT("Cutout"))
        {
            auto* Cutoff=Scalar(TEXT("alphaCutoff"),.5f,TEXT("02 Surface"));auto* Mask=Node<UMaterialExpressionSubtract>(Material);
            Mask->A.Connect(0,Product(ColorMap,4,Color,4));Mask->B.Connect(0,Cutoff);Data->OpacityMask.Connect(0,Mask);
        }
        if(Profile==TEXT("Transparent"))Data->Opacity.Connect(0,Product(ColorMap,4,Color,4));
        int32 Index=0;for(UMaterialExpression* Expression:Material->GetExpressions()){Expression->MaterialExpressionEditorX=-1000+(Index%4)*220;Expression->MaterialExpressionEditorY=(Index/4)*180;++Index;}
        Material->PostEditChange();return Save(Material,Error)?Material:nullptr;
    }
    bool Destination(FString& Path,FString& Error)
    {
        TArray<FString> Folders;FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().GetSelectedPathViewFolders(Folders);
        if(Folders.Num()!=1){Error=TEXT("Select one writable project folder in the Content Browser.");return false;}
        const FContentBrowserItemPath Selected(FStringView(Folders[0]),EContentBrowserPathType::Virtual);
        if(!Selected.HasInternalPath()){Error=TEXT("Select a project content folder, rather than a virtual collection.");return false;}
        Path=Selected.GetInternalPathString();
        if(!(Path==TEXT("/Game")||Path.StartsWith(TEXT("/Game/")))){Error=TEXT("Select one writable project folder in the Content Browser.");return false;}
        FString Disk;if(!FPackageName::TryConvertLongPackageNameToFilename(Path,Disk)){Error=TEXT("The selected content folder is unavailable.");return false;}
        if(IFileManager::Get().DirectoryExists(*Disk)&&IFileManager::Get().IsReadOnly(*Disk)){Error=TEXT("The selected content folder is read-only.");return false;}return true;
    }
}
bool PortableMapNativeMaterials::IsBase(const UMaterialInterface* Material)
{if(!Material)return false;for(const TCHAR* Profile:{TEXT("PBR"),TEXT("Cutout"),TEXT("Transparent")})if(Material->GetOutermost()->GetName()==BasePath(Profile))return true;return false;}
UMaterialInstanceConstant* PortableMapNativeMaterials::Create(UObject* Outer,FName Name,const FString& RequestedProfile)
{
    const FString Profile=RequestedProfile==TEXT("Emissive")?TEXT("PBR"):RequestedProfile;
    if(!Supported(Profile))return nullptr;
    auto* Parent=LoadObject<UMaterialInterface>(nullptr,*(SamplePath(Profile)+TEXT(".MI_Tanuki_")+Profile));
    if(!Parent)return nullptr;
    auto* Material=NewObject<UMaterialInstanceConstant>(Outer,Name,RF_Transactional);Material->SetParentEditorOnly(Parent);
    if(RequestedProfile==TEXT("Emissive"))Material->SetVectorParameterValueEditorOnly(TEXT("emissiveFactor"),FLinearColor::White);
    Material->PostEditChange();return Material;
}
bool PortableMapNativeMaterials::EnsureCollection(FString& Error)
{
    auto* Color=DefaultTexture(TEXT("T_Tanuki_ColorWhite"),FColor::White,true,TC_Default,Error);
    auto* Data=DefaultTexture(TEXT("T_Tanuki_DataWhite"),FColor::White,false,TC_Default,Error);
    auto* Normal=DefaultTexture(TEXT("T_Tanuki_FlatNormal"),FColor(128,128,255,255),false,TC_Normalmap,Error);
    if(!Color||!Data||!Normal)return false;
    for(const TCHAR* Profile:{TEXT("PBR"),TEXT("Cutout"),TEXT("Transparent")})
    {
        auto* Base=MakeBase(Profile,Color,Data,Normal,Error);if(!Base)return false;
        const FString Path=SamplePath(Profile),Name=TEXT("MI_Tanuki_")+FString(Profile);
        if(FPackageName::DoesPackageExist(Path))continue;if(!Fresh(Path,Error))return false;
        auto* Instance=NewObject<UMaterialInstanceConstant>(CreatePackage(*Path),FName(Name),RF_Public|RF_Standalone);Instance->SetParentEditorOnly(Base);
        Instance->PostEditChange();if(!Save(Instance,Error))return false;
    }
    return true;
}
bool PortableMapAuthoring::CanCreateMaterialInCurrentFolder(const FString& Profile,FString& Reason)
{
    if(!Supported(Profile)){Reason=TEXT("Choose PBR, Cutout or Transparent.");return false;}FString Path;if(!Destination(Path,Reason))return false;
    if(!FPackageName::DoesPackageExist(SamplePath(Profile))){Reason=TEXT("The plugin's native material sample is missing; reinstall the toolkit.");return false;}Reason.Reset();return true;
}
bool PortableMapAuthoring::CreateMaterialInCurrentFolder(const FString& Profile,FString& Reason)
{
    if(!CanCreateMaterialInCurrentFolder(Profile,Reason))return false;FString Path;if(!Destination(Path,Reason))return false;
    auto* Parent=LoadObject<UMaterialInterface>(nullptr,*(SamplePath(Profile)+TEXT(".MI_Tanuki_")+Profile));
    if(!Parent){Reason=TEXT("Could not load the native Tanuki material sample.");return false;}
    auto* Factory=NewObject<UMaterialInstanceConstantFactoryNew>();Factory->InitialParent=Parent;
    FString PackageName,AssetName;
    FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().CreateUniqueAssetName(Path/(TEXT("MI_Tanuki_")+Profile),TEXT(""),PackageName,AssetName);
    auto* Created=FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().CreateAsset(AssetName,FPackageName::GetLongPackagePath(PackageName),UMaterialInstanceConstant::StaticClass(),Factory);
    if(!Created){Reason=TEXT("Could not create the Tanuki material instance in the selected folder.");return false;}
    FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().SyncBrowserToAssets(TArray<UObject*>{Created});
    Reason.Reset();return true;
}
