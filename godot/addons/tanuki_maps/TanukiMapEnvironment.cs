using Godot;
using System;
using System.Linq;
using System.Text.Json.Nodes;

public static class TanukiMapEnvironment
{
    public static JsonObject Settings(TanukiMapRoot root)
    {
        using var file=FileAccess.Open("res://addons/tanuki_maps/references/lighting-profiles.json",FileAccess.ModeFlags.Read);
        return JsonNode.Parse(file.GetAsText())![root.LightingKey]!.AsObject();
    }
    public static float ExposureFactor(TanukiMapRoot root)
    {
        var p=Settings(root);float min=(float?)p["minEV100"]??0,max=(float?)p["maxEV100"]??0;
        float ev=Mathf.IsEqualApprox(min,max)?min:(float?)p["referenceEV100"]??0;
        return Mathf.Pow(2,((float?)p["exposureBias"]??0)-ev)/1.2f;
    }
    public static void Apply(TanukiMapRoot root)
    {
        if(!root.OverrideSun)
        {
            var source=Settings(root)["suns"]![0]!;var q=source["quaternion"]!.AsArray();var c=source["color"]!.AsArray();
            root.UpdatingSun=true;
            try{root.SunRotationDegrees=(new Quaternion((float)q[1]!, -(float)q[2]!, -(float)q[0]!, (float)q[3]!)*new Quaternion(Vector3.Up,Mathf.Pi)).GetEuler()*180/Mathf.Pi;root.SunIntensity=(float)source["intensity"]!;root.SunColor=new Color((float)c[0]!, (float)c[1]!, (float)c[2]!).LinearToSrgb();root.SunShadows=true;}
            finally{root.UpdatingSun=false;}
        }
        EnsureSkybox(root);
        var previous=root.GetNodeOrNull("_PreviewEnvironment");
        if(previous!=null){root.RemoveChild(previous);previous.QueueFree();}
        var helpers=new Node3D{Name="_PreviewEnvironment"};helpers.SetMeta("tanuki_preview",true);TanukiMapFile.Own(root,helpers);
        var p=Settings(root);
        foreach(var value in p["suns"]!.AsArray())
        {
            var sun=value!.AsObject();var q=sun["quaternion"]!.AsArray();
            var light=new DirectionalLight3D{Name="Base Sun",Quaternion=new Quaternion((float)q[1]!, -(float)q[2]!, -(float)q[0]!, (float)q[3]!)*new Quaternion(Vector3.Up,Mathf.Pi),ShadowEnabled=true};
            var c=sun["color"]!.AsArray();light.LightColor=new Color((float)c[0]!, (float)c[1]!, (float)c[2]!).LinearToSrgb();
            light.LightEnergy=(float)sun["intensity"]!*ExposureFactor(root);helpers.AddChild(light);light.Owner=root;
            if(root.OverrideSun){light.RotationDegrees=root.SunRotationDegrees;light.LightColor=root.SunColor;light.LightEnergy=root.SunIntensity*ExposureFactor(root);light.ShadowEnabled=root.SunShadows;}
        }
        var material=new ShaderMaterial{Shader=GD.Load<Shader>("res://addons/tanuki_maps/shaders/TanukiSky.gdshader")};
        var image=Image.LoadFromFile("res://addons/tanuki_maps/references/"+root.LightingKey+".hdr");
        if(image==null||image.IsEmpty())throw new InvalidOperationException("The skybox panorama is missing.");
        material.SetShaderParameter("panorama",ImageTexture.CreateFromImage(image));
        var sky=new Sky{SkyMaterial=material};
        var world=new WorldEnvironment{Name="Base Environment",Environment=new Godot.Environment{BackgroundMode=Godot.Environment.BGMode.Sky,Sky=sky,BackgroundEnergyMultiplier=ExposureFactor(root),AmbientLightSource=Godot.Environment.AmbientSource.Sky,AmbientLightEnergy=((float?)p["skyIntensity"]??0),TonemapMode=Godot.Environment.ToneMapper.Filmic}};
        helpers.AddChild(world);world.Owner=root;
    }
    public static TanukiSkybox EnsureSkybox(TanukiMapRoot root)
    {
        var existing=root.GetChildren().OfType<TanukiSkybox>().ToArray();
        if(existing.Length>1)throw new InvalidOperationException("A map has only one Skybox component.");
        if(existing.Length==1)return existing[0];
        var skybox=new TanukiSkybox{Name="Skybox"};skybox.SetMeta("tanuki_preview",true);TanukiMapFile.Own(root,skybox);return skybox;
    }
    public static void References(TanukiMapRoot root)
    {
        var previous=root.GetNodeOrNull("_ScaleReferences");if(previous!=null&&previous.GetMeta("tanuki_reference_version",0).AsInt32()==3)return;
        if(previous!=null){root.RemoveChild(previous);previous.Free();}
        var helpers=new Node3D{Name="_ScaleReferences"};helpers.SetMeta("tanuki_preview",true);TanukiMapFile.Own(root,helpers);
        helpers.SetMeta("tanuki_reference_version",3);Vector3 floorMin=new(-10,-.2f,-10),floorMax=new(10,0,10);float area=0;
        foreach(var mesh in TanukiMapFile.Descendants(root).OfType<MeshInstance3D>())
        {
            Transform3D frame=mesh.Transform;for(var parent=mesh.GetParent();parent!=null&&parent!=root;parent=parent.GetParent())if(parent is Node3D node)frame=node.Transform*frame;
            var bounds=mesh.GetAabb();var min=frame*bounds.GetEndpoint(0);var max=min;
            for(int corner=1;corner<8;corner++){var point=frame*bounds.GetEndpoint(corner);min=min.Min(point);max=max.Max(point);}
            var size=max-min;if(size.Y<Mathf.Min(size.X,size.Z)*.3f&&size.X*size.Z>area){floorMin=min;floorMax=max;area=size.X*size.Z;}
        }
        var center=(floorMin+floorMax)*.5f;float spacing=Mathf.Min(1.5f,(floorMax.X-floorMin.X)*.2f),front=floorMin.Z+Mathf.Min(1.5f,(floorMax.Z-floorMin.Z)*.15f);
        string[] names={"Tanuki","Tengu"};
        for(int i=0;i<names.Length;i++)
        {
            var doc=new GltfDocument();var state=new GltfState();
            var result=doc.AppendFromFile("res://addons/tanuki_maps/references/"+names[i]+".glb",state);
            if(result!=Error.Ok)throw new InvalidOperationException("Missing character scale reference: "+names[i]);
            var character=(Node3D)doc.GenerateScene(state);character.Name=names[i]+" (scale reference)";character.Position=new Vector3(center.X+(i==0?-spacing:spacing),floorMax.Y,front);helpers.AddChild(character);OwnDescendants(character,root);
            var label=new Label3D{Text=names[i]+" (scale reference)",Position=new Vector3(center.X+(i==0?-spacing:spacing),floorMax.Y+2,front),Billboard=BaseMaterial3D.BillboardModeEnum.Enabled};helpers.AddChild(label);label.Owner=root;
        }
    }
    static void OwnDescendants(Node node,Node root){node.Owner=root;foreach(var child in node.GetChildren())OwnDescendants(child,root);}
}
