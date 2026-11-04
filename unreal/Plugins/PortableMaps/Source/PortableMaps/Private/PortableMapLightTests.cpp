#if WITH_DEV_AUTOMATION_TESTS
#include "PortableMapLighting.h"
#include "PortableMapScene.h"
#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "UObject/Package.h"

static TAutoConsoleVariable<FString> PortableLightFile(TEXT("PortableMaps.LightTestFile"),TEXT(""),TEXT("Scoped input map for native portable light resource tests."));

namespace
{
class FLightResourceCheck final : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    UWorld* World=nullptr;
    APortableMapScene* Source=nullptr;
    APortableMapScene* Copy=nullptr;
    int32 Step=0;
    bool Done=false,Success=false,Current=true,StaleCallback=false,StaleAccepted=false;
    double Deadline=0;
    TArray<ULightComponent*> Components(APortableMapScene* Scene)
    {TArray<ULightComponent*> Result;Scene->GetComponents(Result);return Result;}
    bool Finish() {if(World){World->GetPackage()->SetDirtyFlag(false);World->DestroyWorld(false);World=nullptr;}return true;}
public:
    explicit FLightResourceCheck(FAutomationTestBase* InTest):Test(InTest){}
    virtual ~FLightResourceCheck(){if(World)World->DestroyWorld(false);}
    virtual bool Update() override
    {
        if(Step==0)
        {
            World=UWorld::CreateWorld(EWorldType::EditorPreview,false);
            Source=World->SpawnActor<APortableMapScene>();Copy=World->SpawnActor<APortableMapScene>();
            FString Error;
            if(!Test->TestTrue(*FString::Printf(TEXT("Load actual light-map resources: %s"),*Error),Source->LoadFile(PortableLightFile.GetValueOnGameThread(),Error))) {Test->AddError(Error);return Finish();}
            auto Lights=Components(Source);Test->TestEqual(TEXT("All three authored lights realized"),Lights.Num(),3);
            for(auto* C:Lights)Test->TestFalse(TEXT("Prepared lights do not illuminate inactive content"),C->IsVisible());
            Source->SetContentActive(true);for(auto* C:Lights)Test->TestTrue(TEXT("Canonical content activation enables lights"),C->IsVisible());
            Source->SetContentActive(false);for(auto* C:Lights)Test->TestFalse(TEXT("Deactivation retires illumination"),C->IsVisible());
            Copy->CopyPreparedAsync(Source,[this](bool Ok,const FString& Error){Success=Ok;Done=true;if(!Ok)Test->AddError(Error);},[this]{return Current;});
            Deadline=FPlatformTime::Seconds()+20;Step=1;return false;
        }
        if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Portable light async operation timed out"));return Finish();}
        if(Step==1)
        {
            if(!Done)return false;
            Test->TestTrue(TEXT("Prepared-copy operation completes"),Success);auto Lights=Components(Copy);Test->TestEqual(TEXT("Prepared reuse retains lights"),Lights.Num(),3);
            Copy->SetContentActive(true);for(auto* C:Lights)Test->TestTrue(TEXT("Reused resources activate"),C->IsVisible());
            Copy->CopyPreparedAsync(Source,[this](bool Ok,const FString&){StaleCallback=true;StaleAccepted=Ok;},[this]{return Current;});
            Current=false;Step=2;return false;
        }
        if(Step==2)
        {
            if(!StaleCallback)return false;
            Test->TestFalse(TEXT("Stale request reports failure"),StaleAccepted);Test->TestTrue(TEXT("Stale resource request cannot replace current content"),Copy->IsLoaded());
            Test->TestEqual(TEXT("Stale request preserves current light count"),Components(Copy).Num(),3);
            Copy->Clear();Test->TestTrue(TEXT("Teardown clears loaded identity"),!Copy->IsLoaded());Test->TestEqual(TEXT("Teardown destroys lights"),Components(Copy).Num(),0);
            Done=false;Success=false;Current=true;
            Copy->LoadFileAsync(PortableLightFile.GetValueOnGameThread(),TEXT(""),[this](bool Ok,const FString& Error){Done=true;Success=Ok;if(!Ok)Test->AddError(Error);},[this]{return Current;});
            Step=3;Deadline=FPlatformTime::Seconds()+30;return false;
        }
        if(!Done)return false;
        Test->TestTrue(TEXT("Asynchronous file preparation completes"),Success);Test->TestEqual(TEXT("Async preparation retains all lights"),Components(Copy).Num(),3);
        Copy->Clear();Test->TestEqual(TEXT("Async resource teardown destroys lights"),Components(Copy).Num(),0);
        Source->Clear();Test->TestEqual(TEXT("Source cleanup destroys lights"),Components(Source).Num(),0);
        return Finish();
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortableLightResourcesTest,"Radbot.PortableMaps.LightResources",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPortableLightResourcesTest::RunTest(const FString&)
{
    if(PortableLightFile.GetValueOnGameThread().IsEmpty()){AddError(TEXT("Set PortableMaps.LightTestFile to an owned three-light fixture first"));return false;}
    ADD_LATENT_AUTOMATION_COMMAND(FLightResourceCheck(this));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortableLightContractTest,"Radbot.PortableMaps.LightContract",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPortableLightContractTest::RunTest(const FString&)
{
    FPortableMapLight V;V.Id=TEXT("lamp");V.Type=TEXT("spot");V.Frame=FTransform(FRotator(-25,40,0),FVector(100,200,300));V.Intensity=1234;V.Range=2300;V.Color=FLinearColor(.2,.4,.8);
    FPortableMapLight Parsed;auto Json=PortableMapLighting::Write(V);
    TestTrue(TEXT("Complete light round trip"),PortableMapLighting::Read(Json,Parsed));TestEqual(TEXT("Physical intensity"),Parsed.Intensity,V.Intensity);TestTrue(TEXT("Position and optical axis"),Parsed.Frame.Equals(V.Frame));
    for(const FString Type:{FString(TEXT("point")),FString(TEXT("spot")),FString(TEXT("directional"))})
    {
        V.Type=Type;auto* C=PortableMapLighting::Create(GetTransientPackage(),V);
        TestFalse(TEXT("New light starts inactive"),C->IsVisible());TestEqual(TEXT("Native intensity"),C->Intensity,V.Intensity);
        if(auto* Local=Cast<ULocalLightComponent>(C)){TestEqual(TEXT("Candela units"),Local->IntensityUnits,ELightUnits::Candelas);TestEqual(TEXT("Range"),Local->AttenuationRadius,V.Range);}
        C->SetVisibility(true);FPortableMapLight Captured;Captured.Id=V.Id;FString Error;
        TestTrue(TEXT("Native component is exportable"),PortableMapLighting::Capture(C,FTransform::Identity,Captured,Error));TestEqual(TEXT("Native light type survives"),Captured.Type,Type);
        C->DestroyComponent();
    }
    Json->SetStringField(TEXT("type"),TEXT("area"));TestFalse(TEXT("Area lights rejected"),PortableMapLighting::Read(Json,Parsed));Json->SetStringField(TEXT("type"),TEXT("spot"));
    Json->SetNumberField(TEXT("range"),-1);TestFalse(TEXT("Negative range rejected"),PortableMapLighting::Read(Json,Parsed));Json->SetNumberField(TEXT("range"),1000);
    Json->SetNumberField(TEXT("innerCone"),80);Json->SetNumberField(TEXT("outerCone"),20);TestFalse(TEXT("Reversed cones rejected"),PortableMapLighting::Read(Json,Parsed));
    FString Starts;for(int I=0;I<4;I++){if(I)Starts+=TEXT(",");Starts+=FString::Printf(TEXT(R"({"id":"s%d","type":"playerStart","position":[0,0,100],"rotation":[0,0,0,1],"spawnTag":""})"),I);}
    V.Type=TEXT("point");FString Light;auto LightWriter=TJsonWriterFactory<>::Create(&Light);FJsonSerializer::Serialize(PortableMapLighting::Write(V).ToSharedRef(),LightWriter);
    const FString Base=FString::Printf(TEXT(R"({"schemaVersion":3,"mapId":"lights","sceneSha1":"0000000000000000000000000000000000000000","meshes":[{"id":"geometry","mesh":0,"visible":true,"collision":true}],"colliders":[],"markers":[%s],"boundary":{"id":"boundary","shape":"box","position":[0,0,400],"rotation":[0,0,0,1],"size":[2000,2000,800]},"lights":[%s]})"),*Starts,*Light);
    FString Error;TestTrue(TEXT("Light manifest accepted"),APortableMapScene::ValidateManifest(Base,Error));
    TestFalse(TEXT("Older schema cannot silently drop lights"),APortableMapScene::ValidateManifest(Base.Replace(TEXT("\"schemaVersion\":3"),TEXT("\"schemaVersion\":2")),Error));
    TestFalse(TEXT("Lights share the global identity namespace"),APortableMapScene::ValidateManifest(Base.Replace(TEXT("\"lamp\""),TEXT("\"geometry\"")),Error));
    return !HasAnyErrors();
}
#endif
