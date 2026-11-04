using Godot;

[Tool, GlobalClass]
public partial class TanukiMapBoundary : Node3D
{
    public enum BoundaryShape { Box, Cylinder }
    private BoundaryShape shape;
    [Export] public BoundaryShape Shape { get => shape; set { shape=value; Refresh(); } }
    public override void _Ready() => Refresh();
    void Refresh()
    {
        if (!IsInsideTree()) return;
        var preview=GetNodeOrNull<MeshInstance3D>("_BoundaryPreview");
        if(preview==null){ preview=new MeshInstance3D{Name="_BoundaryPreview"};AddChild(preview); }
        preview.Mesh=shape==BoundaryShape.Box ? new BoxMesh{Size=Vector3.One} : new CylinderMesh{TopRadius=.5f,BottomRadius=.5f,Height=1};
        preview.MaterialOverride=new StandardMaterial3D{AlbedoColor=new Color(.2f,.7f,1,.12f),Transparency=BaseMaterial3D.TransparencyEnum.Alpha,CullMode=BaseMaterial3D.CullModeEnum.Disabled,ShadingMode=BaseMaterial3D.ShadingModeEnum.Unshaded};
        preview.CastShadow=GeometryInstance3D.ShadowCastingSetting.Off;
    }
}
