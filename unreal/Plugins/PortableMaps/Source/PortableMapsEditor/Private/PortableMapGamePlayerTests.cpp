#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "PortableMapGamePlayer.h"
#include "Interfaces/IPluginManager.h"

class FWaitForPortablePreview final : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    double Deadline=FPlatformTime::Seconds()+180;
public:
    explicit FWaitForPortablePreview(FAutomationTestBase* InTest):Test(InTest){}
    virtual bool Update() override
    {
        const FString Status=PortableMapGamePlayer::GetStatus();
        if(Status.Contains(TEXT("Tanuki preview is running")))
        {Test->TestTrue(TEXT("Automatically discovered player reached map readiness"),true);PortableMapGamePlayer::Shutdown();return true;}
        if(FPlatformTime::Seconds()>Deadline)
        {Test->AddError(TEXT("Automatic preview did not reach readiness: ")+Status);PortableMapGamePlayer::Shutdown();return true;}
        return false;
    }
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortablePlayerSetupTest,"Radbot.PortableMaps.Editor.AutomaticGameplayPreview",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPortablePlayerSetupTest::RunTest(const FString&)
{
    const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("PortableMaps"));if(!TestTrue(TEXT("PortableMaps installed"),Plugin.IsValid()))return false;
    FString Error;if(!PortableMapGamePlayer::Start(Plugin->GetBaseDir()/TEXT("Samples/Room.tanukimap"),Error)){AddError(Error);return false;}
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForPortablePreview(this));return true;
}
#endif
