#include "PortableMapScene.h"
#include "PortableMapLighting.h"
#include "Components/LightComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerStart.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
#include "glTFRuntimeFunctionLibrary.h"
#include "PortableMapBuildTask.h"
#include "Async/Async.h"
#include "PhysicsEngine/BodySetup.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "HAL/IConsoleManager.h"

namespace PortableMap
{
TFunction<bool(const UObject*)> AccessPolicy;
TAutoConsoleVariable<float> AssemblyBudgetMs(TEXT("PortableMaps.AssemblyBudgetMs"),2.f,TEXT("Per-frame portable-map component assembly budget in milliseconds (clamped to 0.1-4)."));
constexpr int32 MaxItems = 10000;
constexpr int64 MaxManifestBytes = 8 * 1024 * 1024;
constexpr int64 MaxSceneBytes = 256 * 1024 * 1024;
struct FCollider { FString Id, Type; FTransform Frame; FVector Size; float Radius=0, HalfHeight=0; };
struct FMesh { FString Id; int32 Index=0; bool bVisible=true, bCollision=false; };
struct FManifest { FString Id, Hash; FString BaseLighting=TEXT("cartoon-street-day"); FPortableMapSun BaseSun; TArray<FMesh> Meshes; TArray<FCollider> Colliders; TArray<FPortableMapMarker> Markers; FPortableMapBoundary Boundary; TArray<FPortableMapLight> Lights; };

bool Number(const TSharedPtr<FJsonObject>& J, const TCHAR* Key, double& Out)
{
    return J->TryGetNumberField(Key, Out) && FMath::IsFinite(Out) && FMath::Abs(Out) <= 10000000;
}
bool Vector(const TSharedPtr<FJsonObject>& J, const TCHAR* Key, FVector& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
    if (!J->TryGetArrayField(Key, Values) || Values->Num()!=3) return false;
    for (int32 I=0; I<3; ++I) { double V; if (!(*Values)[I]->TryGetNumber(V) || !FMath::IsFinite(V) || FMath::Abs(V)>10000000) return false; Out[I]=V; }
    return true;
}
bool Frame(const TSharedPtr<FJsonObject>& J, FTransform& Out)
{
    FVector Position; const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
    if (!Vector(J,TEXT("position"),Position) || !J->TryGetArrayField(TEXT("rotation"),Values) || Values->Num()!=4) return false;
    double Q[4]; for (int32 I=0; I<4; ++I) if (!(*Values)[I]->TryGetNumber(Q[I]) || !FMath::IsFinite(Q[I])) return false;
    FQuat Rotation(Q[0],Q[1],Q[2],Q[3]);
    if (!FMath::IsNearlyEqual(Rotation.SizeSquared(),1.,.001)) return false;
    Out=FTransform(Rotation,Position); return true;
}
bool Parse(const FString& Json, FManifest& Out, FString& Error)
{
    const auto Fail=[&](const TCHAR* Why) { Error=Why; return false; };
    TSharedPtr<FJsonObject> J;
    if (Json.Len()>MaxManifestBytes || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),J) || !J) return Fail(TEXT("Invalid map JSON"));
    double Version=0;
    if (!Number(J,TEXT("schemaVersion"),Version) || (Version!=1 && Version!=2 && Version!=3 && Version!=4 && Version!=5) || !J->TryGetStringField(TEXT("mapId"),Out.Id) || Out.Id.IsEmpty() || Out.Id.Len()>128) return Fail(TEXT("Unsupported schema version or invalid map ID"));
    if (!J->TryGetStringField(TEXT("sceneSha1"),Out.Hash) || Out.Hash.Len()!=40) return Fail(TEXT("Missing scene hash"));
    for (const TCHAR C : Out.Hash) if (!FChar::IsHexDigit(C)) return Fail(TEXT("Invalid scene hash"));
    TSet<FString> Ids;
    if(Version>=4 && (!J->TryGetStringField(TEXT("baseLighting"),Out.BaseLighting)
        || (Out.BaseLighting!=TEXT("cartoon-street-day") && Out.BaseLighting!=TEXT("cartoon-street-night"))))return Fail(TEXT("Unsupported base lighting profile"));
    if(J->HasField(TEXT("baseSun")))
    {const TSharedPtr<FJsonObject>* Sun=nullptr;if(Version<5||!J->TryGetObjectField(TEXT("baseSun"),Sun)||!PortableMapLighting::ReadSun(*Sun,Out.BaseSun))return Fail(TEXT("Invalid base sun override"));}
    const auto Items=[&](const TCHAR* Key, const TFunction<bool(const TSharedPtr<FJsonObject>&)>& Visit)
    {
        const TArray<TSharedPtr<FJsonValue>>* Array=nullptr;
        if (!J->TryGetArrayField(Key,Array) || Array->Num()>MaxItems) return false;
        for (const auto& Value : *Array)
        {
            const TSharedPtr<FJsonObject>* Object=nullptr; FString Id;
            if (!Value->TryGetObject(Object) || !Object || !(*Object)->TryGetStringField(TEXT("id"),Id) || Id.IsEmpty() || Id.Len()>128 || Ids.Contains(Id)) return false;
            Ids.Add(Id); if (!Visit(*Object)) return false;
        }
        return true;
    };
    if (!Items(TEXT("meshes"),[&](const TSharedPtr<FJsonObject>& M)
    {
        FMesh V; double Index; M->TryGetStringField(TEXT("id"),V.Id);
        if (!Number(M,TEXT("mesh"),Index) || Index<0 || Index>MaxItems || FMath::FloorToDouble(Index)!=Index || !M->TryGetBoolField(TEXT("visible"),V.bVisible) || !M->TryGetBoolField(TEXT("collision"),V.bCollision)) return false;
        V.Index=static_cast<int32>(Index); Out.Meshes.Add(V); return true;
    })) return Fail(TEXT("Invalid meshes or duplicate IDs"));
    if (!Items(TEXT("colliders"),[&](const TSharedPtr<FJsonObject>& C)
    {
        FCollider V; C->TryGetStringField(TEXT("id"),V.Id);
        if (!C->TryGetStringField(TEXT("type"),V.Type) || !Frame(C,V.Frame)) return false;
        if (V.Type==TEXT("box")) { if (!Vector(C,TEXT("size"),V.Size) || V.Size.GetMin()<=0) return false; }
        else if (V.Type==TEXT("sphere") || V.Type==TEXT("capsule"))
        {
            double R=0,H=0; if (!Number(C,TEXT("radius"),R) || R<=0) return false; V.Radius=R;
            if (V.Type==TEXT("capsule")) { if (!Number(C,TEXT("halfHeight"),H) || H<R) return false; V.HalfHeight=H; }
        }
        else return false;
        Out.Colliders.Add(V); return true;
    })) return Fail(TEXT("Invalid collider or duplicate IDs"));
    if (!Items(TEXT("markers"),[&](const TSharedPtr<FJsonObject>& M)
    {
        FPortableMapMarker V; FString Type,Tag; M->TryGetStringField(TEXT("id"),V.Id);
        if (!M->TryGetStringField(TEXT("type"),Type) || Type!=TEXT("playerStart") || !Frame(M,V.Transform) || !M->TryGetStringField(TEXT("spawnTag"),Tag) || Tag.Len()>128) return false;
        V.Type=FName(Type); V.SpawnTag=FName(Tag); Out.Markers.Add(V); return true;
    })) return Fail(TEXT("Unsupported marker, invalid transform, or duplicate IDs"));
    if (Out.Meshes.IsEmpty() || Out.Markers.IsEmpty()) return Fail(TEXT("A map needs geometry and at least one player start"));
    if(J->HasField(TEXT("boundary")) && !J->HasTypedField<EJson::Null>(TEXT("boundary")))
    {
        const TSharedPtr<FJsonObject>* B=nullptr; FString Id;
        auto& Boundary=Out.Boundary;
        if(!J->TryGetObjectField(TEXT("boundary"),B) || !B || !(*B)->TryGetStringField(TEXT("id"),Id) || Id!=TEXT("boundary") || Ids.Contains(Id) ||
            !(*B)->TryGetStringField(TEXT("shape"),Boundary.Shape) || (Boundary.Shape!=TEXT("box") && Boundary.Shape!=TEXT("cylinder")) ||
            !Frame(*B,Boundary.Frame) || !Vector(*B,TEXT("size"),Boundary.Size) || Boundary.Size.GetMin()<1 ||
            (Boundary.Shape==TEXT("cylinder") && !FMath::IsNearlyEqual(Boundary.Size.X,Boundary.Size.Y,.1)) || Out.Markers.Num()<4)
            return Fail(TEXT("Invalid playable boundary or fewer than four player starts"));
        Boundary.bPresent=true;
    }
    if(Version>=2 && !Out.Boundary.bPresent) return Fail(TEXT("Playable map schema requires a boundary"));
    if(J->HasField(TEXT("lights")))
    {
        if(!Items(TEXT("lights"),[&](const TSharedPtr<FJsonObject>& L) {FPortableMapLight V;if(!PortableMapLighting::Read(L,V)) return false;Out.Lights.Add(V);return true;}) || Out.Lights.Num()>256 || (Version<3 && !Out.Lights.IsEmpty()))
            return Fail(TEXT("Invalid lights, duplicate IDs, unsupported light type or light count exceeds 256"));
    }
    else if(Version>=3) return Fail(TEXT("Light schema requires a lights array"));
    Error.Reset(); return true;
}
}

