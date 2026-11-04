using Godot;
using System;
using System.IO;

public static class TanukiShaders
{
    const string Root="res://addons/tanuki_maps/";
    public static readonly string[] Profiles={"PBR","Cutout","Transparent"};
    public static string Profile(Material material)
    {
        if(material is ShaderMaterial shader)foreach(string profile in Profiles)
            if(shader.Shader==GD.Load<Shader>(Root+"shaders/Tanuki"+profile+".gdshader"))return profile;
        return "";
    }
    public static ShaderMaterial Create(string profile="PBR")
    {
        if(Array.IndexOf(Profiles,profile)<0)throw new InvalidDataException("Unsupported Tanuki shader: "+profile);
        var material=new ShaderMaterial{ResourceName="Tanuki "+profile,Shader=GD.Load<Shader>(Root+"shaders/Tanuki"+profile+".gdshader")};
        material.SetShaderParameter("base_color",Colors.White);material.SetShaderParameter("roughness",.7f);material.SetShaderParameter("metallic",0f);
        foreach(string channel in new[]{"roughness_channel","metallic_channel","ao_channel"})material.SetShaderParameter(channel,0);
        material.SetShaderParameter("normal_strength",1f);material.SetShaderParameter("uv_tiling",Vector2.One);material.SetShaderParameter("uv_offset",Vector2.Zero);
        if(profile=="Cutout")material.SetShaderParameter("alpha_cutoff",.5f);
        if(profile=="PBR"){material.SetShaderParameter("emission_enabled",false);material.SetShaderParameter("emission_color",Colors.White);material.SetShaderParameter("emission_strength",1f);}
        return material;
    }
    public static StandardMaterial3D ToStandard(Material source)
    {
        string profile=Profile(source);if(profile.Length==0)return source as StandardMaterial3D??throw new InvalidDataException("Use a Tanuki shader or a portable StandardMaterial3D.");
        var shader=(ShaderMaterial)source;
        Variant Read(string name)=>shader.GetShaderParameter(name);
        Texture2D Image(string name){var value=Read(name);return value.VariantType==Variant.Type.Object?value.AsGodotObject() as Texture2D:null;}
        var material=TanukiMaterials.Create(profile=="Cutout"?TanukiMaterials.Profile.Cutout:profile=="Transparent"?TanukiMaterials.Profile.Transparent:TanukiMaterials.Profile.PBR);
        material.ResourceName=source.ResourceName;material.AlbedoColor=Read("base_color").AsColor();material.AlbedoTexture=Image("base_color_texture");
        material.Roughness=Read("roughness").AsSingle();material.Metallic=Read("metallic").AsSingle();
        material.RoughnessTexture=Image("roughness_texture");material.MetallicTexture=Image("metallic_texture");
        material.Set("roughness_texture_channel",Read("roughness_channel"));material.Set("metallic_texture_channel",Read("metallic_channel"));
        material.NormalTexture=Image("normal_texture");material.NormalEnabled=material.NormalTexture!=null;material.NormalScale=Read("normal_strength").AsSingle();
        material.AOTexture=Image("ao_texture");material.AOEnabled=material.AOTexture!=null;material.Set("ao_texture_channel",Read("ao_channel"));
        var scale=Read("uv_tiling").AsVector2();var offset=Read("uv_offset").AsVector2();material.Uv1Scale=new Vector3(scale.X,scale.Y,1);material.Uv1Offset=new Vector3(offset.X,offset.Y,0);
        if(profile=="Cutout")material.AlphaScissorThreshold=Read("alpha_cutoff").AsSingle();
        if(profile=="PBR"&&Read("emission_enabled").AsBool())
        {material.EmissionEnabled=true;material.Emission=Read("emission_color").AsColor();material.EmissionEnergyMultiplier=Read("emission_strength").AsSingle();material.EmissionTexture=Image("emission_texture");material.Set("emission_operator",1);}
        TanukiMaterials.Validate(material,source.ResourceName);return material;
    }
    public static ShaderMaterial FromStandard(StandardMaterial3D source)
    {
        TanukiMaterials.Validate(source,source.ResourceName);
        var surface=TanukiMaterials.Classify(source);string profile=surface==TanukiMaterials.Profile.Cutout?"Cutout":surface==TanukiMaterials.Profile.Transparent?"Transparent":"PBR";
        var material=Create(profile);material.ResourceName=source.ResourceName;
        void Set(string name,Variant value)=>material.SetShaderParameter(name,value);
        Set("base_color",source.AlbedoColor);Set("base_color_texture",source.AlbedoTexture);
        Set("roughness",source.Roughness);Set("metallic",source.Metallic);Set("roughness_texture",source.RoughnessTexture);Set("metallic_texture",source.MetallicTexture);
        Set("roughness_channel",source.Get("roughness_texture_channel"));Set("metallic_channel",source.Get("metallic_texture_channel"));
        if(source.NormalEnabled)Set("normal_texture",source.NormalTexture);Set("normal_strength",source.NormalScale);
        if(source.AOEnabled)Set("ao_texture",source.AOTexture);Set("ao_channel",source.Get("ao_texture_channel"));
        Set("uv_tiling",new Vector2(source.Uv1Scale.X,source.Uv1Scale.Y));Set("uv_offset",new Vector2(source.Uv1Offset.X,source.Uv1Offset.Y));
        if(profile=="Cutout")Set("alpha_cutoff",source.AlphaScissorThreshold);
        if(profile=="PBR"){Set("emission_enabled",source.EmissionEnabled);Set("emission_color",source.Emission);Set("emission_strength",source.EmissionEnergyMultiplier);Set("emission_texture",source.EmissionTexture);}
        return material;
    }
    public static Material ForAuthoring(StandardMaterial3D source)
    {
        // The native shader uses repeat + linear mip filtering. Keep compatible
        // imported native materials for other sampler modes rather than changing them.
        using var defaults=new StandardMaterial3D();
        if(source.TextureFilter!=defaults.TextureFilter||source.TextureRepeat!=defaults.TextureRepeat)return source;
        return FromStandard(source);
    }
    public static void EnsureSamples()
    {
        DirAccess.MakeDirRecursiveAbsolute(Root+"materials");
        foreach(string profile in Profiles)
        {
            string path=Root+"materials/Tanuki "+profile+".tres";
            if(Godot.FileAccess.FileExists(path))continue;
            using var material=Create(profile);if(ResourceSaver.Save(material,path)!=Error.Ok)throw new IOException("Cannot create Tanuki sample: "+path);
        }
        string emission=Root+"materials/Tanuki PBR Emission.tres";
        if(!Godot.FileAccess.FileExists(emission)){using var material=Create();material.ResourceName="Tanuki PBR Emission";material.SetShaderParameter("emission_enabled",true);if(ResourceSaver.Save(material,emission)!=Error.Ok)throw new IOException("Cannot create emission sample.");}
    }
}
