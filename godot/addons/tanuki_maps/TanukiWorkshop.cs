using Godot;
public sealed class TanukiWorkshop : PortableWorkshopClient
{
    public TanukiWorkshop():base(ProjectSettings.GlobalizePath("res://addons/tanuki_maps/native/tanuki_steam_api64.dll"),ProjectSettings.GlobalizePath("user://workshop")){}
}
