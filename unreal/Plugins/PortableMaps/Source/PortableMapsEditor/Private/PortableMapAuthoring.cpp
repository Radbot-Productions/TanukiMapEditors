#include "PortableMapAuthoring.h"
#include "PortableMapEditorHost.h"
#include "PortableMapEnvironment.h"
#include "PortableMapLighting.h"
#include "Components/LightComponent.h"
#include "PortableMapMaterialContract.h"
#include "PortableMapScene.h"
#include "glTFRuntimeAsset.h"
#include "glTFRuntimeFunctionLibrary.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/CapsuleComponent.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Factories/TextureFactory.h"
#include "Factories/WorldFactory.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "StaticMeshAttributes.h"
#include "UObject/SavePackage.h"
#include "Exporters/GLTFExporter.h"
#include "Options/GLTFExportOptions.h"
#include "ImageUtils.h"
#include "ImageCore.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveDrawingUtils.h"
#include "SceneManagement.h"

APortableMapAuthoringRoot::APortableMapAuthoringRoot()
{SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("MapRoot"))); GetRootComponent()->SetMobility(EComponentMobility::Static); bIsEditorOnlyActor=true; MapId=FGuid::NewGuid().ToString(EGuidFormats::Digits);}
void APortableMapAuthoringRoot::OnConstruction(const FTransform& Transform)
{Super::OnConstruction(Transform);if(GetWorld()&&GetWorld()->WorldType==EWorldType::Editor){if(!bOverrideSun)PortableMapEnvironment::ResetSun(this);PortableMapEditorHost::ConfigureReferences(this);}}
void APortableMapAuthoringRoot::PostEditChangeProperty(FPropertyChangedEvent& Event)
{Super::PostEditChangeProperty(Event);if(Event.GetPropertyName()==GET_MEMBER_NAME_CHECKED(APortableMapAuthoringRoot,BaseLighting)){bOverrideSun=false;PortableMapEnvironment::ResetSun(this);}PortableMapEditorHost::ConfigureReferences(this);}


void APortableMapAuthoringRoot::PostActorCreated()
{
    Super::PostActorCreated();
    if(GetWorld() && GetWorld()->WorldType==EWorldType::Editor)
    {
        auto* Boundary=GetWorld()->SpawnActor<APortableMapAuthoringBoundary>();
        Boundary->AttachToActor(this,FAttachmentTransformRules::KeepRelativeTransform);
        Boundary->SetActorRelativeTransform(FTransform(FQuat::Identity,FVector(0,0,987.5),FVector(60,60,20.25)));
        Boundary->SetActorLabel(TEXT("Map Boundary"));
    }
}
class FPortableBoundarySceneProxy : public FPrimitiveSceneProxy
{
    bool Cylinder;
public:
    FPortableBoundarySceneProxy(const UPortableMapBoundaryPreview* Component):FPrimitiveSceneProxy(Component),Cylinder(CastChecked<APortableMapAuthoringBoundary>(Component->GetOwner())->Shape==EPortableMapBoundaryShape::Cylinder){}
    virtual SIZE_T GetTypeHash() const override {static size_t Unique;return reinterpret_cast<size_t>(&Unique);}
    virtual uint32 GetMemoryFootprint() const override {return sizeof(*this)+GetAllocatedSize();}
    virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
    {FPrimitiveViewRelevance R;R.bDrawRelevance=IsShown(View);R.bDynamicRelevance=true;R.bEditorPrimitiveRelevance=UseEditorCompositing(View);return R;}
    virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views,const FSceneViewFamily& Family,uint32 Visibility,FMeshElementCollector& Collector) const override
    {
        for(int32 I=0;I<Views.Num();++I) if(Visibility&(1<<I))
        {
            auto* PDI=Collector.GetPDI(I);FLinearColor Color(.1f,.8f,1.f,.8f);const FMatrix& T=GetLocalToWorld();
            if(Cylinder) DrawWireCylinder(PDI,T.GetOrigin(),T.GetScaledAxis(EAxis::X),T.GetScaledAxis(EAxis::Y),T.GetScaledAxis(EAxis::Z),Color,50,50,48,SDPG_World,1.5f);
            else DrawWireBox(PDI,T,FBox(FVector(-50),FVector(50)),Color,SDPG_World,1.5f);
        }
    }
};
FPrimitiveSceneProxy* UPortableMapBoundaryPreview::CreateSceneProxy(){return new FPortableBoundarySceneProxy(this);}
FBoxSphereBounds UPortableMapBoundaryPreview::CalcBounds(const FTransform& T) const {return FBoxSphereBounds(FBox(FVector(-50),FVector(50))).TransformBy(T);}
APortableMapAuthoringBoundary::APortableMapAuthoringBoundary()
{
    Preview=CreateDefaultSubobject<UPortableMapBoundaryPreview>(TEXT("BoundaryPreview"));SetRootComponent(Preview);
    Preview->SetMobility(EComponentMobility::Static);Preview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Preview->SetHiddenInGame(true);Preview->SetCastShadow(false);bIsEditorOnlyActor=true;
}
void APortableMapAuthoringBoundary::OnConstruction(const FTransform& Transform)
{Super::OnConstruction(Transform);Preview->MarkRenderStateDirty();}

