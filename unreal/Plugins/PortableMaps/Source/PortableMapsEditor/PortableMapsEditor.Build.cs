using UnrealBuildTool;
public class PortableMapsEditor : ModuleRules
{
    public PortableMapsEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]{"Core","CoreUObject","Engine","PortableMaps","DeveloperSettings"});
        PrivateDependencyModuleNames.AddRange(new[]{"UnrealEd","AssetRegistry","AssetTools","ContentBrowser","ContentBrowserData","Json","JsonUtilities","glTFRuntime","MeshDescription","StaticMeshDescription","PhysicsCore","GLTFExporter","RenderCore","RHI","ImageCore","Projects","Slate","SlateCore","AdvancedPreviewScene","DesktopPlatform","ToolMenus","PropertyEditor","InputCore","ApplicationCore","EditorFramework","LevelEditor","OnlineSubsystem"});
        if(Target.Platform==UnrealTargetPlatform.Win64){PrivateDependencyModuleNames.Add("SteamShared");AddEngineThirdPartyPrivateStaticDependencies(Target,"Steamworks");}
    }
}
