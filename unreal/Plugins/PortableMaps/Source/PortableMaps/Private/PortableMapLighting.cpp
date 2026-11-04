#include "PortableMapLighting.h"
#include "Dom/JsonObject.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/DirectionalLightComponent.h"

namespace PortableMapLighting
{
bool ReadSun(const TSharedPtr<FJsonObject>& Json,FPortableMapSun& Out)
{
    if(!Json)return false;
    FPortableMapLight Defaults;Defaults.Id=TEXT("base-sun");Defaults.Type=TEXT("directional");
    auto Full=Write(Defaults);
    for(const auto* Key:{TEXT("rotation"),TEXT("color"),TEXT("intensity"),TEXT("shadows")})
    {const auto* Value=Json->Values.Find(Key);if(!Value)return false;Full->SetField(Key,*Value);}
    FPortableMapLight Light;if(!Read(Full,Light))return false;
    Out.bOverride=true;Out.Rotation=Light.Frame.GetRotation();Out.Color=Light.Color;Out.Intensity=Light.Intensity;Out.bShadows=Light.bShadows;return true;
}
TSharedPtr<FJsonObject> WriteSun(const FPortableMapSun& Sun)
{
    FPortableMapLight Light;Light.Id=TEXT("base-sun");Light.Type=TEXT("directional");Light.Frame.SetRotation(Sun.Rotation);Light.Color=Sun.Color;Light.Intensity=Sun.Intensity;Light.bShadows=Sun.bShadows;
    auto Full=Write(Light);auto Json=MakeShared<FJsonObject>();
    for(const auto* Key:{TEXT("rotation"),TEXT("color"),TEXT("intensity"),TEXT("shadows")})Json->SetField(Key,Full->Values[Key]);
    return Json;
}
bool Read(const TSharedPtr<FJsonObject>& J,FPortableMapLight& V)
{
    if(!J || !J->TryGetStringField(TEXT("id"),V.Id) || V.Id.IsEmpty() || V.Id.Len()>128 || !J->TryGetStringField(TEXT("type"),V.Type) ||
        (V.Type!=TEXT("point") && V.Type!=TEXT("spot") && V.Type!=TEXT("directional"))) return false;
    auto Array=[&](const TCHAR* Key,int32 Count,TArray<double>& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
        if(!J->TryGetArrayField(Key,Values) || Values->Num()!=Count) return false;
        for(auto Value:*Values) {double N; if(!Value->TryGetNumber(N) || !FMath::IsFinite(N) || FMath::Abs(N)>10000000) return false; Out.Add(N);} return true;
    };
    TArray<double> P,Q,C;
    if(!Array(TEXT("position"),3,P) || !Array(TEXT("rotation"),4,Q) || !Array(TEXT("color"),3,C)) return false;
    FQuat Rotation(Q[0],Q[1],Q[2],Q[3]);
    if(!FMath::IsNearlyEqual(Rotation.SizeSquared(),1.,.001)) return false;
    for(double N:C) if(N<0 || N>1) return false;
    V.Frame=FTransform(Rotation,FVector(P[0],P[1],P[2])); V.Color=FLinearColor(C[0],C[1],C[2]);
    auto Scalar=[&](const TCHAR* Key,float& Out) {double N; if(!J->TryGetNumberField(Key,N) || !FMath::IsFinite(N) || N<0 || N>10000000) return false; Out=N; return true;};
    return Scalar(TEXT("intensity"),V.Intensity) && Scalar(TEXT("range"),V.Range) && V.Range>0 &&
        Scalar(TEXT("innerCone"),V.InnerCone) && Scalar(TEXT("outerCone"),V.OuterCone) &&
        V.InnerCone<=V.OuterCone && V.OuterCone>0 && V.OuterCone<90 && J->TryGetBoolField(TEXT("shadows"),V.bShadows);
}
TSharedPtr<FJsonObject> Write(const FPortableMapLight& V)
{
    auto J=MakeShared<FJsonObject>();
    auto Array=[&](std::initializer_list<double> Numbers) {TArray<TSharedPtr<FJsonValue>> A; for(double N:Numbers) A.Add(MakeShared<FJsonValueNumber>(N)); return A;};
    auto P=V.Frame.GetLocation();auto Q=V.Frame.GetRotation();
    J->SetStringField(TEXT("id"),V.Id);J->SetStringField(TEXT("type"),V.Type);
    J->SetArrayField(TEXT("position"),Array({P.X,P.Y,P.Z}));J->SetArrayField(TEXT("rotation"),Array({Q.X,Q.Y,Q.Z,Q.W}));
    J->SetArrayField(TEXT("color"),Array({V.Color.R,V.Color.G,V.Color.B}));J->SetNumberField(TEXT("intensity"),V.Intensity);
    J->SetNumberField(TEXT("range"),V.Range);J->SetNumberField(TEXT("innerCone"),V.InnerCone);J->SetNumberField(TEXT("outerCone"),V.OuterCone);J->SetBoolField(TEXT("shadows"),V.bShadows);return J;
}
ULightComponent* Create(UObject* Outer,const FPortableMapLight& V)
{
    ULightComponent* C=V.Type==TEXT("directional")?static_cast<ULightComponent*>(NewObject<UDirectionalLightComponent>(Outer)):
        V.Type==TEXT("spot")?static_cast<ULightComponent*>(NewObject<USpotLightComponent>(Outer)):NewObject<UPointLightComponent>(Outer);
    C->SetMobility(EComponentMobility::Movable);C->SetRelativeTransform(V.Frame);C->SetLightColor(V.Color);C->SetCastShadows(V.bShadows);
    if(auto* Local=Cast<ULocalLightComponent>(C)) {Local->SetIntensityUnits(ELightUnits::Candelas);Local->SetAttenuationRadius(V.Range);}
    if(auto* Spot=Cast<USpotLightComponent>(C)) {Spot->SetInnerConeAngle(V.InnerCone);Spot->SetOuterConeAngle(V.OuterCone);}
    C->SetIntensity(V.Intensity);C->SetVisibility(false);return C;
}
bool Capture(ULightComponent* C,const FTransform& Root,FPortableMapLight& V,FString& Error)
{
    if(C->IsA<UDirectionalLightComponent>()) V.Type=TEXT("directional");
    else if(C->IsA<USpotLightComponent>()) V.Type=TEXT("spot");
    else if(C->IsA<UPointLightComponent>()) V.Type=TEXT("point");
    else {Error=TEXT("Portable maps support only Point, Spot and Directional lights");return false;}
    if(!C->IsVisible()) {Error=TEXT("Remove or enable disabled portable lights before export");return false;}
    V.Frame=C->GetComponentTransform().GetRelativeTransform(Root);V.Frame.SetScale3D(FVector::OneVector);
    V.Color=FLinearColor(C->LightColor);V.bShadows=C->CastShadows;V.Intensity=C->Intensity;
    if(auto* Local=Cast<ULocalLightComponent>(C))
    {
        if(Local->IntensityUnits!=ELightUnits::Candelas) {Error=TEXT("Set portable Point/Spot intensity units to Candelas");return false;}
        V.Range=Local->AttenuationRadius;
    }
    if(auto* Point=Cast<UPointLightComponent>(C)) if(!Point->bUseInverseSquaredFalloff || Point->SourceRadius!=0 || Point->SoftSourceRadius!=0 || Point->SourceLength!=0)
    {Error=TEXT("Use inverse-square point/spot lights with zero source size for portable maps");return false;}
    if(auto* Spot=Cast<USpotLightComponent>(C)) {V.InnerCone=Spot->InnerConeAngle;V.OuterCone=Spot->OuterConeAngle;}
    FPortableMapLight Checked; if(!Read(Write(V),Checked)) {Error=TEXT("Invalid portable light settings");return false;} return true;
}
}