namespace PortableMapAuthoring
{
APortableMapAuthoringRoot* CreateStarter(UWorld* World)
{
    if(!World || World->WorldType!=EWorldType::Editor) return nullptr;
    auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));if(!Cube) return nullptr;
    FglTFRuntimeConfig MaterialConfig;MaterialConfig.bAllowExternalFiles=false;MaterialConfig.bNoArchive=true;
    auto* MaterialAsset=UglTFRuntimeFunctionLibrary::glTFLoadAssetFromString(TEXT(R"({"asset":{"version":"2.0"},"materials":[{"pbrMetallicRoughness":{"baseColorFactor":[0.5,0.55,0.6,1],"metallicFactor":0,"roughnessFactor":0.7}}]})"),MaterialConfig);
    FglTFRuntimeMaterialsConfig Materials;
    auto* Source=MaterialAsset?Cast<UMaterialInstanceDynamic>(MaterialAsset->LoadMaterial(0,Materials,false)):nullptr;if(!Source)return nullptr;
    auto* Root=World->SpawnActor<APortableMapAuthoringRoot>();Root->SetActorLabel(TEXT("New Map"));
    TArray<AActor*> Children;Root->GetAttachedActors(Children,true,true);
    for(auto* Actor:Children) if(auto* Boundary=Cast<APortableMapAuthoringBoundary>(Actor)) Boundary->SetActorRelativeTransform(FTransform(FQuat::Identity,FVector(0,0,387.5),FVector(20,20,8.25)));
    auto* Floor=World->SpawnActor<AStaticMeshActor>();Floor->SetActorLabel(TEXT("Floor"));Floor->GetStaticMeshComponent()->SetStaticMesh(Cube);Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
    auto* Material=NewObject<UMaterialInstanceConstant>(Floor,TEXT("FloorMaterial"),RF_Transactional);Material->SetParentEditorOnly(Source->Parent);PortableMapMaterialContract::CopyParameters(Source,Material);
    Material->AddAssetUserData(NewObject<UPortableMapMaterialData>(Material));Material->PostEditChange();Floor->GetStaticMeshComponent()->SetMaterial(0,Material);
    Floor->AttachToActor(Root,FAttachmentTransformRules::KeepRelativeTransform);Floor->SetActorRelativeTransform(FTransform(FQuat::Identity,FVector(0,0,-10),FVector(20,20,.2)));
    for(int32 I=0;I<4;++I){auto* Start=World->SpawnActor<APlayerStart>();Start->AttachToActor(Root,FAttachmentTransformRules::KeepRelativeTransform);Start->SetActorRelativeLocation(FVector(0,(I-1.5)*200,100));Start->SetActorLabel(FString::Printf(TEXT("Player Start %d"),I+1));}
    return Root;
}
using JObject=TSharedPtr<FJsonObject>;
using JValue=TSharedPtr<FJsonValue>;
static JValue Object(JObject O) {return MakeShared<FJsonValueObject>(O);}
static TArray<JValue> Array(std::initializer_list<double> Values) {TArray<JValue> A; for(double V:Values) A.Add(MakeShared<FJsonValueNumber>(V)); return A;}
static FString Json(JObject O) {FString S; auto W=TJsonWriterFactory<>::Create(&S); FJsonSerializer::Serialize(O.ToSharedRef(),W); return S;}
static JObject Parse(const FString& S) {JObject O; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(S),O); return O;}
static uint32 U32(const TArray<uint8>& B,int32 O) {return uint32(B[O])|uint32(B[O+1])<<8|uint32(B[O+2])<<16|uint32(B[O+3])<<24;}
static void Write32(TArray<uint8>& B,uint32 V) {for(int I=0;I<4;I++) B.Add(uint8(V>>(I*8)));}
static bool Save(UObject* O)
{FAssetRegistryModule::AssetCreated(O); O->MarkPackageDirty(); FSavePackageArgs A; A.TopLevelFlags=RF_Public|RF_Standalone; FString File=FPackageName::LongPackageNameToFilename(O->GetPackage()->GetName(),O->IsA<UWorld>()?FPackageName::GetMapPackageExtension():FPackageName::GetAssetPackageExtension()); IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true); return UPackage::SavePackage(O->GetPackage(),O,*File,A);}
static FVector Vec(const TArray<JValue>& A) {return FVector(A[0]->AsNumber(),A[1]->AsNumber(),A[2]->AsNumber());}
static FTransform Frame(JObject O) {auto A=O->GetArrayField(TEXT("rotation")); return FTransform(FQuat(A[0]->AsNumber(),A[1]->AsNumber(),A[2]->AsNumber(),A[3]->AsNumber()),Vec(O->GetArrayField(TEXT("position"))));}
static JObject At(JObject O,const TCHAR* Key,int32 Index)
{const TArray<JValue>* A=nullptr; if(!O || !O->TryGetArrayField(Key,A) || !A->IsValidIndex(Index)) return nullptr; return (*A)[Index]->AsObject();}
static bool UnityFrame(JObject N,FTransform& T)
{
    const TSharedPtr<FJsonObject>* P=nullptr,*Q=nullptr,*S=nullptr;
    if(!N || !N->TryGetObjectField(TEXT("position"),P) || !N->TryGetObjectField(TEXT("rotation"),Q) || !N->TryGetObjectField(TEXT("scale"),S)) return false;
    const auto V=[](JObject O,FVector& Out) {double X,Y,Z; if(!O->TryGetNumberField(TEXT("x"),X) || !O->TryGetNumberField(TEXT("y"),Y) || !O->TryGetNumberField(TEXT("z"),Z) || !FMath::IsFinite(X) || !FMath::IsFinite(Y) || !FMath::IsFinite(Z) || FMath::Max3(FMath::Abs(X),FMath::Abs(Y),FMath::Abs(Z))>10000000) return false; Out=FVector(Z,X,Y); return true;};
    FVector Position,Scale,Rotation; double W;
    if(!V(*P,Position) || !V(*S,Scale) || !V(*Q,Rotation) || !(*Q)->TryGetNumberField(TEXT("w"),W) || !FMath::IsFinite(W)) return false;
    FQuat Quat(Rotation.X,Rotation.Y,Rotation.Z,W); if(Quat.SizeSquared()<.0001) return false; Quat.Normalize(); T=FTransform(Quat,Position*100,Scale); return true;
}
static void FrameOut(JObject O,const FTransform& T) {auto P=T.GetTranslation(); auto Q=T.GetRotation(); O->SetArrayField(TEXT("position"),Array({P.X,P.Y,P.Z})); O->SetArrayField(TEXT("rotation"),Array({Q.X,Q.Y,Q.Z,Q.W}));}
APortableMapAuthoringRoot* FindRoot(UWorld* World)
{APortableMapAuthoringRoot* Root=nullptr; if(World) for(TActorIterator<APortableMapAuthoringRoot> It(World);It;++It) {if(Root) return nullptr; Root=*It;} return Root;}
bool Import(const FString& File,const FString& Folder,bool bFork,UWorld*& OutWorld,FString& Error,bool bSaveAssets)
{
    if(!APortableMapScene::IsAccessAllowed(nullptr)) {Error=TEXT("Portable map authoring is disabled by the existing edition/development feature flags"); return false;}
    OutWorld=nullptr; FPortableMapFileInfo Info;
    if(!APortableMapScene::InspectFile(File,Info,Error)) return false;
    if(!Folder.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(Folder) || FindPackage(nullptr,*(Folder/TEXT("Map"))) || FPackageName::DoesPackageExist(Folder/TEXT("Map")) || IFileManager::Get().DirectoryExists(*FPackageName::LongPackageNameToFilename(Folder))) {Error=TEXT("Choose a new, unused /Game asset folder"); return false;}
    TArray<uint8> Bytes; FFileHelper::LoadFileToArray(Bytes,*File); uint32 J=U32(Bytes,8),G=U32(Bytes,12);
    FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()+16),J); auto M=Parse(FString(Text.Length(),Text.Get()));
    TArray<uint8> Glb; Glb.Append(Bytes.GetData()+16+J,G);
    FglTFRuntimeConfig Config; Config.bAllowExternalFiles=false; Config.bNoArchive=true; Config.TransformBaseType=EglTFRuntimeTransformBaseType::Matrix;
    Config.BasisMatrix=FBasisVectorMatrix(FVector(0,0,1),FVector(-1,0,0),FVector(0,1,0),FVector::ZeroVector);
    auto* Loaded=UglTFRuntimeFunctionLibrary::glTFLoadAssetFromData(Glb,Config); if(!Loaded) {Error=TEXT("Cannot decode map geometry"); return false;}
    auto* Package=CreatePackage(*(Folder/TEXT("Map"))); auto* Factory=NewObject<UWorldFactory>(); Factory->WorldType=EWorldType::Inactive; Factory->bInformEngineOfWorld=false; Factory->bCreateWorldPartition=false;
    auto* World=Cast<UWorld>(Factory->FactoryCreateNew(UWorld::StaticClass(),Package,TEXT("Map"),RF_Public|RF_Standalone,nullptr,GWarn));
    if(!World) {Error=TEXT("Cannot create the editor map"); return false;}
    auto* Root=World->SpawnActor<APortableMapAuthoringRoot>(); M->TryGetStringField(TEXT("baseLighting"),Root->BaseLighting); Root->MapId=bFork?FGuid::NewGuid().ToString(EGuidFormats::Digits):M->GetStringField(TEXT("mapId"));
    {const auto& S=Info.BaseSun;Root->bOverrideSun=S.bOverride;Root->SunRotation=S.Rotation.Rotator();Root->SunIntensity=S.Intensity;Root->SunColor=S.Color;Root->bSunShadows=S.bShadows;}
    if(!Root->bOverrideSun)PortableMapEnvironment::ResetSun(Root);
    Root->GetRootComponent()->SetMobility(EComponentMobility::Static);
    Root->SetActorLabel(TEXT("Portable Map Root")); int32 Serial=0;
    TArray<AActor*> AuthorNodes; TMap<FString,int32> BindingNodes;
    const TSharedPtr<FJsonObject>* Authoring=nullptr;
    if(M->TryGetObjectField(TEXT("authoring"),Authoring))
    {
        const TArray<JValue>* Nodes=nullptr,*Bindings=nullptr; int32 Version=0;
        if(!(*Authoring)->TryGetNumberField(TEXT("version"),Version) || Version!=1 || !(*Authoring)->TryGetArrayField(TEXT("nodes"),Nodes) || Nodes->IsEmpty() || Nodes->Num()>30000 || !(*Authoring)->TryGetArrayField(TEXT("bindings"),Bindings)) {Error=TEXT("Unsupported authoring metadata"); return false;}
        int32 BoundaryNode=INDEX_NONE;
        for(auto Value:*Bindings) {auto B=Value->AsObject(); FString Id; int32 Index;
            if(B && B->TryGetStringField(TEXT("id"),Id) && Id==TEXT("boundary") && B->TryGetNumberField(TEXT("node"),Index)) BoundaryNode=Index;}
        TArray<int32> Depth;
        for(int32 I=0;I<Nodes->Num();I++)
        {
            auto N=(*Nodes)[I]->AsObject(); int32 Parent=-2; FString Name; FTransform T;
            if(!N || !N->TryGetNumberField(TEXT("parent"),Parent) || (I==0 ? Parent!=-1 : Parent<0 || Parent>=I) || !N->TryGetStringField(TEXT("name"),Name) || Name.Len()>1024 || !UnityFrame(N,T)) {Error=TEXT("Invalid authoring hierarchy"); return false;}
            Depth.Add(I==0?0:Depth[Parent]+1); if(Depth.Last()>128) {Error=TEXT("Authoring hierarchy exceeds 128 levels"); return false;}
            AActor* Node=I==0?Root:I==BoundaryNode?static_cast<AActor*>(World->SpawnActor<APortableMapAuthoringBoundary>()):World->SpawnActor<AActor>();
            if(I>0) {if(!Node->GetRootComponent()) {auto* C=NewObject<USceneComponent>(Node); C->SetMobility(EComponentMobility::Static); Node->SetRootComponent(C); Node->AddInstanceComponent(C); C->RegisterComponent();} Node->AttachToActor(AuthorNodes[Parent],FAttachmentTransformRules::KeepRelativeTransform); Node->SetActorRelativeTransform(T);}
            Node->SetActorLabel(Name); AuthorNodes.Add(Node);
        }
        TSet<FString> Expected; if(M->HasTypedField<EJson::Object>(TEXT("boundary"))) Expected.Add(TEXT("boundary"));
        for(const TCHAR* Key:{TEXT("meshes"),TEXT("colliders"),TEXT("markers")}) for(auto E:M->GetArrayField(Key)) Expected.Add(E->AsObject()->GetStringField(TEXT("id")));
        if(M->HasTypedField<EJson::Array>(TEXT("lights"))) for(auto E:M->GetArrayField(TEXT("lights"))) Expected.Add(E->AsObject()->GetStringField(TEXT("id")));
        for(auto Value:*Bindings) {auto B=Value->AsObject(); int32 Node=-1; FString Id; if(!B || !B->TryGetStringField(TEXT("id"),Id) || !Expected.Contains(Id) || BindingNodes.Contains(Id) || !B->TryGetNumberField(TEXT("node"),Node) || !AuthorNodes.IsValidIndex(Node)) {Error=TEXT("Invalid authoring binding"); return false;} BindingNodes.Add(Id,Node);}
        if(BindingNodes.Num()!=Expected.Num()) {Error=TEXT("Authoring bindings do not match map entries"); return false;}
    }
    const auto ParentFor=[&](JObject E)->AActor* {auto* Index=BindingNodes.Find(E->GetStringField(TEXT("id"))); return Index?AuthorNodes[*Index]:Root;};
    FglTFRuntimeMaterialsConfig Materials;
    // Persist embedded base-color textures and material parameters; never save transient dynamic instances.
    uint32 N=U32(Glb,12); FUTF8ToTCHAR GText(reinterpret_cast<const ANSICHAR*>(Glb.GetData()+20),N); auto Doc=Parse(FString(GText.Length(),GText.Get()));
    int32 BinaryOffset=28+N;
    const TArray<JValue>* MaterialArray=nullptr;
    if(Doc->TryGetArrayField(TEXT("materials"),MaterialArray)) for(int32 I=0;I<MaterialArray->Num();I++)
    {
        auto* Source=Loaded->LoadMaterial(I,Materials,false); auto* Dynamic=Cast<UMaterialInstanceDynamic>(Source);
        if(!Dynamic) {Error=TEXT("Unsupported imported material"); return false;}
        FString Name=FString::Printf(TEXT("Material_%d"),I); auto* P=CreatePackage(*(Folder/Name)); auto* Mat=NewObject<UMaterialInstanceConstant>(P,*Name,RF_Public|RF_Standalone);
        Mat->SetParentEditorOnly(Dynamic->Parent); PortableMapMaterialContract::CopyParameters(Dynamic,Mat);
        const TSharedPtr<FJsonObject>* PbrField=nullptr;
        auto Pbr=(*MaterialArray)[I]->AsObject()->TryGetObjectField(TEXT("pbrMetallicRoughness"),PbrField)?*PbrField:MakeShared<FJsonObject>();
        // glTF omits defaults: a textured Blender material legitimately has no
        // baseColorFactor (the specified default is opaque white).
        FLinearColor BaseColor=FLinearColor::White;
        if(Pbr->HasField(TEXT("baseColorFactor")))
        {
            const TArray<JValue>* Factor=nullptr;
            if(!Pbr->TryGetArrayField(TEXT("baseColorFactor"),Factor) || Factor->Num()!=4) {Error=TEXT("Invalid material base color factor");return false;}
            for(int32 Channel=0;Channel<4;Channel++)
            {
                double Value;if(!(*Factor)[Channel]->TryGetNumber(Value) || !FMath::IsFinite(Value) || Value<0 || Value>1) {Error=TEXT("Invalid material base color channel");return false;}
                BaseColor.Component(Channel)=Value;
            }
        }
        Mat->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("baseColorFactor")),BaseColor);
        double Metallic=1,Roughness=1; Pbr->TryGetNumberField(TEXT("metallicFactor"),Metallic); Pbr->TryGetNumberField(TEXT("roughnessFactor"),Roughness);
        Mat->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("metallicFactor")),Metallic); Mat->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("roughnessFactor")),Roughness);
        auto* Contract=NewObject<UPortableMapMaterialData>(Mat); FString Mode; (*MaterialArray)[I]->AsObject()->TryGetStringField(TEXT("alphaMode"),Mode); Contract->bMasked=Mode==TEXT("MASK");Contract->Profile=PortableMapMaterialContract::Classify(Mat); Mat->AddAssetUserData(Contract);
        double Cutoff=.5; (*MaterialArray)[I]->AsObject()->TryGetNumberField(TEXT("alphaCutoff"),Cutoff); Mat->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("alphaCutoff")),Cutoff);
        for(const TCHAR* TextureName:{TEXT("baseColorTexture"),TEXT("metallicRoughnessTexture"),TEXT("normalTexture"),TEXT("occlusionTexture"),TEXT("emissiveTexture")})
        {
            const TSharedPtr<FJsonObject>* Texture=nullptr;
            bool PbrTexture=FCString::Strcmp(TextureName,TEXT("baseColorTexture"))==0||FCString::Strcmp(TextureName,TEXT("metallicRoughnessTexture"))==0;
            if(!(PbrTexture?Pbr:(*MaterialArray)[I]->AsObject())->TryGetObjectField(TextureName,Texture))continue;
            int32 Index=(*Texture)->GetIntegerField(TEXT("index")); auto TextureData=At(Doc,TEXT("textures"),Index); if(!TextureData) {Error=TEXT("Invalid texture reference"); return false;}
            auto ImageData=At(Doc,TEXT("images"),TextureData->GetIntegerField(TEXT("source"))); if(!ImageData) {Error=TEXT("Invalid image reference"); return false;}
            auto V=At(Doc,TEXT("bufferViews"),ImageData->GetIntegerField(TEXT("bufferView"))); if(!V) {Error=TEXT("Invalid image buffer reference"); return false;}
            int32 Offset=V->GetIntegerField(TEXT("byteOffset")),Size=V->GetIntegerField(TEXT("byteLength"));
            if(Offset<0 || Size<=0 || int64(BinaryOffset)+Offset+Size>Glb.Num()) {Error=TEXT("Invalid embedded texture view"); return false;}
            FString TName=FString::Printf(TEXT("Texture_%d_%s"),I,TextureName); auto* TP=CreatePackage(*(Folder/TName)); const uint8* Start=Glb.GetData()+BinaryOffset+Offset;
            bool Normal=FCString::Strcmp(TextureName,TEXT("normalTexture"))==0;
            bool ColorTexture=FCString::Strcmp(TextureName,TEXT("baseColorTexture"))==0||FCString::Strcmp(TextureName,TEXT("emissiveTexture"))==0;
            auto* TF=NewObject<UTextureFactory>(); TF->SuppressImportOverwriteDialog();
            TF->ColorSpaceMode=ColorTexture?ETextureSourceColorSpace::SRGB:ETextureSourceColorSpace::Linear;
            TF->CompressionSettings=Normal?TC_Normalmap:TC_Default;
            TF->bDeferCompression=true;
            auto* Tex=Cast<UTexture2D>(TF->FactoryCreateBinary(UTexture2D::StaticClass(),TP,*TName,RF_Public|RF_Standalone,nullptr,TEXT("png"),Start,Start+Size,GWarn));
            if(!Tex){Error=TEXT("Cannot import material texture");return false;}
            Tex->PreEditChange(nullptr);
            double SamplerIndex=-1;TextureData->TryGetNumberField(TEXT("sampler"),SamplerIndex);
            if(auto Sampler=At(Doc,TEXT("samplers"),SamplerIndex))
            {
                auto Wrap=[](int32 Value){return Value==33071?TA_Clamp:Value==33648?TA_Mirror:TA_Wrap;};
                double S=10497,T=10497,Filter=9729;Sampler->TryGetNumberField(TEXT("wrapS"),S);Sampler->TryGetNumberField(TEXT("wrapT"),T);Sampler->TryGetNumberField(TEXT("magFilter"),Filter);
                Tex->AddressX=Wrap(S);Tex->AddressY=Wrap(T);Tex->Filter=Filter==9728?TF_Nearest:TF_Bilinear;
            }
            Tex->PostEditChange();
            if(bSaveAssets&&!Save(Tex)){Error=TEXT("Cannot save imported material texture");return false;}
            Mat->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TextureName),Tex);
        }
        Mat->PostEditChange(); if(bSaveAssets && !Save(Mat)) {Error=TEXT("Cannot save the imported material"); return false;} Materials.MaterialsOverrideMap.Add(I,Mat);
    }
    for(auto Value:M->GetArrayField(TEXT("meshes")))
    {
        auto E=Value->AsObject(); FString Name=FString::Printf(TEXT("Mesh_%d"),Serial++); auto* P=CreatePackage(*(Folder/Name));
        FglTFRuntimeStaticMeshConfig MC; MC.CacheMode=EglTFRuntimeCacheMode::None; MC.Outer=P; MC.MaterialsConfig=Materials; MC.bGenerateStaticMeshDescription=true; MC.bAllowCPUAccess=true; MC.bBuildComplexCollision=E->GetBoolField(TEXT("collision")); MC.CollisionComplexity=CTF_UseComplexAsSimple;
        auto* Mesh=Loaded->LoadStaticMesh(E->GetIntegerField(TEXT("mesh")),MC); if(!Mesh) {Error=TEXT("Cannot import static mesh"); return false;}
        auto* Parent=ParentFor(E); FMatrix NodeFrame=Parent->GetActorTransform().ToMatrixWithScale();
        if(FMath::Abs(NodeFrame.Determinant())<.000001) {Error=TEXT("Degenerate authoring transform"); return false;}
        auto* Description=Mesh->GetMeshDescription(0);
        if(!Description) {Error=TEXT("The importer did not generate editable mesh data"); return false;}
        FStaticMeshAttributes Attributes(*Description); auto Positions=Attributes.GetVertexPositions(); auto Normals=Attributes.GetVertexInstanceNormals();
        for(FVertexID Vertex:Description->Vertices().GetElementIDs()) Positions[Vertex]=FVector3f(NodeFrame.InverseTransformPosition(FVector(Positions[Vertex])));
        for(FVertexInstanceID Vertex:Description->VertexInstances().GetElementIDs()) Normals[Vertex]=FVector3f(FVector(NodeFrame.GetTransposed().TransformVector(FVector(Normals[Vertex]))).GetSafeNormal());
        if(NodeFrame.Determinant()<0) Description->ReverseAllPolygonFacing(); Mesh->CommitMeshDescription(0); Mesh->Build(false);
        Mesh->Rename(*Name,P,REN_DontCreateRedirectors|REN_NonTransactional); Mesh->SetFlags(RF_Public|RF_Standalone); Mesh->ClearFlags(RF_Transient);
        if(bSaveAssets && !Save(Mesh)) {Error=TEXT("Cannot save imported mesh"); return false;}
        auto* Actor=World->SpawnActor<AStaticMeshActor>(); Actor->SetActorLabel(E->GetStringField(TEXT("id"))); Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
        Actor->AttachToActor(Parent,FAttachmentTransformRules::KeepRelativeTransform);
        if(!E->GetBoolField(TEXT("visible"))) {Actor->Tags.Add(TEXT("PortableCollisionOnly")); Actor->SetActorHiddenInGame(true);}
        Actor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
        Actor->GetStaticMeshComponent()->SetCollisionEnabled(E->GetBoolField(TEXT("collision"))?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
    }
    for(auto Value:M->GetArrayField(TEXT("colliders")))
    {
        auto E=Value->AsObject(); auto* Actor=World->SpawnActor<AActor>(); auto Type=E->GetStringField(TEXT("type")); UShapeComponent* C=nullptr;
        if(Type==TEXT("box")) {auto* S=NewObject<UBoxComponent>(Actor); S->SetBoxExtent(Vec(E->GetArrayField(TEXT("size")))*.5); C=S;}
        else if(Type==TEXT("sphere")) {auto* S=NewObject<USphereComponent>(Actor); S->SetSphereRadius(E->GetNumberField(TEXT("radius"))); C=S;}
        else {auto* S=NewObject<UCapsuleComponent>(Actor); S->SetCapsuleSize(E->GetNumberField(TEXT("radius")),E->GetNumberField(TEXT("halfHeight"))); C=S;}
        C->SetCollisionProfileName(TEXT("BlockAll"));
        Actor->SetRootComponent(C); Actor->AddInstanceComponent(C); C->RegisterComponent(); Actor->SetActorTransform(Frame(E)); Actor->SetActorLabel(E->GetStringField(TEXT("id"))); Actor->AttachToActor(ParentFor(E),FAttachmentTransformRules::KeepWorldTransform);
    }
    for(auto Value:M->GetArrayField(TEXT("markers")))
    {auto E=Value->AsObject(); auto* Start=World->SpawnActor<APlayerStart>(); Start->SetActorTransform(Frame(E)); Start->PlayerStartTag=FName(*E->GetStringField(TEXT("spawnTag"))); Start->AttachToActor(ParentFor(E),FAttachmentTransformRules::KeepWorldTransform);}
    if(M->HasTypedField<EJson::Object>(TEXT("boundary")))
    {
        auto E=M->GetObjectField(TEXT("boundary"));
        auto* Boundary=Cast<APortableMapAuthoringBoundary>(ParentFor(E));
        if(!Boundary) {Boundary=World->SpawnActor<APortableMapAuthoringBoundary>(); Boundary->AttachToActor(Root,FAttachmentTransformRules::KeepRelativeTransform);}
        FTransform T=Frame(E); T.SetScale3D(Vec(E->GetArrayField(TEXT("size")))*.01);
        Boundary->SetActorTransform(T*Root->GetActorTransform()); Boundary->SetActorLabel(TEXT("Map Boundary"));
        Boundary->Shape=E->GetStringField(TEXT("shape"))==TEXT("box")?EPortableMapBoundaryShape::Box:EPortableMapBoundaryShape::Cylinder;
        Boundary->OnConstruction(Boundary->GetActorTransform());
    }
    else
    {
        auto* Boundary=World->SpawnActor<APortableMapAuthoringBoundary>();
        Boundary->AttachToActor(Root,FAttachmentTransformRules::KeepRelativeTransform);
        Boundary->SetActorRelativeTransform(FTransform(FQuat::Identity,FVector(0,0,987.5),FVector(60,60,20.25)));
        Boundary->SetActorLabel(TEXT("Map Boundary"));
    }
    if(M->HasTypedField<EJson::Array>(TEXT("lights"))) for(auto Value:M->GetArrayField(TEXT("lights")))
    {
        FPortableMapLight V;if(!PortableMapLighting::Read(Value->AsObject(),V)) {Error=TEXT("Invalid portable light");return false;}
        auto* Actor=World->SpawnActor<AActor>();auto* C=PortableMapLighting::Create(Actor,V);
        Actor->SetRootComponent(C);Actor->AddInstanceComponent(C);C->RegisterComponent();C->SetVisibility(true);
        Actor->SetActorTransform(V.Frame*Root->GetActorTransform());Actor->AttachToActor(ParentFor(Value->AsObject()),FAttachmentTransformRules::KeepWorldTransform);Actor->SetActorLabel(V.Id);
    }
    // Preview illumination stays outside the export root; authored lights travel with the map.
    if(bSaveAssets && !Save(World)) {Error=TEXT("Cannot save imported map"); return false;}
    PortableMapEditorHost::ConfigureReferences(Root);OutWorld=World; Error.Reset(); return true;
}
bool Export(APortableMapAuthoringRoot* Root,const FString& File,FString& Error)
{
    if(!APortableMapScene::IsAccessAllowed(nullptr)) {Error=TEXT("Portable map authoring is disabled by the existing edition/development feature flags"); return false;}
    const auto Fail=[&](const TCHAR* Why){Error=Why; return false;};
    if(!Root || Root->MapId.IsEmpty() || Root->MapId.Len()>128 || !FPaths::GetExtension(File).Equals(TEXT("tanukimap"),ESearchCase::IgnoreCase)) return Fail(TEXT("Choose a map root and a .tanukimap destination"));
    if(!Root->GetActorScale3D().Equals(FVector::OneVector)) return Fail(TEXT("The map root must have unit scale"));
    TArray<AActor*> Actors; Root->GetAttachedActors(Actors,true,true); TSet<AActor*> Geometry; TMap<FString,AActor*> Names;
    Actors.RemoveAll([](const AActor* Actor){return Actor->Tags.Contains(TEXT("Tanuki.Authoring.Preview"))||Actor->Tags.Contains(TEXT("Tanuki.Authoring.ScaleReference"))||Actor->Tags.Contains(TEXT("Tanuki.Authoring.BaseLighting"));});
    TArray<JValue> Colliders,Markers,Entries,Lights;
    int32 Serial=0; APortableMapAuthoringBoundary* Boundary=nullptr;
    for(auto* A:Actors)
    {
        TArray<ULightComponent*> LightComponents;A->GetComponents(LightComponents);
        for(auto* C:LightComponents)
        {
            FPortableMapLight V;V.Id=FString::Printf(TEXT("light-%d"),Serial++);
            if(!PortableMapLighting::Capture(C,Root->GetActorTransform(),V,Error)) return false;
            Lights.Add(Object(PortableMapLighting::Write(V)));
        }
        if(Lights.Num()>256) return Fail(TEXT("Portable maps support up to 256 lights"));
        if(auto* B=Cast<APortableMapAuthoringBoundary>(A)) {if(Boundary) return Fail(TEXT("A map requires exactly one Map Boundary")); Boundary=B; continue;}
        if(auto* Start=Cast<APlayerStart>(A))
        {auto E=MakeShared<FJsonObject>(); FString Id=FString::Printf(TEXT("start-%d"),Serial++); E->SetStringField(TEXT("id"),Id); E->SetStringField(TEXT("type"),TEXT("playerStart")); E->SetStringField(TEXT("spawnTag"),Start->PlayerStartTag.IsNone()?TEXT(""):Start->PlayerStartTag.ToString()); FrameOut(E,Start->GetActorTransform().GetRelativeTransform(Root->GetActorTransform())); Markers.Add(Object(E)); continue;}
        TArray<UStaticMeshComponent*> Meshes; A->GetComponents(Meshes);
        if(!Meshes.IsEmpty()) {if(Names.Contains(A->GetActorLabel())) return Fail(TEXT("Give each mesh actor a unique label before exporting")); Names.Add(A->GetActorLabel(),A); Geometry.Add(A);}
        TArray<UShapeComponent*> Shapes; A->GetComponents(Shapes);
        for(auto* Shape:Shapes)
        {
            auto E=MakeShared<FJsonObject>(); E->SetStringField(TEXT("id"),FString::Printf(TEXT("collider-%d"),Serial++));
            FTransform T=Shape->GetComponentTransform().GetRelativeTransform(Root->GetActorTransform()); FVector S=T.GetScale3D().GetAbs(); T.SetScale3D(FVector::OneVector); FrameOut(E,T);
            if(auto* B=Cast<UBoxComponent>(Shape)) {E->SetStringField(TEXT("type"),TEXT("box")); auto V=B->GetUnscaledBoxExtent()*S*2; E->SetArrayField(TEXT("size"),Array({V.X,V.Y,V.Z}));}
            else if(auto* Sphere=Cast<USphereComponent>(Shape)) {E->SetStringField(TEXT("type"),TEXT("sphere")); E->SetNumberField(TEXT("radius"),Sphere->GetUnscaledSphereRadius()*S.GetMax());}
            else if(auto* Capsule=Cast<UCapsuleComponent>(Shape)) {E->SetStringField(TEXT("type"),TEXT("capsule")); E->SetNumberField(TEXT("radius"),Capsule->GetScaledCapsuleRadius()); E->SetNumberField(TEXT("halfHeight"),Capsule->GetScaledCapsuleHalfHeight());}
            else return Fail(TEXT("Unsupported collider"));
            Colliders.Add(Object(E));
        }
    }
    if(!Boundary) return Fail(TEXT("A map requires exactly one Map Boundary"));
    if(Geometry.IsEmpty() || Markers.Num()<4) return Fail(TEXT("Attach static meshes and at least four Player Starts under the map root"));
    auto BoundaryRecord=MakeShared<FJsonObject>();
    FTransform BoundaryFrame=Boundary->GetActorTransform().GetRelativeTransform(Root->GetActorTransform());
    FVector BoundarySize=BoundaryFrame.GetScale3D()*100;
    if(BoundarySize.GetMin()<=0 || BoundarySize.ContainsNaN()) return Fail(TEXT("Map Boundary dimensions must be positive"));
    if(Boundary->Shape==EPortableMapBoundaryShape::Cylinder && !FMath::IsNearlyEqual(BoundarySize.X,BoundarySize.Y,.1)) return Fail(TEXT("Cylinder boundary X and Y dimensions must match"));
    BoundaryFrame.SetScale3D(FVector::OneVector); FrameOut(BoundaryRecord,BoundaryFrame);
    BoundaryRecord->SetStringField(TEXT("id"),TEXT("boundary")); BoundaryRecord->SetStringField(TEXT("shape"),Boundary->Shape==EPortableMapBoundaryShape::Box?TEXT("box"):TEXT("cylinder"));
    BoundaryRecord->SetArrayField(TEXT("size"),Array({BoundarySize.X,BoundarySize.Y,BoundarySize.Z}));
    const FString Scratch=FPaths::ProjectSavedDir()/TEXT("PortableMaps/EditorExports")/FGuid::NewGuid().ToString(EGuidFormats::Digits); IFileManager::Get().MakeDirectory(*Scratch,true);
    ON_SCOPE_EXIT {IFileManager::Get().DeleteDirectory(*Scratch,false,true);};
    auto* Options=NewObject<UGLTFExportOptions>(); Options->bSkipNearDefaultValues=false; Options->TextureImageFormat=EGLTFTextureImageFormat::None; Options->BakeMaterialInputs=EGLTFMaterialBakeMode::Disabled; Options->bExportHiddenInGame=true; Options->bExportLights=false; Options->bExportCameras=false; Options->bExportSourceModel=true; Options->bExportVertexColors=false;
    FGLTFExportMessages Messages;
    if(!UGLTFExporter::ExportToGLTF(Root->GetWorld(),Scratch/TEXT("scene.glb"),Options,Geometry,Messages)) return Fail(TEXT("Unreal could not export the selected static geometry"));
    TArray<uint8> Glb; if(!FFileHelper::LoadFileToArray(Glb,*(Scratch/TEXT("scene.glb"))) || Glb.Num()<28) return Fail(TEXT("Unreal did not produce a GLB"));
    int32 JsonSize=U32(Glb,12); if(JsonSize<=0 || JsonSize>Glb.Num()-28) return Fail(TEXT("Invalid exported GLB"));
    FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Glb.GetData()+20),JsonSize); auto D=Parse(FString(Text.Length(),Text.Get())); if(!D) return Fail(TEXT("Invalid exported scene JSON"));
    TArray<uint8> Bin; Bin.Append(Glb.GetData()+28+JsonSize,Glb.Num()-28-JsonSize);
    auto Accessors=D->GetArrayField(TEXT("accessors")); auto Views=D->GetArrayField(TEXT("bufferViews")); auto Nodes=D->GetArrayField(TEXT("nodes")); auto Meshes=D->GetArrayField(TEXT("meshes"));
    TArray<JValue> ExportMaterials,Images,Textures;
    const TArray<JValue>* Optional=nullptr; if(D->TryGetArrayField(TEXT("images"),Optional)) Images=*Optional; if(D->TryGetArrayField(TEXT("textures"),Optional)) Textures=*Optional;
    TMap<UMaterialInterface*,int32> MaterialIndices;
    FString MaterialError;
    const auto MaterialIndex=[&](UMaterialInterface* Material)->int32
    {
        if(auto* Existing=MaterialIndices.Find(Material))return *Existing;
        JObject Result;
        auto AddTexture=[&](UTexture2D* Texture)->int32
        {
            FImage Image;TArray64<uint8> Png;
            if(!Texture||!FImageUtils::GetTexture2DSourceImage(Texture,Image)||!FImageUtils::CompressImage(Png,TEXT("png"),Image)||Png.Num()>MAX_int32)return INDEX_NONE;
            while(Bin.Num()%4)Bin.Add(0);int32 Offset=Bin.Num();Bin.Append(Png.GetData(),Png.Num());
            auto View=MakeShared<FJsonObject>();View->SetNumberField(TEXT("buffer"),0);View->SetNumberField(TEXT("byteOffset"),Offset);View->SetNumberField(TEXT("byteLength"),Png.Num());Views.Add(Object(View));
            auto JImage=MakeShared<FJsonObject>();JImage->SetStringField(TEXT("mimeType"),TEXT("image/png"));JImage->SetNumberField(TEXT("bufferView"),Views.Num()-1);Images.Add(Object(JImage));
            auto JTexture=MakeShared<FJsonObject>();JTexture->SetNumberField(TEXT("source"),Images.Num()-1);return Textures.Add(Object(JTexture));
        };
        if(!PortableMapMaterialContract::Export(Material,Result,AddTexture,MaterialError))return INDEX_NONE;
        int32 Index=ExportMaterials.Add(Object(Result));MaterialIndices.Add(Material,Index);return Index;
    };
    TArray<int32> Parents; Parents.Init(INDEX_NONE,Nodes.Num());
    for(int32 I=0;I<Nodes.Num();I++) {const TArray<JValue>* Children=nullptr; if(Nodes[I]->AsObject()->TryGetArrayField(TEXT("children"),Children)) for(auto Child:*Children) {int32 K=Child->AsNumber(); if(!Parents.IsValidIndex(K) || Parents[K]!=INDEX_NONE || K==I) return Fail(TEXT("Invalid exported hierarchy")); Parents[K]=I;}}
    TArray<FMatrix> Frames; TArray<FString> Owners; Frames.SetNum(Nodes.Num()); Owners.SetNum(Nodes.Num()); TSet<int32> Visiting,Done;
    TFunction<bool(int32)> Resolve=[&](int32 I)
    {
        if(Done.Contains(I)) return true; if(Visiting.Contains(I)) return false; Visiting.Add(I);
        auto N=Nodes[I]->AsObject(); FVector P=FVector::ZeroVector,S=FVector::OneVector; FQuat Q=FQuat::Identity; const TArray<JValue>* A=nullptr;
        if(N->TryGetArrayField(TEXT("translation"),A)) P=Vec(*A); if(N->TryGetArrayField(TEXT("scale"),A)) S=Vec(*A);
        if(N->TryGetArrayField(TEXT("rotation"),A)) Q=FQuat((*A)[0]->AsNumber(),(*A)[1]->AsNumber(),(*A)[2]->AsNumber(),(*A)[3]->AsNumber());
        Frames[I]=FTransform(Q,P,S).ToMatrixWithScale();
        FString Name; N->TryGetStringField(TEXT("name"),Name); Owners[I]=Names.Contains(Name)?Name:TEXT("");
        if(Parents[I]!=INDEX_NONE) {if(!Resolve(Parents[I])) return false; Frames[I]*=Frames[Parents[I]]; if(Owners[I].IsEmpty()) Owners[I]=Owners[Parents[I]];}
        Visiting.Remove(I); Done.Add(I); return true;
    };
    TArray<JValue> NewMeshes,NewNodes,SceneNodes,AuthNodes,Bindings;
    auto AuthRoot=MakeShared<FJsonObject>(); AuthRoot->SetStringField(TEXT("name"),Root->GetActorLabel()); AuthRoot->SetNumberField(TEXT("parent"),-1);
    const auto MetadataTransform=[](JObject N,FTransform T)
    {
        FVector P=T.GetTranslation()*.01,S=T.GetScale3D(); FQuat Q=T.GetRotation(); auto V=MakeShared<FJsonObject>(); V->SetNumberField(TEXT("x"),P.Y); V->SetNumberField(TEXT("y"),P.Z); V->SetNumberField(TEXT("z"),P.X); N->SetObjectField(TEXT("position"),V);
        V=MakeShared<FJsonObject>(); V->SetNumberField(TEXT("x"),S.Y); V->SetNumberField(TEXT("y"),S.Z); V->SetNumberField(TEXT("z"),S.X); N->SetObjectField(TEXT("scale"),V);
        V=MakeShared<FJsonObject>(); V->SetNumberField(TEXT("x"),Q.Y); V->SetNumberField(TEXT("y"),Q.Z); V->SetNumberField(TEXT("z"),Q.X); V->SetNumberField(TEXT("w"),Q.W); N->SetObjectField(TEXT("rotation"),V);
    };
    MetadataTransform(AuthRoot,FTransform::Identity); AuthNodes.Add(Object(AuthRoot));
    TMap<AActor*,int32> ActorNodes; ActorNodes.Add(Root,0);
    TFunction<int32(AActor*)> CaptureActor=[&](AActor* A)->int32
    {
        if(auto* Found=ActorNodes.Find(A)) return *Found;
        AActor* Parent=A->GetAttachParentActor(); if(!Parent || !Actors.Contains(Parent)) Parent=Root;
        int32 ParentIndex=CaptureActor(Parent); auto N=MakeShared<FJsonObject>(); N->SetStringField(TEXT("name"),A->GetActorLabel()); N->SetNumberField(TEXT("parent"),ParentIndex); MetadataTransform(N,A->GetActorTransform().GetRelativeTransform(Parent->GetActorTransform()));
        int32 Index=AuthNodes.Add(Object(N)); ActorNodes.Add(A,Index); return Index;
    };
    for(AActor* A:Actors) CaptureActor(A);
    for(int32 I=0;I<Nodes.Num();I++)
    {
        auto N=Nodes[I]->AsObject(); double MeshIndex; if(!N->TryGetNumberField(TEXT("mesh"),MeshIndex)) continue;
        if(!Resolve(I) || !Names.Contains(Owners[I]) || !Meshes.IsValidIndex(MeshIndex)) return Fail(TEXT("Unsupported exported mesh node"));
        auto* Owner=Names[Owners[I]]; auto Source=Meshes[MeshIndex]->AsObject(); auto Target=Parse(Json(Source));
        UStaticMeshComponent* Component=nullptr; TArray<UStaticMeshComponent*> Components; Owner->GetComponents(Components); FString NodeName; N->TryGetStringField(TEXT("name"),NodeName);
        for(auto* C:Components) if((C==Owner->GetRootComponent() && NodeName==Owner->GetActorLabel()) || C->GetName()==NodeName) Component=C;
        if(!Component) return Fail(TEXT("Cannot associate exported mesh with its authoring component"));
        int32 Section=0;
        for(auto Primitive:Target->GetArrayField(TEXT("primitives")))
        {
            auto Part=Primitive->AsObject(); auto Attr=Part->GetObjectField(TEXT("attributes"));
            Attr->RemoveField(TEXT("TANGENT")); // Rebuild the tangent basis after transform baking, as Unity exports do.
            if(Owner->ActorHasTag(TEXT("PortableCollisionOnly"))) Part->RemoveField(TEXT("material"));
            else
            {
                int32 Slot=Component->GetStaticMesh()->GetSectionInfoMap().Get(0,Section).MaterialIndex;
                int32 Index=MaterialIndex(Component->GetMaterial(Slot));
                if(Index==INDEX_NONE) return Fail(*(Component->GetOwner()->GetActorLabel()+TEXT(" / material slot ")+FString::FromInt(Slot)+TEXT(": ")+MaterialError));
                Part->SetNumberField(TEXT("material"),Index);
            }
            // The geometry below is transformed into the map frame; regenerate
            // tangent space from its final positions, normals and UVs on import.
            Attr->RemoveField(TEXT("TANGENT"));
            Section++;
            for(const TCHAR* Key:{TEXT("POSITION"),TEXT("NORMAL")})
            {
                double Index; if(!Attr->TryGetNumberField(Key,Index)) continue; auto A=Accessors[Index]->AsObject(); auto V=Views[A->GetIntegerField(TEXT("bufferView"))]->AsObject();
                if(A->GetIntegerField(TEXT("componentType"))!=5126 || A->GetStringField(TEXT("type"))!=TEXT("VEC3")) return Fail(TEXT("Exported position/normal quantization is unsupported"));
                int32 Count=A->GetIntegerField(TEXT("count")),Offset=0,Extra=0,Stride=12; V->TryGetNumberField(TEXT("byteOffset"),Offset); A->TryGetNumberField(TEXT("byteOffset"),Extra); V->TryGetNumberField(TEXT("byteStride"),Stride);
                if(Count<=0 || int64(Offset)+Extra+int64(Count-1)*Stride+12>Bin.Num()) return Fail(TEXT("Invalid exported vertex buffer"));
                while(Bin.Num()%4) Bin.Add(0); int32 NewOffset=Bin.Num(); FVector Min(BIG_NUMBER),Max(-BIG_NUMBER);
                for(int32 K=0;K<Count;K++)
                {
                    FVector3f Raw; FMemory::Memcpy(&Raw,Bin.GetData()+Offset+Extra+K*Stride,12); FVector Value(Raw);
                    if(FCString::Strcmp(Key,TEXT("POSITION"))==0) {Value=FVector(Frames[I].TransformPosition(Value)); FVector UE(Value.X,Value.Z,Value.Y); UE*=100; UE=Root->GetActorTransform().InverseTransformPosition(UE); Value=FVector(-UE.Y,UE.Z,UE.X)*.01;}
                    else {Value=FVector(Frames[I].Inverse().GetTransposed().TransformVector(Value)).GetSafeNormal(); FVector UE(Value.X,Value.Z,Value.Y); UE=Root->GetActorQuat().UnrotateVector(UE); Value=FVector(-UE.Y,UE.Z,UE.X);}
                    Raw=FVector3f(Value); Bin.Append(reinterpret_cast<uint8*>(&Raw),12); Min=Min.ComponentMin(Value); Max=Max.ComponentMax(Value);
                }
                auto NV=MakeShared<FJsonObject>(); NV->SetNumberField(TEXT("buffer"),0); NV->SetNumberField(TEXT("byteOffset"),NewOffset); NV->SetNumberField(TEXT("byteLength"),Count*12); Views.Add(Object(NV));
                auto NA=Parse(Json(A)); NA->SetNumberField(TEXT("bufferView"),Views.Num()-1); NA->SetNumberField(TEXT("byteOffset"),0); NA->SetArrayField(TEXT("min"),Array({Min.X,Min.Y,Min.Z})); NA->SetArrayField(TEXT("max"),Array({Max.X,Max.Y,Max.Z})); Accessors.Add(Object(NA)); Attr->SetNumberField(Key,Accessors.Num()-1);
            }
            if(Frames[I].Determinant()<0)
            {
                auto A=Accessors[Part->GetIntegerField(TEXT("indices"))]->AsObject(); auto V=Views[A->GetIntegerField(TEXT("bufferView"))]->AsObject(); int32 Count=A->GetIntegerField(TEXT("count")),Type=A->GetIntegerField(TEXT("componentType"));
                int32 Width=Type==5121?1:Type==5123?2:Type==5125?4:0,Offset=0,Extra=0,Stride=Width;
                V->TryGetNumberField(TEXT("byteOffset"),Offset); A->TryGetNumberField(TEXT("byteOffset"),Extra); V->TryGetNumberField(TEXT("byteStride"),Stride);
                if(!Width || Count%3 || int64(Offset)+Extra+int64(Count-1)*Stride+Width>Bin.Num()) return Fail(TEXT("Unsupported exported index buffer"));
                TArray<uint8> Reversed;
                for(int32 K=0;K<Count;K+=3) for(int32 Corner=2;Corner>=0;Corner--) Reversed.Append(Bin.GetData()+Offset+Extra+(K+Corner)*Stride,Width);
                while(Bin.Num()%4) Bin.Add(0); int32 NewOffset=Bin.Num(); Bin.Append(Reversed);
                auto NV=MakeShared<FJsonObject>(); NV->SetNumberField(TEXT("buffer"),0); NV->SetNumberField(TEXT("byteOffset"),NewOffset); NV->SetNumberField(TEXT("byteLength"),Reversed.Num()); Views.Add(Object(NV));
                auto NA=Parse(Json(A)); NA->SetNumberField(TEXT("bufferView"),Views.Num()-1); NA->SetNumberField(TEXT("byteOffset"),0); Accessors.Add(Object(NA)); Part->SetNumberField(TEXT("indices"),Accessors.Num()-1);
            }
        }
        FString Id=FString::Printf(TEXT("mesh-%d"),Serial++); auto E=MakeShared<FJsonObject>(); E->SetStringField(TEXT("id"),Id); E->SetNumberField(TEXT("mesh"),NewMeshes.Num()); E->SetBoolField(TEXT("visible"),!Owner->ActorHasTag(TEXT("PortableCollisionOnly"))); E->SetBoolField(TEXT("collision"),Owner->ActorHasTag(TEXT("PortableCollisionOnly")) || Component->GetCollisionEnabled()!=ECollisionEnabled::NoCollision); Entries.Add(Object(E));
        auto NewNode=MakeShared<FJsonObject>(); NewNode->SetNumberField(TEXT("mesh"),NewMeshes.Num()); NewNodes.Add(Object(NewNode)); SceneNodes.Add(MakeShared<FJsonValueNumber>(NewMeshes.Num())); NewMeshes.Add(Object(Target));
        auto AN=MakeShared<FJsonObject>(); AN->SetStringField(TEXT("name"),Component->GetName()); AN->SetNumberField(TEXT("parent"),ActorNodes[Owner]); MetadataTransform(AN,Component->GetComponentTransform().GetRelativeTransform(Owner->GetActorTransform())); AuthNodes.Add(Object(AN));
        auto B=MakeShared<FJsonObject>(); B->SetStringField(TEXT("id"),Id); B->SetNumberField(TEXT("node"),AuthNodes.Num()-1); Bindings.Add(Object(B));
    }
    for(const auto& List:{Colliders,Markers,Lights}) for(auto Value:List)
    {
        // Primitive physics and starts use canonical map-space frames across engines.
        // Unreal's composed FTransform and Unity's affine hierarchy differ under non-uniform scale.
        auto E=Value->AsObject(); FString Id=E->GetStringField(TEXT("id")); auto AN=MakeShared<FJsonObject>(); AN->SetStringField(TEXT("name"),Id); AN->SetNumberField(TEXT("parent"),0); MetadataTransform(AN,Frame(E)); AuthNodes.Add(Object(AN));
        auto B=MakeShared<FJsonObject>(); B->SetStringField(TEXT("id"),Id); B->SetNumberField(TEXT("node"),AuthNodes.Num()-1);
        FString Type=E->GetStringField(TEXT("type"));
        if(Type==TEXT("box")) {FVector S=Vec(E->GetArrayField(TEXT("size")))*.01; auto V=MakeShared<FJsonObject>(); V->SetNumberField(TEXT("x"),S.Y); V->SetNumberField(TEXT("y"),S.Z); V->SetNumberField(TEXT("z"),S.X); B->SetObjectField(TEXT("size"),V);}
        if(Type==TEXT("sphere") || Type==TEXT("capsule")) B->SetNumberField(TEXT("radius"),E->GetNumberField(TEXT("radius"))*.01);
        if(Type==TEXT("capsule")) {B->SetNumberField(TEXT("height"),E->GetNumberField(TEXT("halfHeight"))*.02); B->SetNumberField(TEXT("direction"),1);}
        Bindings.Add(Object(B));
    }
    auto BoundaryBinding=MakeShared<FJsonObject>(); BoundaryBinding->SetStringField(TEXT("id"),TEXT("boundary")); BoundaryBinding->SetNumberField(TEXT("node"),ActorNodes[Boundary]); Bindings.Add(Object(BoundaryBinding));
    auto M=MakeShared<FJsonObject>(); M->SetObjectField(TEXT("boundary"),BoundaryRecord); M->SetNumberField(TEXT("schemaVersion"),5); M->SetStringField(TEXT("baseLighting"),Root->BaseLighting); if(Root->bOverrideSun){auto Sun=PortableMapLighting::WriteSun(Root->GetSun());FPortableMapSun Checked;if(!PortableMapLighting::ReadSun(Sun,Checked))return Fail(TEXT("Invalid base sun settings"));M->SetObjectField(TEXT("baseSun"),Sun);} M->SetStringField(TEXT("mapId"),Root->MapId); M->SetArrayField(TEXT("meshes"),Entries); M->SetArrayField(TEXT("colliders"),Colliders); M->SetArrayField(TEXT("markers"),Markers);M->SetArrayField(TEXT("lights"),Lights);
    auto Authoring=MakeShared<FJsonObject>(); Authoring->SetNumberField(TEXT("version"),1); Authoring->SetArrayField(TEXT("nodes"),AuthNodes); Authoring->SetArrayField(TEXT("bindings"),Bindings); M->SetObjectField(TEXT("authoring"),Authoring);
    D->SetArrayField(TEXT("meshes"),NewMeshes); D->SetArrayField(TEXT("nodes"),NewNodes); D->SetArrayField(TEXT("accessors"),Accessors); D->SetArrayField(TEXT("bufferViews"),Views);
    TArray<JValue> MaterialExtensions;MaterialExtensions.Add(MakeShared<FJsonValueString>(TEXT("KHR_texture_transform")));MaterialExtensions.Add(MakeShared<FJsonValueString>(TEXT("KHR_materials_emissive_strength")));D->SetArrayField(TEXT("extensionsUsed"),MaterialExtensions);D->SetArrayField(TEXT("materials"),ExportMaterials); D->SetArrayField(TEXT("images"),Images); D->SetArrayField(TEXT("textures"),Textures);
    auto Scene=MakeShared<FJsonObject>(); Scene->SetArrayField(TEXT("nodes"),SceneNodes); D->SetArrayField(TEXT("scenes"),{Object(Scene)}); D->SetNumberField(TEXT("scene"),0); D->RemoveField(TEXT("animations")); D->RemoveField(TEXT("skins"));
    D->GetArrayField(TEXT("buffers"))[0]->AsObject()->SetNumberField(TEXT("byteLength"),Bin.Num());
    auto BuildGlb=[&]() {FTCHARToUTF8 J(*Json(D)); TArray<uint8> Out,TextBytes; TextBytes.Append(reinterpret_cast<const uint8*>(J.Get()),J.Length()); while(TextBytes.Num()%4) TextBytes.Add(' '); while(Bin.Num()%4) Bin.Add(0); Write32(Out,0x46546c67); Write32(Out,2); Write32(Out,28+TextBytes.Num()+Bin.Num()); Write32(Out,TextBytes.Num()); Write32(Out,0x4e4f534a); Out.Append(TextBytes); Write32(Out,Bin.Num()); Write32(Out,0x004e4942); Out.Append(Bin); return Out;};
    auto SceneBytes=BuildGlb(); FSHAHash Hash; FSHA1::HashBuffer(SceneBytes.GetData(),SceneBytes.Num(),Hash.Hash); M->SetStringField(TEXT("sceneSha1"),Hash.ToString());
    FString Manifest=Json(M); if(!APortableMapScene::ValidateManifest(Manifest,Error)) return false; FTCHARToUTF8 Utf8(*Manifest);
    if(Utf8.Length()>8*1024*1024 || SceneBytes.Num()>256*1024*1024) return Fail(TEXT("Map exceeds the runtime container size limits"));
    TArray<uint8> Out; Write32(Out,0x50414d54); Write32(Out,1); Write32(Out,Utf8.Length()); Write32(Out,SceneBytes.Num()); Out.Append(reinterpret_cast<const uint8*>(Utf8.Get()),Utf8.Length()); Out.Append(SceneBytes);
    FString Temp=File+TEXT(".")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".tmp");
    bool Written=FFileHelper::SaveArrayToFile(Out,*Temp) && IFileManager::Get().Move(*File,*Temp,true,true); if(!Written) {IFileManager::Get().Delete(*Temp); return Fail(TEXT("Cannot write map file"));} Error.Reset(); return true;
}
}
