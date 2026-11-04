using UnrealBuildTool;
public class PortableMaps : ModuleRules
{
    public PortableMaps(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PublicDependencyModuleNames.Add("DeveloperSettings");
        PrivateDependencyModuleNames.AddRange(new[] { "Json", "JsonUtilities", "glTFRuntime", "PhysicsCore" });
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PrivateDependencyModuleNames.AddRange(new[] { "SteamShared", "Steamworks" });
            AddEngineThirdPartyPrivateStaticDependencies(Target, "Steamworks");
        }
    }
}
