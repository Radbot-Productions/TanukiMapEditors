#if WITH_DEV_AUTOMATION_TESTS
#include "PortableMapScene.h"
#include "PortableMapWorkshopSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Engine/GameInstance.h"

static TAutoConsoleVariable<FString> CVarPortableMapTestFile(TEXT("PortableMaps.TestFile"),TEXT(""),TEXT("Explicit .tanukimap input for portable-map native tests."));

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortableMapSingleFileTest,"Radbot.PortableMaps.SingleFileIntegrity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPortableMapSingleFileTest::RunTest(const FString&)
{
    FString Filename;
    if (!FParse::Value(FCommandLine::Get(),TEXT("PortableMapFileTest="),Filename)
        && !FParse::Value(FCommandLine::Get(),TEXT("PortableMapViewerTest="),Filename))
    { Filename=CVarPortableMapTestFile.GetValueOnGameThread(); }
    if (Filename.IsEmpty()) { AddError(TEXT("Supply -PortableMapFileTest=<export.tanukimap> or set PortableMaps.TestFile")); return false; }
    FPortableMapFileInfo Info; FString Error;
    const bool Valid=APortableMapScene::InspectFile(Filename,Info,Error);
    TestTrue(*FString::Printf(TEXT("Single file contains valid manifest and scene: %s"),*Error),Valid);
    if (!Valid) return false;
    TestTrue(TEXT("Has starts"),Info.PlayerStarts>0);
    TestEqual(TEXT("Whole-file identity is recorded"),Info.FileHash.Len(),40);
    TArray<uint8> Bytes; FFileHelper::LoadFileToArray(Bytes,*Filename);
    const FString Temporary=FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(),TEXT("PortableMapCorruption"),TEXT(".tanukimap"));
    Bytes.Last()^=1; FFileHelper::SaveArrayToFile(Bytes,*Temporary);
    FPortableMapFileInfo Rejected;
    TestFalse(TEXT("Corrupted embedded scene is rejected"),APortableMapScene::InspectFile(Temporary,Rejected,Error));
    TestTrue(TEXT("Failure clears identity"),Rejected.FileHash.IsEmpty());
    Bytes.Last()^=1; Bytes[8]^=1; FFileHelper::SaveArrayToFile(Bytes,*Temporary);
    TestFalse(TEXT("Mismatched container lengths are rejected"),APortableMapScene::InspectFile(Temporary,Rejected,Error));
    IFileManager::Get().Delete(*Temporary);
    TestTrue(TEXT("Corrected retry succeeds"),APortableMapScene::InspectFile(Filename,Rejected,Error));
    TestEqual(TEXT("Corrected retry restores exact identity"),Rejected.FileHash,Info.FileHash);
    auto* Settings=GetMutableDefault<UPortableMapWorkshopSettings>();
    TGuardValue<bool> Guard(Settings->bDeveloperOnlyWorkshopVerified,false);
    FPortableMapWorkshopClient Workshop;
    TestFalse(TEXT("Unverified Workshop configuration rejects admission"),Workshop.CanBegin(Error));
    TestTrue(TEXT("Privacy configuration is the actual rejection reason"),Error.Contains(TEXT("verified")));
    TestFalse(TEXT("Upload shares the same privacy gate"),Workshop.Upload(Filename,TEXT("Test"),TEXT(""),0,Error));
    TestFalse(TEXT("Rejected upload is not pending"),Workshop.IsPending());
    return true;
}
#endif
