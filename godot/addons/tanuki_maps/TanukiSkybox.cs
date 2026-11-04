using Godot;

[Tool,GlobalClass]
public partial class TanukiSkybox : Node3D
{
    /// <summary>The sky and base light share a profile, so the presets always replace together.</summary>
    [Export] public TanukiMapRoot.BaseLightingProfile SkyPreset
    {
        get=>GetParent() is TanukiMapRoot root?root.BaseLighting:TanukiMapRoot.BaseLightingProfile.CartoonStreetDay;
        set{if(GetParent() is TanukiMapRoot root)root.BaseLighting=value;}
    }
}
