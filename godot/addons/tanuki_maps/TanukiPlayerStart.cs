using Godot;

[Tool, GlobalClass]
public partial class TanukiPlayerStart : Node3D
{
    [Export] public string SpawnTag { get; set; } = "";
    public override void _Ready()
    {
        if(!Engine.IsEditorHint() || GetNodeOrNull("_StartPreview")!=null) return;
        AddChild(new MeshInstance3D{Name="_StartPreview",Mesh=new CapsuleMesh{Radius=.21f,Height=.6f},MaterialOverride=new StandardMaterial3D{AlbedoColor=new Color(.2f,.85f,.35f)}});
    }
}
