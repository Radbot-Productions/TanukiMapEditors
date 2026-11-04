using Godot;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

public static class TanukiMaterials
{
    public enum Profile { PBR, Emissive, Cutout, Transparent }
    static readonly HashSet<string> Properties=new(StringComparer.Ordinal)
    {
        "resource_name","resource_local_to_scene","albedo_color","albedo_texture","metallic","metallic_texture","metallic_texture_channel",
        "roughness","roughness_texture","roughness_texture_channel","emission_enabled","emission","emission_energy_multiplier","emission_texture","emission_operator",
        "normal_enabled","normal_scale","normal_texture","ao_enabled","ao_texture","ao_texture_channel","transparency","alpha_scissor_threshold",
        "uv1_scale","uv1_offset","texture_filter","texture_repeat"
    };
    public static Profile Classify(StandardMaterial3D material)=>material.Transparency is BaseMaterial3D.TransparencyEnum.Alpha or BaseMaterial3D.TransparencyEnum.AlphaDepthPrePass?Profile.Transparent:
        material.Transparency is BaseMaterial3D.TransparencyEnum.AlphaScissor or BaseMaterial3D.TransparencyEnum.AlphaHash?Profile.Cutout:material.EmissionEnabled?Profile.Emissive:Profile.PBR;
    public static StandardMaterial3D Create(Profile profile)
    {
        var material=new StandardMaterial3D{ResourceName="Tanuki "+profile,AlbedoColor=profile==Profile.Transparent?new Color(.6f,.8f,1,.4f):new Color(.6f,.6f,.6f),Metallic=0,Roughness=.7f};
        material.Set("metallic_texture_channel",0);material.Set("roughness_texture_channel",0);material.Set("ao_texture_channel",0);
        if(profile==Profile.Cutout)material.Transparency=BaseMaterial3D.TransparencyEnum.AlphaScissor;
        if(profile==Profile.Transparent)material.Transparency=BaseMaterial3D.TransparencyEnum.Alpha;
        if(profile==Profile.Emissive){material.EmissionEnabled=true;material.Emission=Colors.White;material.Set("emission_operator",1);}
        return material;
    }
    public static void Validate(Material source,string context)
    {
        if(source==null)return;
        if(TanukiShaders.Profile(source).Length>0){using var projected=TanukiShaders.ToStandard(source);return;}
        if(source is not StandardMaterial3D material)throw new InvalidDataException(context+": use Tanuki material presets or Repair Materials; custom shaders are unsupported.");
        if(material.Transparency is not (BaseMaterial3D.TransparencyEnum.Disabled or BaseMaterial3D.TransparencyEnum.AlphaScissor or BaseMaterial3D.TransparencyEnum.Alpha))
            throw new InvalidDataException(context+": unsupported transparency mode. Use the Tanuki Transparent preset.");
        if(material.EmissionEnabled&&Classify(material)!=Profile.Emissive)throw new InvalidDataException(context+": emission requires the opaque Emissive profile.");
        if(material.EmissionEnabled&&material.EmissionTexture!=null&&material.Get("emission_operator").AsInt32()!=1)throw new InvalidDataException(context+": use Multiply for the emission texture.");
        using var defaults=new StandardMaterial3D();
        foreach(var property in material.GetPropertyList())
        {
            string name=property["name"].AsString();
            if(Properties.Contains(name)||name.StartsWith("metadata/",StringComparison.Ordinal)||name=="resource_path"||name=="resource_scene_unique_id"||name=="script")continue;
            if((property["usage"].AsInt64()&(long)PropertyUsageFlags.Storage)==0)continue;
            var actual=material.Get(name);var expected=defaults.Get(name);
            bool Empty(Variant value)=>value.VariantType==Variant.Type.Nil||(value.VariantType==Variant.Type.Object&&value.AsGodotObject()==null);
            if(Empty(actual)&&Empty(expected))continue;
            if(!actual.Equals(expected))throw new InvalidDataException(context+": unsupported material setting '"+name+"' ("+actual+" versus "+expected+"). Use Repair Materials.");
        }
        foreach(var texture in new[]{material.AlbedoTexture,material.MetallicTexture,material.RoughnessTexture,material.NormalTexture,material.AOTexture,material.EmissionTexture})
            if(texture!=null&&(texture is ViewportTexture || texture.GetWidth()>8192||texture.GetHeight()>8192))throw new InvalidDataException(context+": use static 2D textures up to 8192 pixels.");
    }
    public static void CreatePresets()
    {
        TanukiShaders.EnsureSamples();
    }
    public static StandardMaterial3D Convert(Material source,out string report)
    {
        if(source is StandardMaterial3D standard)
        {
            var converted=Create(Classify(standard));
            foreach(string property in Properties.Where(p=>p is not ("resource_name" or "resource_local_to_scene" or "emission_operator")))converted.Set(property,standard.Get(property));
            var profile=Classify(standard);converted.Transparency=profile==Profile.Transparent?BaseMaterial3D.TransparencyEnum.Alpha:profile==Profile.Cutout?BaseMaterial3D.TransparencyEnum.AlphaScissor:BaseMaterial3D.TransparencyEnum.Disabled;
            converted.EmissionEnabled=profile==Profile.Emissive;converted.Set("emission_operator",1);converted.ResourceName=standard.ResourceName+" (Tanuki)";
            Validate(converted,converted.ResourceName);report="Recognized PBR, texture and UV properties copied; unsupported material features removed.";return converted;
        }
        var result=Create(Profile.PBR);var notes=new List<string>();
        if(source is ShaderMaterial shader)
        {
            Variant Read(params string[] names)
            {
                foreach(string name in names){var value=shader.GetShaderParameter(name);if(value.VariantType!=Variant.Type.Nil){notes.Add(name);return value;}}
                return default;
            }
            Color ColorValue(Variant value,Color fallback)=>value.VariantType==Variant.Type.Color?value.AsColor():value.VariantType==Variant.Type.Vector3?new Color(value.AsVector3().X,value.AsVector3().Y,value.AsVector3().Z):fallback;
            float FloatValue(Variant value,float fallback)=>value.VariantType is Variant.Type.Float or Variant.Type.Int?value.AsSingle():fallback;
            Texture2D TextureValue(Variant value)=>value.VariantType==Variant.Type.Object?value.AsGodotObject() as Texture2D:null;
            var color=ColorValue(Read("albedo","base_color","color","tint"),result.AlbedoColor);
            float alpha=FloatValue(Read("opacity","alpha"),color.A),cutoff=FloatValue(Read("alpha_cutoff","alpha_scissor_threshold"),0);
            var emission=ColorValue(Read("emission_color","emissive_color","emission"),Colors.Black);
            var profile=cutoff>0?Profile.Cutout:alpha<.999f?Profile.Transparent:Mathf.Max(emission.R,Mathf.Max(emission.G,emission.B))>0?Profile.Emissive:Profile.PBR;
            result.Dispose();result=Create(profile);color.A=alpha;result.AlbedoColor=color;
            result.Metallic=FloatValue(Read("metallic"),0);result.Roughness=FloatValue(Read("roughness"),.7f);
            result.AlbedoTexture=TextureValue(Read("albedo_texture","base_color_texture","diffuse_texture"));
            result.NormalTexture=TextureValue(Read("normal_texture","normal_map"));result.NormalEnabled=result.NormalTexture!=null;
            result.AOTexture=TextureValue(Read("ao_texture","occlusion_texture"));result.Set("ao_enabled",result.AOTexture!=null);
            result.MetallicTexture=TextureValue(Read("metallic_texture"));result.RoughnessTexture=TextureValue(Read("roughness_texture"));
            if(profile==Profile.Cutout)result.AlphaScissorThreshold=cutoff;
            if(profile==Profile.Emissive){result.Emission=emission;result.EmissionEnergyMultiplier=FloatValue(Read("emission_strength","emission_energy"),1);result.EmissionTexture=TextureValue(Read("emission_texture","emissive_texture"));}
        }
        Validate(result,"Repaired material");
        report=Classify(result)+" conversion. Copied: "+(notes.Count>0?string.Join(", ",notes):"no recognized parameters; neutral defaults used")+". Shader graph/procedural behavior could not be converted.";
        return result;
    }
}