APortableMapScene::APortableMapScene()
{
    PrimaryActorTick.bCanEverTick=false;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}
bool APortableMapScene::ValidateManifest(const FString& Json, FString& OutError)
{
    PortableMap::FManifest Parsed; return PortableMap::Parse(Json,Parsed,OutError);
}
void APortableMapScene::SetAccessPolicy(TFunction<bool(const UObject*)> Policy) {PortableMap::AccessPolicy=MoveTemp(Policy);}
bool APortableMapScene::IsAccessAllowed(const UObject* Context) {return !PortableMap::AccessPolicy || PortableMap::AccessPolicy(Context);}
bool APortableMapScene::LoadDirectory(const FString& Directory, FString& OutError)
{
    if(!IsAccessAllowed(this)) {OutError=TEXT("Portable map feature access is disabled"); return false;}
    using namespace PortableMap;
    // Fixed filenames and embedded GLB resources: imported JSON cannot request arbitrary filesystem or network reads.
    const FString Base=FPaths::ConvertRelativePathToFull(Directory);
    const FString ManifestPath=Base/TEXT("map.json"), ScenePath=Base/TEXT("scene.glb");
    const int64 ManifestSize=IFileManager::Get().FileSize(*ManifestPath), SceneSize=IFileManager::Get().FileSize(*ScenePath);
    if (ManifestSize<=0 || ManifestSize>MaxManifestBytes || SceneSize<=0 || SceneSize>MaxSceneBytes) { OutError=TEXT("Missing or oversized map.json/scene.glb"); return false; }
    FString Json; TArray<uint8> Data;
    if (!FFileHelper::LoadFileToString(Json,*ManifestPath)) { OutError=TEXT("Cannot read map.json"); return false; }
    if (!FFileHelper::LoadFileToArray(Data,*ScenePath)) { OutError=TEXT("Cannot read scene.glb"); return false; }
    return LoadBytes(Json,Data,OutError);
}
bool APortableMapScene::LoadFile(const FString& Filename,FString& OutError)
{
    if(!IsAccessAllowed(this)) {OutError=TEXT("Portable map feature access is disabled"); return false;}
    FPortableMapFileInfo Info;
    if (!InspectFile(Filename, Info, OutError)) return false;
    const int64 Size=IFileManager::Get().FileSize(*Filename);
    if (Size<16 || Size>PortableMap::MaxManifestBytes+PortableMap::MaxSceneBytes+16) { OutError=TEXT("Missing or oversized portable map file"); return false; }
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes,*Filename) || Bytes.Num()<16) { OutError=TEXT("Cannot read portable map file"); return false; }
    const auto U32=[&](int32 Offset) { return uint32(Bytes[Offset]) | uint32(Bytes[Offset+1])<<8 | uint32(Bytes[Offset+2])<<16 | uint32(Bytes[Offset+3])<<24; };
    const uint32 JsonLength=U32(8), SceneLength=U32(12);
    if (U32(0)!=0x50414d54 || U32(4)!=1 || JsonLength==0 || JsonLength>PortableMap::MaxManifestBytes || SceneLength==0 || SceneLength>PortableMap::MaxSceneBytes || int64(JsonLength)+SceneLength+16!=Bytes.Num())
    { OutError=TEXT("Invalid portable map header, version or payload lengths"); return false; }
    const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()+16),JsonLength);
    FString Json(Text.Length(),Text.Get());
    TArray<uint8> Data; Data.Append(Bytes.GetData()+16+JsonLength,SceneLength);
    return LoadBytes(Json,Data,OutError);
}
bool APortableMapScene::InspectFile(const FString& Filename, FPortableMapFileInfo& OutInfo, FString& OutError)
{
    using namespace PortableMap;
    OutInfo = {};
    const int64 Size = IFileManager::Get().FileSize(*Filename);
    if (Size < 16 || Size > MaxManifestBytes + MaxSceneBytes + 16) { OutError = TEXT("Missing or oversized portable map file"); return false; }
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *Filename)) { OutError = TEXT("Cannot read portable map"); return false; }
    const auto U32 = [&](int32 Offset) { return uint32(Bytes[Offset]) | uint32(Bytes[Offset+1])<<8 | uint32(Bytes[Offset+2])<<16 | uint32(Bytes[Offset+3])<<24; };
    if (Bytes.Num() < 16) { OutError = TEXT("Truncated container"); return false; }
    const uint32 JsonLength = U32(8), SceneLength = U32(12);
    if (U32(0) != 0x50414d54 || U32(4) != 1 || !JsonLength || JsonLength > MaxManifestBytes || !SceneLength || SceneLength > MaxSceneBytes || int64(JsonLength) + SceneLength + 16 != Bytes.Num())
    { OutError = TEXT("Invalid portable map header, version or payload lengths"); return false; }
    const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()+16), JsonLength);
    FManifest Parsed;
    if (!Parse(FString(Text.Length(), Text.Get()), Parsed, OutError)) return false;
    FSHAHash Hash;
    FSHA1::HashBuffer(Bytes.GetData()+16+JsonLength, SceneLength, Hash.Hash);
    if (!Hash.ToString().Equals(Parsed.Hash, ESearchCase::IgnoreCase)) { OutError = TEXT("Embedded scene digest mismatch"); return false; }
    FSHA1::HashBuffer(Bytes.GetData()+16, JsonLength, Hash.Hash);
    OutInfo.ContentId = Parsed.Id + TEXT(":") + Hash.ToString();
    FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num(), Hash.Hash);
    OutInfo.FileHash = Hash.ToString();
    OutInfo.PlayerStarts = Parsed.Markers.Num();
    OutInfo.BaseLighting=Parsed.BaseLighting;OutInfo.BaseSun=Parsed.BaseSun;
    OutError.Reset();
    return true;
}
bool APortableMapScene::LoadBytes(const FString& Json,const TArray<uint8>& Data,FString& OutError)
{
    using namespace PortableMap;
    FManifest Parsed;
    if (!Parse(Json,Parsed,OutError)) return false;
    FSHAHash Hash; FSHA1::HashBuffer(Data.GetData(),Data.Num(),Hash.Hash);
    if (!Hash.ToString().Equals(Parsed.Hash,ESearchCase::IgnoreCase)) { OutError=TEXT("Scene hash mismatch; export is incomplete or modified"); return false; }
    FglTFRuntimeConfig Config; Config.bAllowExternalFiles=false; Config.bNoArchive=true;
    // glTF (-UnityX, UnityY, UnityZ) metres -> Unreal (UnityZ, UnityX, UnityY) centimetres.
    Config.TransformBaseType=EglTFRuntimeTransformBaseType::Matrix;
    Config.BasisMatrix=FBasisVectorMatrix(FVector(0,0,1),FVector(-1,0,0),FVector(0,1,0),FVector::ZeroVector);
    UglTFRuntimeAsset* Loaded=UglTFRuntimeFunctionLibrary::glTFLoadAssetFromData(Data,Config);
    if (!Loaded) { OutError=TEXT("GLB parser rejected scene"); return false; }
    TArray<TObjectPtr<UPrimitiveComponent>> Staged;
    const auto Abort=[&](const TCHAR* Why) { for (UPrimitiveComponent* C : Staged) C->DestroyComponent(); OutError=Why; return false; };
    for (const FMesh& M : Parsed.Meshes)
    {
        FglTFRuntimeStaticMeshConfig MeshConfig; MeshConfig.Outer=this;
        MeshConfig.bBuildComplexCollision=M.bCollision;
        MeshConfig.bAllowCPUAccess=M.bCollision;
        MeshConfig.CollisionComplexity=M.bCollision ? CTF_UseComplexAsSimple : CTF_UseDefault;
        UStaticMesh* Mesh=Loaded->LoadStaticMesh(M.Index,MeshConfig);
        if (!Mesh) return Abort(TEXT("Failed to build imported static mesh"));
        UStaticMeshComponent* C=NewObject<UStaticMeshComponent>(this);
        C->SetupAttachment(RootComponent); C->SetStaticMesh(Mesh);
        C->SetCollisionProfileName(M.bCollision ? TEXT("BlockAll") : TEXT("NoCollision"));
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision); C->SetVisibility(false);
        C->ComponentTags.Add(M.bVisible ? TEXT("PortableVisible") : TEXT("PortableHidden"));
        C->ComponentTags.Add(FName(*M.Id));
        if (M.bCollision) C->ComponentTags.Add(TEXT("PortableCollision"));
        Staged.Add(C);
    }
    for (const FCollider& V : Parsed.Colliders)
    {
        UShapeComponent* C=nullptr;
        if (V.Type==TEXT("box")) { auto* B=NewObject<UBoxComponent>(this); B->SetBoxExtent(V.Size*.5); C=B; }
        else if (V.Type==TEXT("sphere")) { auto* S=NewObject<USphereComponent>(this); S->SetSphereRadius(V.Radius); C=S; }
        else { auto* P=NewObject<UCapsuleComponent>(this); P->SetCapsuleSize(V.Radius,V.HalfHeight); C=P; }
        C->SetupAttachment(RootComponent); C->SetRelativeTransform(V.Frame);
        C->SetCollisionProfileName(TEXT("BlockAll")); C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->SetHiddenInGame(true); C->ComponentTags.Add(TEXT("PortableCollision")); Staged.Add(C);
    }
    Clear(); Asset=Loaded; Realized=MoveTemp(Staged); Markers=MoveTemp(Parsed.Markers); Boundary=MoveTemp(Parsed.Boundary); BaseLighting=Parsed.BaseLighting; BaseSun=Parsed.BaseSun;
    for(const auto& V:Parsed.Lights) {auto* C=PortableMapLighting::Create(this,V);C->SetupAttachment(RootComponent);AddInstanceComponent(C);C->RegisterComponent();Lights.Add(C);}
    FTCHARToUTF8 ManifestUtf8(*Json);
    FSHAHash ManifestHash; FSHA1::HashBuffer(ManifestUtf8.Get(),ManifestUtf8.Length(),ManifestHash.Hash);
    ContentId=Parsed.Id+TEXT(":")+ManifestHash.ToString();
    for (UPrimitiveComponent* C : Realized) { AddInstanceComponent(C); C->RegisterComponent(); }
    SetContentActive(bContentActive); OutError.Reset();
    UE_LOG(LogTemp,Display,TEXT("PORTABLE_MAP_LOADED id=%s components=%d starts=%d"),*ContentId,Realized.Num(),Markers.Num());
    return true;
}
void APortableMapScene::SetContentActive(bool bActive)
{
    bContentActive=bActive;
    for(ULightComponent* C:Lights) C->SetVisibility(bActive);
    for (UPrimitiveComponent* C : Realized)
    {
        C->SetVisibility(bActive && C->ComponentHasTag(TEXT("PortableVisible")));
        C->SetCollisionEnabled(bActive && C->ComponentHasTag(TEXT("PortableCollision")) ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
    }
}
void APortableMapScene::Clear()
{
    CancelPreparation(); PreparedSource=nullptr;
    for (UPrimitiveComponent* C : Realized) if (C) C->DestroyComponent();
    for(ULightComponent* C:Lights) if(C) C->DestroyComponent();
    Lights.Reset();
    Realized.Reset(); Markers.Reset(); Boundary=FPortableMapBoundary(); BaseLighting=TEXT("cartoon-street-day"); BaseSun=FPortableMapSun(); Asset=nullptr; ContentId.Reset();
}
void APortableMapScene::EndPlay(const EEndPlayReason::Type Reason) { Clear(); Super::EndPlay(Reason); }

struct FPortableMapBuildState
{
    TAtomic<bool> Cancelled{false};
    uint64 BudgetFrame=MAX_uint64;
    double Deadline=0;
    PortableMap::FManifest Manifest;
    FPortableMapFileInfo Info;
    TSharedPtr<FglTFRuntimeParser> Parser;
    FString Error;
};
void APortableMapScene::CancelPreparation()
{if(Preparation) Preparation->Cancel(); Preparation=nullptr;}
void APortableMapScene::LoadFileAsync(const FString& File,const FString& Expected,TFunction<void(bool,const FString&)> Done,TFunction<bool()> Current)
{
    if(!IsAccessAllowed(this)) {if(Done) Done(false,TEXT("Portable map feature access is disabled")); return;}
    CancelPreparation(); Preparation=NewObject<UPortableMapBuildTask>(this);
    Preparation->Start(this,File,Expected,MoveTemp(Done),MoveTemp(Current));
}
void APortableMapScene::CopyPreparedAsync(APortableMapScene* Source,TFunction<void(bool,const FString&)> Done,TFunction<bool()> Current)
{
    if(!IsAccessAllowed(this)) {if(Done) Done(false,TEXT("Portable map feature access is disabled")); return;}
    CancelPreparation(); Preparation=NewObject<UPortableMapBuildTask>(this);
    Preparation->Copy(this,Source,MoveTemp(Done),MoveTemp(Current));
}
bool UPortableMapBuildTask::Current() const
{return !bCancelled && IsValid(Target) && APortableMapScene::IsAccessAllowed(Target) && Target->Preparation==this && (!Source || (IsValid(Source) && Source->IsLoaded())) && (!IsCurrent || IsCurrent());}
void UPortableMapBuildTask::Cancel()
{
    if(Work) Work->Cancelled.Store(true);
    else if(MaterialHandle) MaterialHandle->CancelHandle();
    MaterialHandle.Reset();
    bCancelled=true; Completion={};
    if(TickHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
    TickHandle.Reset();
    for(UPrimitiveComponent* Component:Components) if(IsValid(Component)) Component->DestroyComponent();
    for(ULightComponent* Component:Lights) if(IsValid(Component)) Component->DestroyComponent();
    Lights.Reset();
    Components.Reset(); Asset=nullptr; Source=nullptr;
}
void UPortableMapBuildTask::Start(APortableMapScene* Scene,const FString& File,const FString& Expected,TFunction<void(bool,const FString&)> Done,TFunction<bool()> CurrentContext)
{
    Target=Scene; Completion=MoveTemp(Done); IsCurrent=MoveTemp(CurrentContext); Started=FPlatformTime::Seconds();
    TWeakObjectPtr<UPortableMapBuildTask> Weak=this;
    // Parser construction otherwise performs 22 synchronous LoadObject calls even on its async path.
    MaterialHandle=UAssetManager::GetStreamableManager().RequestAsyncLoad(FglTFRuntimeParser::GetBaseMaterialPaths(),FStreamableDelegate::CreateLambda([Weak,File,Expected]
    {if(auto* Self=Weak.Get(); Self && Self->Current()) Self->ReadFile(File,Expected);}),FStreamableManager::DefaultAsyncLoadPriority,true);
    if(!MaterialHandle) Finish(false,TEXT("Cannot request portable map material dependencies"));
}
void UPortableMapBuildTask::ReadFile(const FString& File,const FString& Expected)
{
    UE_LOG(LogTemp,Display,TEXT("PORTABLE_MAP_PREPARING file=%s"),*File);
    Work=MakeShared<FPortableMapBuildState,ESPMode::ThreadSafe>(); auto State=Work;
    TWeakObjectPtr<UPortableMapBuildTask> Weak=this;
    auto Dependencies=MaterialHandle;
    Async(EAsyncExecution::ThreadPool,[Weak,State,File,Expected,Dependencies]() mutable
    {
        if(!State->Cancelled.Load() && APortableMapScene::InspectFile(File,State->Info,State->Error) && (Expected.IsEmpty() || State->Info.FileHash.Equals(Expected,ESearchCase::IgnoreCase)))
        {
            TArray<uint8> Bytes;
            if(FFileHelper::LoadFileToArray(Bytes,*File) && Bytes.Num()>=16)
            {
                const auto U32=[&](int32 O){return uint32(Bytes[O])|uint32(Bytes[O+1])<<8|uint32(Bytes[O+2])<<16|uint32(Bytes[O+3])<<24;};
                const uint32 JsonLength=U32(8),DataLength=U32(12);
                FSHAHash Hash; FSHA1::HashBuffer(Bytes.GetData(),Bytes.Num(),Hash.Hash);
                if(int64(JsonLength)+DataLength+16==Bytes.Num() && Hash.ToString()==State->Info.FileHash)
                {
                    FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()+16),JsonLength);
                    if(!State->Cancelled.Load() && PortableMap::Parse(FString(Text.Length(),Text.Get()),State->Manifest,State->Error))
                    {
                        FglTFRuntimeConfig Config; Config.bAllowExternalFiles=false; Config.bNoArchive=true;
                        Config.TransformBaseType=EglTFRuntimeTransformBaseType::Matrix;
                        Config.BasisMatrix=FBasisVectorMatrix(FVector(0,0,1),FVector(-1,0,0),FVector(0,1,0),FVector::ZeroVector);
                        State->Parser=FglTFRuntimeParser::FromData(Bytes.GetData()+16+JsonLength,DataLength,Config);
                    }
                }
            }
            if(!State->Parser && State->Error.IsEmpty()) State->Error=TEXT("Map changed while preparing or GLB parsing failed");
        }
        else if(State->Error.IsEmpty()) State->Error=TEXT("Prepared map differs from selected revision");
        AsyncTask(ENamedThreads::GameThread,[Weak,State=MoveTemp(State),Dependencies=MoveTemp(Dependencies)]
        {
            auto* Self=Weak.Get(); if(!Self || !Self->Current()) {if(Dependencies) Dependencies->ReleaseHandle(); return;}
            if(!State->Parser) {Self->Finish(false,State->Error); return;}
            Self->Asset=NewObject<UglTFRuntimeAsset>(Self);
            if(!Self->Asset->SetParser(State->Parser.ToSharedRef())) {Self->Finish(false,TEXT("GLB parser rejected prepared scene")); return;}
            if(Dependencies) Dependencies->ReleaseHandle();
            Self->TickHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(Self,&UPortableMapBuildTask::Tick));
        });
    });
}
void UPortableMapBuildTask::Copy(APortableMapScene* Scene,APortableMapScene* InSource,TFunction<void(bool,const FString&)> Done,TFunction<bool()> CurrentContext)
{
    Target=Scene; Source=InSource; Completion=MoveTemp(Done); IsCurrent=MoveTemp(CurrentContext); Started=FPlatformTime::Seconds();
    Work=MakeShared<FPortableMapBuildState,ESPMode::ThreadSafe>();
    if(!IsValid(Source) || !Source->IsLoaded()) {Finish(false,TEXT("Prepared map resources are unavailable")); return;}
    Asset=Source->Asset;
    TickHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&ThisClass::Tick));
}
void UPortableMapBuildTask::Register(UPrimitiveComponent* C)
{
    C->SetupAttachment(Target->GetRootComponent()); C->SetCollisionEnabled(ECollisionEnabled::NoCollision); C->SetVisibility(false);
    Target->AddInstanceComponent(C); C->RegisterComponent(); Components.Add(C);
}
void UPortableMapBuildTask::MeshReady(UStaticMesh* Mesh)
{
    UE_LOG(LogTemp,Display,TEXT("PORTABLE_MAP_MESH_READY index=%d mesh=%s current=%d"),MeshIndex,*GetNameSafe(Mesh),Current());
    if(!Current()) return;
    if(!Mesh) {Finish(false,TEXT("Failed to prepare imported static mesh")); return;}
    if(Work->Manifest.Meshes[MeshIndex].bCollision)
    {
        PendingMesh=Mesh; auto* Body=Mesh->GetBodySetup();
        Body->bNeverNeedsCookedCollisionData=false; Body->CollisionTraceFlag=CTF_UseComplexAsSimple;
        Body->CreatePhysicsMeshesAsync(FOnAsyncPhysicsCookFinished::CreateWeakLambda(this,[this](bool Success)
        {if(!Current()) return; if(!Success) {Finish(false,TEXT("Asynchronous collision cooking failed")); return;} CompleteMesh(PendingMesh); PendingMesh=nullptr;}));
        return;
    }
    CompleteMesh(Mesh);
}
void UPortableMapBuildTask::CompleteMesh(UStaticMesh* Mesh)
{
    bMeshPending=false;
    const double Begin=FPlatformTime::Seconds(); const auto& Entry=Work->Manifest.Meshes[MeshIndex++];
    auto* C=NewObject<UStaticMeshComponent>(Target); C->SetStaticMesh(Mesh);
    C->SetCollisionProfileName(Entry.bCollision?TEXT("BlockAll"):TEXT("NoCollision"));
    C->ComponentTags.Add(Entry.bVisible?TEXT("PortableVisible"):TEXT("PortableHidden")); C->ComponentTags.Add(FName(*Entry.Id));
    if(Entry.bCollision) C->ComponentTags.Add(TEXT("PortableCollision")); Register(C);
    MaxSlice=FMath::Max(MaxSlice,FPlatformTime::Seconds()-Begin);
    // Continue the serial parser pipeline within this frame's budget instead of adding one frame per mesh.
    if(Current()) Tick(0);
}
bool UPortableMapBuildTask::Tick(float)
{
    if(!Current()) {Finish(false,TEXT("Map preparation cancelled by owner or revision change")); return false;}
    const double Begin=FPlatformTime::Seconds();
    if(Work->BudgetFrame!=GFrameCounter)
    {Work->BudgetFrame=GFrameCounter; Work->Deadline=Begin+FMath::Clamp(PortableMap::AssemblyBudgetMs.GetValueOnGameThread(),.1f,4.f)*.001;}
    if(Begin>=Work->Deadline) return true;
    if(Source)
    {
        while(MeshIndex<Source->Realized.Num() && FPlatformTime::Seconds()<Work->Deadline)
        {
            auto* C=DuplicateObject<UPrimitiveComponent>(Source->Realized[MeshIndex++],Target);
            Register(C);
        }
        MaxSlice=FMath::Max(MaxSlice,FPlatformTime::Seconds()-Begin);
        while(LightIndex<Source->Lights.Num() && FPlatformTime::Seconds()<Work->Deadline)
        {
            auto* C=DuplicateObject<ULightComponent>(Source->Lights[LightIndex++],Target);
            C->SetupAttachment(Target->GetRootComponent());C->SetVisibility(false);Target->AddInstanceComponent(C);C->RegisterComponent();Lights.Add(C);
        }
        if(MeshIndex==Source->Realized.Num() && LightIndex==Source->Lights.Num()) {Finish(true,TEXT("")); return false;}
        return true;
    }
    if(bMeshPending) return true;
    if(MeshIndex<Work->Manifest.Meshes.Num())
    {
        const auto& Entry=Work->Manifest.Meshes[MeshIndex]; FglTFRuntimeStaticMeshConfig Config;
        Config.Outer=Target; Config.bBuildComplexCollision=false; Config.bAllowCPUAccess=Entry.bCollision;
        Config.CacheMode=EglTFRuntimeCacheMode::None; Config.CollisionComplexity=CTF_UseDefault;
        FglTFRuntimeStaticMeshAsync Callback; Callback.BindDynamic(this,&ThisClass::MeshReady); bMeshPending=true;
        Asset->LoadStaticMeshAsync(Entry.Index,Callback,Config); return true;
    }
    while(ColliderIndex<Work->Manifest.Colliders.Num() && FPlatformTime::Seconds()<Work->Deadline)
    {
        const auto& V=Work->Manifest.Colliders[ColliderIndex++]; UShapeComponent* C=nullptr;
        if(V.Type==TEXT("box")) {auto* B=NewObject<UBoxComponent>(Target); B->SetBoxExtent(V.Size*.5); C=B;}
        else if(V.Type==TEXT("sphere")) {auto* S=NewObject<USphereComponent>(Target); S->SetSphereRadius(V.Radius); C=S;}
        else {auto* P=NewObject<UCapsuleComponent>(Target); P->SetCapsuleSize(V.Radius,V.HalfHeight); C=P;}
        C->SetRelativeTransform(V.Frame); C->SetCollisionProfileName(TEXT("BlockAll")); C->SetHiddenInGame(true); C->ComponentTags.Add(TEXT("PortableCollision")); Register(C);
    }
    MaxSlice=FMath::Max(MaxSlice,FPlatformTime::Seconds()-Begin);
    while(LightIndex<Work->Manifest.Lights.Num() && FPlatformTime::Seconds()<Work->Deadline)
    {
        auto* C=PortableMapLighting::Create(Target,Work->Manifest.Lights[LightIndex++]);C->SetupAttachment(Target->GetRootComponent());Target->AddInstanceComponent(C);C->RegisterComponent();Lights.Add(C);
    }
    if(ColliderIndex==Work->Manifest.Colliders.Num() && LightIndex==Work->Manifest.Lights.Num()) {Finish(true,TEXT("")); return false;}
    return true;
}
void UPortableMapBuildTask::Finish(bool Success,const FString& Error)
{
    if(bCancelled) return;
    if(MaterialHandle) MaterialHandle->ReleaseHandle(); MaterialHandle.Reset();
    if(TickHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(TickHandle); TickHandle.Reset();
    if(Success && Current())
    {
        // Keep the old scene until the complete replacement has been realized.
        for(UPrimitiveComponent* C:Target->Realized) if(IsValid(C)) C->DestroyComponent();
        for(ULightComponent* C:Target->Lights) if(IsValid(C)) C->DestroyComponent();
        Target->Lights=MoveTemp(Lights);
        Target->Realized=MoveTemp(Components); Target->Asset=Asset; Target->PreparedSource=Source;
        Target->Markers=Source?Source->Markers:Work->Manifest.Markers;
        Target->Boundary=Source?Source->Boundary:Work->Manifest.Boundary;
        Target->BaseLighting=Source?Source->BaseLighting:Work->Manifest.BaseLighting;
        Target->BaseSun=Source?Source->BaseSun:Work->Manifest.BaseSun;
        Target->ContentId=Source?Source->ContentId:Work->Info.ContentId;
        Target->SetContentActive(Target->bContentActive);
    }
    else {Success=false; for(UPrimitiveComponent* C:Components) if(IsValid(C)) C->DestroyComponent(); Components.Reset();for(ULightComponent* C:Lights) if(IsValid(C)) C->DestroyComponent();Lights.Reset();}
    UE_LOG(LogTemp,Display,TEXT("PORTABLE_MAP_ASYNC_COMPLETE success=%d reused=%d seconds=%.3f maxAssemblyMs=%.3f components=%d reason=%s"),Success,Source!=nullptr,FPlatformTime::Seconds()-Started,MaxSlice*1000,Success?Target->Realized.Num():0,*Error);
    auto Callback=MoveTemp(Completion); bCancelled=true;
    if(IsValid(Target) && Target->Preparation==this) Target->Preparation=nullptr;
    if(Callback) Callback(Success,Error);
}
