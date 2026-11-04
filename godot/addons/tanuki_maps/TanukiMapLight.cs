using Godot;
using System;
using System.Text.Json.Nodes;

[Tool,GlobalClass]
public partial class TanukiMapLight : Node3D
{
    public enum LightShape {Point,Spot,Directional}
    [Export] public LightShape Shape {get;set;}
    [Export] public Color Color {get;set;}=Colors.White;
    [Export(PropertyHint.Range,"0,10000000")] public float Intensity {get;set;}=1000;
    [Export(PropertyHint.Range,"0.01,100000")] public float Range {get;set;}=10;
    [Export(PropertyHint.Range,"0,89")] public float InnerCone {get;set;}=20;
    [Export(PropertyHint.Range,"0.01,89")] public float OuterCone {get;set;}=35;
    [Export] public bool Shadows {get;set;}=true;
    Light3D preview;
    LightShape previous;
    public override void _Ready(){SetProcess(true);Refresh();}
    public override void _Process(double delta)=>Refresh();
    public void Refresh()
    {
        if(preview==null || previous!=Shape)
        {
            if(preview!=null){RemoveChild(preview);preview.QueueFree();}
            preview=Shape==LightShape.Point?new OmniLight3D():Shape==LightShape.Spot?new SpotLight3D():new DirectionalLight3D();
            preview.Name="_LightPreview";preview.SetMeta("tanuki_preview",true);preview.Rotation=new Vector3(0,Mathf.Pi,0);AddChild(preview);previous=Shape;
        }
        preview.LightColor=Color;preview.ShadowEnabled=Shadows;
        // Preview exposure/energy differs between editors; export retains physical units.
        var root=GetParent();while(root!=null&&root is not TanukiMapRoot)root=root.GetParent();
        preview.LightEnergy=Intensity*(root is TanukiMapRoot map?TanukiMapEnvironment.ExposureFactor(map):1/1.2f);
        if(preview is OmniLight3D point)point.OmniRange=Range;
        if(preview is SpotLight3D spot){spot.SpotRange=Range;spot.SpotAngle=OuterCone;spot.SpotAngleAttenuation=1;}
    }
    public void Validate()
    {
        if(!Enum.IsDefined(Shape)||!float.IsFinite(Intensity)||Intensity<0||Intensity>10000000||!float.IsFinite(Range)||Range<=0||Range>100000||
            !float.IsFinite(InnerCone)||!float.IsFinite(OuterCone)||InnerCone<0||InnerCone>OuterCone||OuterCone<=0||OuterCone>=90||
            !float.IsFinite(Color.R)||!float.IsFinite(Color.G)||!float.IsFinite(Color.B)||Mathf.Min(Color.R,Mathf.Min(Color.G,Color.B))<0||Mathf.Max(Color.R,Mathf.Max(Color.G,Color.B))>1)
            throw new System.IO.InvalidDataException("Invalid portable light: "+Name);
    }
    public JsonObject Settings(string id)
    {
        Validate();var c=Color.SrgbToLinear();return new JsonObject{["id"]=id,["type"]=Shape.ToString().ToLowerInvariant(),["color"]=new JsonArray(c.R,c.G,c.B),["intensity"]=Intensity,["range"]=Range*100,["innerCone"]=InnerCone,["outerCone"]=OuterCone,["shadows"]=Shadows};
    }
}
