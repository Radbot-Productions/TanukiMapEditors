#if WITH_DEV_AUTOMATION_TESTS
#include "PortableMapScene.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortableBoundaryManifestTest,"Radbot.PortableMaps.BoundaryContract",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPortableBoundaryManifestTest::RunTest(const FString&)
{
    const FString Start=TEXT(R"({"id":"start-%d","type":"playerStart","position":[0,0,100],"rotation":[0,0,0,1],"spawnTag":""})");
    FString Starts;
    for(int32 I=0;I<4;++I) {if(I) Starts+=TEXT(",");Starts+=Start.Replace(TEXT("%d"),*FString::FromInt(I));}
    const FString Valid=FString::Printf(TEXT(R"({"schemaVersion":2,"mapId":"boundary-test","sceneSha1":"0000000000000000000000000000000000000000","meshes":[{"id":"mesh","mesh":0,"visible":true,"collision":false}],"colliders":[],"markers":[%s],"boundary":{"id":"boundary","shape":"box","position":[300,200,1200],"rotation":[0,0,0,1],"size":[4000,4000,2400]}})"),*Starts);
    FString Error;
    TestTrue(TEXT("Box and four starts accepted"),APortableMapScene::ValidateManifest(Valid,Error));
    TestTrue(TEXT("Cylinder accepted"),APortableMapScene::ValidateManifest(Valid.Replace(TEXT("\"box\""),TEXT("\"cylinder\"")),Error));
    TestFalse(TEXT("Unsupported boundary shape rejected"),APortableMapScene::ValidateManifest(Valid.Replace(TEXT("\"box\""),TEXT("\"sphere\"")),Error));
    TestFalse(TEXT("Negative dimensions rejected"),APortableMapScene::ValidateManifest(Valid.Replace(TEXT("[4000,4000,2400]"),TEXT("[-4000,4000,2400]")),Error));
    TestFalse(TEXT("Boundary cannot alias geometry ID"),APortableMapScene::ValidateManifest(Valid.Replace(TEXT("\"id\":\"boundary\""),TEXT("\"id\":\"mesh\"")),Error));
    const FString Fourth=FString(TEXT(","))+Start.Replace(TEXT("%d"),TEXT("3"));
    const FString Three=Valid.Replace(*Fourth,TEXT(""));
    TestFalse(TEXT("New playable map needs four starts"),APortableMapScene::ValidateManifest(Three,Error));
    return true;
}
#endif
