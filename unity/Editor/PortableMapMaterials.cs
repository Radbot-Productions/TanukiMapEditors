using System;
using System.IO;
using System.Linq;
using UnityEditor;
using UnityEngine;
using UnityEngine.Rendering;
using Object=UnityEngine.Object;

namespace Radbot.MapAuthoring.Editor
{
    public static class PortableMapMaterials
    {
        public enum Profile { PBR, Emissive, Cutout, Transparent }
        public static readonly Profile[] SurfaceProfiles={Profile.PBR,Profile.Cutout,Profile.Transparent};
        public static bool HasEmission(Material material)=>IsTanuki(material)?material.HasProperty("_EmissionEnabled")&&material.GetFloat("_EmissionEnabled")>.5f:material.IsKeywordEnabled("_EMISSION");
        public static readonly string[] TextureProperties={"_MainTex","_BaseMap","_BumpMap","_MetallicGlossMap","_MetallicRoughnessMap","_OcclusionMap","_EmissionMap"};
        public static bool IsTanuki(Material material)=>material&&material.shader&&Enum.GetNames(typeof(Profile)).Any(p=>material.shader.name=="Tanuki/"+p||material.shader.name=="Tanuki/URP/"+p);
        public static bool HasTextures(Material material)=>TextureProperties.Any(p=>material.HasProperty(p)&&material.GetTexture(p));
        public static Profile Classify(Material material)
        {
            if(IsTanuki(material)){var profile=(Profile)Enum.Parse(typeof(Profile),material.shader.name.Split('/').Last());return profile==Profile.Emissive?Profile.PBR:profile;}
            float mode=material.HasProperty("_Surface")?material.GetFloat("_Surface"):material.HasProperty("_Mode")?material.GetFloat("_Mode"):0;
            string renderType=material.GetTag("RenderType",true,"");
            if(mode>=2 || (material.HasProperty("_Surface")&&mode>0)||renderType=="Transparent")return Profile.Transparent;
            if(material.IsKeywordEnabled("_ALPHATEST_ON")||renderType=="TransparentCutout")return Profile.Cutout;
            bool stock=material.shader&&(material.shader.name=="Standard"||material.shader.name=="Universal Render Pipeline/Lit");
            return Profile.PBR;
        }
        public static void Validate(Material material)
        {
            if(!material || !material.shader)throw new InvalidOperationException("Missing map material.");
            string shader=material.shader.name;
            if(!IsTanuki(material)&&shader!="Standard"&&shader!="Universal Render Pipeline/Lit")throw new InvalidOperationException("Material '"+material.name+"' uses unsupported shader '"+shader+"'. Use Tanuki material presets or Repair Materials.");
            foreach(string keyword in new[]{"_PARALLAXMAP","_DETAIL_MULX2","_DETAIL_SCALED","_SPECULAR_SETUP","_RECEIVE_SHADOWS_OFF","_SPECULARHIGHLIGHTS_OFF","_ENVIRONMENTREFLECTIONS_OFF"})
                if(material.IsKeywordEnabled(keyword))throw new InvalidOperationException("Material '"+material.name+"' uses unsupported "+keyword+". Use Repair Materials.");
            if(material.HasProperty("_Cull")&&material.GetFloat("_Cull")!=2)throw new InvalidOperationException("Use back-face culling on portable material '"+material.name+"'.");
            if(shader=="Standard"&&material.GetFloat("_Mode")==3)throw new InvalidOperationException("Use the Tanuki Transparent alpha-blend preset instead of premultiplied transparency: "+material.name);
            var profile=Classify(material);
            if(profile==Profile.Transparent&&material.HasProperty("_Blend")&&material.GetFloat("_Blend")!=0)throw new InvalidOperationException("Use alpha blending on the Tanuki Transparent preset: "+material.name);
            foreach(string property in new[]{material.HasProperty("_BaseColor")?"_BaseColor":"_Color","_EmissionColor"})
            {
                var color=material.GetColor(property);
                if(new[]{color.r,color.g,color.b,color.a}.Any(v=>float.IsNaN(v)||float.IsInfinity(v)||v<0))throw new InvalidOperationException("Invalid color on portable material: "+material.name);
                if(property!="_EmissionColor"&&new[]{color.r,color.g,color.b,color.a}.Any(v=>v>1))throw new InvalidOperationException("Base color and opacity must be within 0–1: "+material.name);
            }
            foreach(string property in new[]{"_Metallic",IsTanuki(material)?"_Roughness":material.HasProperty("_Smoothness")?"_Smoothness":"_Glossiness","_Cutoff","_OcclusionStrength"})
                if(material.HasProperty(property)){float value=material.GetFloat(property);if(float.IsNaN(value)||float.IsInfinity(value)||value<0||value>1)throw new InvalidOperationException("Material value must be within 0–1: "+material.name+" / "+property);}
            if(profile!=Profile.PBR&&HasEmission(material))throw new InvalidOperationException("Emission is supported on the PBR surface: "+material.name);
            if(material.HasProperty("_EmissionStrength")&&(!float.IsFinite(material.GetFloat("_EmissionStrength"))||material.GetFloat("_EmissionStrength")<0))throw new InvalidOperationException("Emission strength must be finite and non-negative: "+material.name);
            if(material.HasProperty("_WorkflowMode")&&material.GetFloat("_WorkflowMode")!=1)throw new InvalidOperationException("Use metallic/roughness workflow: "+material.name);
            if(material.HasProperty("_SmoothnessTextureChannel")&&material.GetFloat("_SmoothnessTextureChannel")!=0)throw new InvalidOperationException("Use metallic texture alpha for smoothness: "+material.name);
            foreach(string property in TextureProperties)
                if(material.HasProperty(property)&&material.GetTexture(property) is Texture texture &&
                    (texture.dimension!=TextureDimension.Tex2D||texture.width>8192||texture.height>8192))throw new InvalidOperationException("Use 2D textures up to 8192 pixels: "+material.name+" / "+property);
            if(material.HasProperty("_BumpScale")&&(!float.IsFinite(material.GetFloat("_BumpScale"))||material.GetFloat("_BumpScale")<0))throw new InvalidOperationException("Normal strength must be finite and non-negative: "+material.name);
            foreach(string property in new[]{"_MetallicRoughnessMap","_MetallicGlossMap","_OcclusionMap"})
                if(material.HasProperty(property)&&material.GetTexture(property) is Texture data&&AssetImporter.GetAtPath(AssetDatabase.GetAssetPath(data)) is TextureImporter importer&&importer.sRGBTexture)
                    throw new InvalidOperationException("Disable sRGB (Color Texture) on material data image '"+data.name+"' before export: "+property);
        }
        public static Material Create(Profile profile)
        {
            bool emission=profile==Profile.Emissive;profile=emission?Profile.PBR:profile;
            bool urp=GraphicsSettings.currentRenderPipeline;
            var shader=TanukiMaterialShaders.Find(profile,urp);
            if(!shader)throw new InvalidOperationException("Tanuki materials require Built-in or Universal Render Pipeline.");
            var material=new Material(shader){name="Tanuki "+profile};
            material.SetColor("_Color",profile==Profile.Transparent?new Color(.6f,.8f,1,.4f):new Color(.6f,.6f,.6f,1));
            material.SetFloat("_Metallic",0);material.SetFloat("_Roughness",.7f);
            Configure(material,emission?Profile.Emissive:profile);return material;
        }
        public static void Configure(Material material,Profile profile)
        {
            if(IsTanuki(material))
            {
                bool emission=profile==Profile.Emissive;profile=emission?Profile.PBR:profile;
                material.shader=TanukiMaterialShaders.Find(profile,material.shader.name.StartsWith("Tanuki/URP/",StringComparison.Ordinal));
                material.SetOverrideTag("TanukiProfile",profile.ToString());
                material.renderQueue=profile==Profile.Transparent?3000:profile==Profile.Cutout?2450:2000;
                if(material.HasProperty("_EmissionEnabled"))material.SetFloat("_EmissionEnabled",emission?1:0);
                material.SetColor("_EmissionColor",emission?Color.white:Color.black);
                material.globalIlluminationFlags=emission?MaterialGlobalIlluminationFlags.BakedEmissive:MaterialGlobalIlluminationFlags.EmissiveIsBlack;
                return;
            }
            bool urp=material.HasProperty("_Surface"),blend=profile==Profile.Transparent,cutout=profile==Profile.Cutout;
            material.SetOverrideTag("TanukiProfile",profile.ToString());
            material.SetOverrideTag("RenderType",blend?"Transparent":cutout?"TransparentCutout":"Opaque");
            material.SetFloat(urp?"_Surface":"_Mode",urp?(blend?1:0):blend?2:cutout?1:0);
            if(material.HasProperty("_AlphaClip"))material.SetFloat("_AlphaClip",cutout?1:0);
            if(material.HasProperty("_Blend"))material.SetFloat("_Blend",0);
            material.SetFloat("_SrcBlend",(float)(blend?BlendMode.SrcAlpha:BlendMode.One));material.SetFloat("_DstBlend",(float)(blend?BlendMode.OneMinusSrcAlpha:BlendMode.Zero));material.SetFloat("_ZWrite",blend?0:1);
            foreach(string keyword in new[]{"_ALPHATEST_ON","_ALPHABLEND_ON","_ALPHAPREMULTIPLY_ON","_SURFACE_TYPE_TRANSPARENT","_EMISSION"})material.DisableKeyword(keyword);
            if(cutout)material.EnableKeyword("_ALPHATEST_ON");
            if(blend)material.EnableKeyword(urp?"_SURFACE_TYPE_TRANSPARENT":"_ALPHABLEND_ON");
            material.renderQueue=blend?3000:cutout?2450:2000;
            if(profile==Profile.Emissive){material.EnableKeyword("_EMISSION");material.SetColor("_EmissionColor",Color.white);material.globalIlluminationFlags=MaterialGlobalIlluminationFlags.BakedEmissive;}
            else {material.SetColor("_EmissionColor",Color.black);material.globalIlluminationFlags=MaterialGlobalIlluminationFlags.EmissiveIsBlack;}
        }
        public static void CreatePresets(){Selection.activeObject=TanukiMaterialShaders.Sample(Profile.PBR);EditorGUIUtility.PingObject(Selection.activeObject);}
        [MenuItem("Tanuki/Materials/PBR")]static void NewPbr()=>TanukiMaterialShaders.CreateCopy(Profile.PBR);
        [MenuItem("Tanuki/Materials/Cutout")]static void NewCutout()=>TanukiMaterialShaders.CreateCopy(Profile.Cutout);
        [MenuItem("Tanuki/Materials/Transparent")]static void NewTransparent()=>TanukiMaterialShaders.CreateCopy(Profile.Transparent);
        public static string Repair(PortableMapRoot root)
        {
            if(!root)throw new InvalidOperationException("Select a map first.");
            string folder=PortableMapAuthoringWindow.ImportFolder("Repaired Materials");Directory.CreateDirectory(folder);AssetDatabase.Refresh();
            Undo.IncrementCurrentGroup();int group=Undo.GetCurrentGroup();Undo.SetCurrentGroupName("Repair Map Materials");
            var copies=new System.Collections.Generic.Dictionary<Material,Material>();var notes=new System.Collections.Generic.List<string>();
            foreach(var renderer in root.GetComponentsInChildren<MeshRenderer>().Where(r=>!PortablePreviewPlayer.IsPreview(r.transform)))
            {
                var assigned=renderer.sharedMaterials;
                for(int slot=0;slot<assigned.Length;slot++)
                {
                    var source=assigned[slot];
                    if(!source)
                    {
                        var replacement=Create(Profile.PBR);AssetDatabase.CreateAsset(replacement,AssetDatabase.GenerateUniqueAssetPath(folder+"/Default PBR.mat"));
                        Undo.RecordObject(renderer,"Repair Missing Material");assigned[slot]=replacement;notes.Add(renderer.name+" / slot "+slot+": missing material replaced with PBR.");continue;
                    }
                    try{Validate(source);continue;}catch(InvalidOperationException){}
                    if(!copies.TryGetValue(source,out var converted))
                    {
                        var profile=Classify(source);converted=Create(profile);converted.name=source.name+" (Tanuki "+profile+")";
                        foreach(var pair in new[]{new[]{"_BaseColor","_Color"},new[]{"_BaseMap","_MainTex"}})
                        {
                            string from=pair.FirstOrDefault(source.HasProperty),to=pair.FirstOrDefault(converted.HasProperty);
                            if(from==null||to==null)continue;
                            if(pair[0]=="_BaseColor")converted.SetColor(to,source.GetColor(from));
                            else {converted.SetTexture(to,source.GetTexture(from));converted.mainTextureScale=source.mainTextureScale;converted.mainTextureOffset=source.mainTextureOffset;}
                        }
                        foreach(string property in new[]{"_Metallic","_Cutoff","_BumpScale","_OcclusionStrength"})if(source.HasProperty(property)&&converted.HasProperty(property))converted.SetFloat(property,source.GetFloat(property));
                        foreach(string property in new[]{"_BumpMap","_MetallicGlossMap","_OcclusionMap","_EmissionMap"})if(source.HasProperty(property)&&source.GetTexture(property))converted.SetTexture(property,source.GetTexture(property));
                        foreach(string keyword in new[]{"_NORMALMAP","_METALLICGLOSSMAP"})if(source.IsKeywordEnabled(keyword))converted.EnableKeyword(keyword);
                        string smooth=source.HasProperty("_Smoothness")?"_Smoothness":source.HasProperty("_Glossiness")?"_Glossiness":null;
                        if(smooth!=null)converted.SetFloat(converted.HasProperty("_Smoothness")?"_Smoothness":"_Glossiness",source.GetFloat(smooth));
                        if(profile==Profile.Emissive&&source.HasProperty("_EmissionColor"))converted.SetColor("_EmissionColor",source.GetColor("_EmissionColor"));
                        Validate(converted);AssetDatabase.CreateAsset(converted,AssetDatabase.GenerateUniqueAssetPath(folder+"/Material.mat"));copies.Add(source,converted);
                        notes.Add(source.name+" → "+profile+". Recognized PBR properties copied; custom graph, procedural, detail and refraction behavior omitted.");
                    }
                    Undo.RecordObject(renderer,"Repair Map Material");assigned[slot]=converted;
                }
                renderer.sharedMaterials=assigned;
            }
            Undo.FlushUndoRecordObjects();Undo.CollapseUndoOperations(group);Undo.IncrementCurrentGroup();return notes.Count==0?"Materials already satisfy the portable contract.":string.Join("\n",notes);
        }
    }
}
