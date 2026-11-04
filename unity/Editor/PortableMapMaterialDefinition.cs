using System;
using System.IO;
using System.Linq;
using UnityEditor;
using UnityEngine;

namespace Radbot.MapAuthoring.Editor
{
    /// <summary>Editor-owned inputs; the stock Unity material is their preview projection.</summary>
    public sealed class PortableMapMaterialDefinition : ScriptableObject
    {
        public PortableMapMaterials.Profile surface;
        public Color baseColor=Color.white;
        public Texture2D baseColorTexture,normalTexture,roughnessTexture,metallicTexture,aoTexture,emissionTexture;
        [Range(0,1)]public float roughness=.7f,metallic;
        [Range(0,10)]public float normalStrength=1;
        public Color emissionColor=Color.white;
        [Min(0)]public float emissionStrength=1;
        [Range(0,1)]public float opacity=1,alphaCutoff=.5f;
        public Vector2 uvTiling=Vector2.one,uvOffset;
        [HideInInspector]public Material material;
        [HideInInspector]public Texture2D nativeMetalRough,nativeNormal,nativeAO,exportMetalRough;
        [SerializeField,HideInInspector]int projectionCRC;
        [SerializeField,HideInInspector]string sourceHash;
        static readonly System.Collections.Generic.Dictionary<Material,PortableMapMaterialDefinition> transient=new();
        string SourceHash()=>string.Join("|",new[]{baseColorTexture,normalTexture,roughnessTexture,metallicTexture,aoTexture,emissionTexture}.Select(texture=>{string path=AssetDatabase.GetAssetPath(texture);return string.IsNullOrEmpty(path)||path==AssetDatabase.GetAssetPath(this)?"":AssetDatabase.GetAssetDependencyHash(path).ToString();}));
        public void ValidateProjection()
        {
            if(material.ComputeCRC()!=projectionCRC||sourceHash!=SourceHash())throw new InvalidOperationException("Tanuki material preview or source images changed. Use Rebuild Preview in the Tanuki Material Editor before export: "+material.name);
        }

        public static PortableMapMaterialDefinition Find(Material material)
        {
            if(!material)return null;
            string path=AssetDatabase.GetAssetPath(material);
            if(!string.IsNullOrEmpty(path))return AssetDatabase.LoadAllAssetsAtPath(path).OfType<PortableMapMaterialDefinition>().FirstOrDefault();
            return transient.TryGetValue(material,out var data)?data:null;
        }
        public static PortableMapMaterialDefinition Attach(Material material,bool capture=true)
        {
            var existing=Find(material);if(existing)return existing;
            if(capture&&material.IsKeywordEnabled("_METALLICGLOSSMAP")&&material.GetTexture("_MetallicGlossMap"))
                throw new InvalidOperationException("This material uses a packed native metallic/smoothness map. Create a new Tanuki material and assign the separate roughness and metallic source images.");
            var data=CreateInstance<PortableMapMaterialDefinition>();data.name="Tanuki Material Inputs";data.material=material;data.hideFlags=HideFlags.HideInHierarchy;
            if(capture)
            {
                data.surface=PortableMapMaterials.Classify(material);
                data.baseColor=material.GetColor(material.HasProperty("_BaseColor")?"_BaseColor":"_Color").linear;data.opacity=data.baseColor.a;
                data.baseColorTexture=material.mainTexture as Texture2D;
                data.roughness=1-material.GetFloat(material.HasProperty("_Smoothness")?"_Smoothness":"_Glossiness");data.metallic=material.GetFloat("_Metallic");
                data.uvTiling=material.mainTextureScale;data.uvOffset=material.mainTextureOffset;
                if(material.HasProperty("_BumpScale"))data.normalStrength=material.GetFloat("_BumpScale");
                if(material.HasProperty("_BumpMap")&&material.GetTexture("_BumpMap"))data.normalTexture=material.GetTexture("_BumpMap") as Texture2D;
                if(material.HasProperty("_OcclusionMap"))data.aoTexture=material.GetTexture("_OcclusionMap") as Texture2D;
                if(material.HasProperty("_EmissionMap"))data.emissionTexture=material.GetTexture("_EmissionMap") as Texture2D;
                if(material.HasProperty("_EmissionColor")){var emission=material.GetColor("_EmissionColor").linear;data.emissionStrength=Mathf.Max(1,emission.maxColorComponent);data.emissionColor=emission/data.emissionStrength;}
            }
            string path=AssetDatabase.GetAssetPath(material);
            if(!string.IsNullOrEmpty(path)&&path.StartsWith("Assets/",StringComparison.Ordinal))AssetDatabase.AddObjectToAsset(data,material);
            else transient[material]=data;
            return data;
        }
        Texture2D Derived(ref Texture2D target,string label,int width,int height)
        {
            if(!target){target=new Texture2D(width,height,TextureFormat.RGBA32,false,true){name=label,hideFlags=HideFlags.HideInHierarchy};string path=AssetDatabase.GetAssetPath(this);if(!string.IsNullOrEmpty(path))AssetDatabase.AddObjectToAsset(target,this);}
            if(target.width!=width||target.height!=height)target.Reinitialize(width,height);
            return target;
        }
        static Texture2D Read(Texture2D source)
        {
            if(!source)return null;
            if(source.width>8192||source.height>8192)throw new InvalidOperationException("Use textures up to 8192 pixels.");
            var copy=new Texture2D(source.width,source.height,TextureFormat.RGBA32,false,true);
            string path=AssetDatabase.GetAssetPath(source);
            if(File.Exists(path)&&new[]{".png",".jpg",".jpeg"}.Contains(Path.GetExtension(path).ToLowerInvariant()))copy.LoadImage(File.ReadAllBytes(path));
            else if(source.isReadable)copy.SetPixels(source.GetPixels());
            else {DestroyImmediate(copy);throw new InvalidOperationException("Use PNG/JPEG source images or readable generated textures for material data maps.");}
            copy.Apply();return copy;
        }
        public void Apply()
        {
            if(!material)throw new InvalidOperationException("Material inputs have no material owner.");
            PortableMapMaterials.Configure(material,surface);
            material.SetColor(material.HasProperty("_BaseColor")?"_BaseColor":"_Color",new Color(baseColor.r,baseColor.g,baseColor.b,surface is PortableMapMaterials.Profile.Cutout or PortableMapMaterials.Profile.Transparent?opacity:1).gamma);
            material.mainTexture=baseColorTexture;material.mainTextureScale=uvTiling;material.mainTextureOffset=uvOffset;
            material.SetFloat("_Metallic",metallic);material.SetFloat(material.HasProperty("_Smoothness")?"_Smoothness":"_Glossiness",1-roughness);
            material.SetFloat("_Cutoff",alphaCutoff);
            Texture2D rough=null,metal=null,normal=null,ao=null;
            try
            {
                rough=Read(roughnessTexture);metal=Read(metallicTexture);normal=Read(normalTexture);ao=Read(aoTexture);
                if(rough||metal)
                {
                    int w=Mathf.Max(rough?rough.width:1,metal?metal.width:1),h=Mathf.Max(rough?rough.height:1,metal?metal.height:1);
                    var native=new Color[w*h];var portable=new Color[w*h];
                    for(int y=0;y<h;y++)for(int x=0;x<w;x++)
                    {float u=(x+.5f)/w,v=(y+.5f)/h,r=rough?rough.GetPixelBilinear(u,v).r:roughness,m=metal?metal.GetPixelBilinear(u,v).r:metallic;
                        native[y*w+x]=new Color(m,1,1,1-r);portable[y*w+x]=new Color(1,rough?r:1,metal?m:1,1);}
                    Derived(ref nativeMetalRough,"Tanuki Native Metallic Smoothness",w,h).SetPixels(native);nativeMetalRough.Apply();
                    Derived(ref exportMetalRough,"Tanuki Export Metallic Roughness",w,h).SetPixels(portable);exportMetalRough.Apply();
                    material.SetTexture("_MetallicGlossMap",nativeMetalRough);material.EnableKeyword("_METALLICGLOSSMAP");material.SetFloat(material.HasProperty("_Smoothness")?"_Smoothness":"_GlossMapScale",1);
                }
                else {material.SetTexture("_MetallicGlossMap",null);material.DisableKeyword("_METALLICGLOSSMAP");}
                if(normal)
                {var pixels=normal.GetPixels();for(int i=0;i<pixels.Length;i++)pixels[i].a=1;Derived(ref nativeNormal,"Tanuki Native Normal",normal.width,normal.height).SetPixels(pixels);nativeNormal.Apply();material.SetTexture("_BumpMap",nativeNormal);material.EnableKeyword("_NORMALMAP");}
                else {material.SetTexture("_BumpMap",null);material.DisableKeyword("_NORMALMAP");}
                material.SetFloat("_BumpScale",normalStrength);
                if(ao)
                {var pixels=ao.GetPixels();for(int i=0;i<pixels.Length;i++)pixels[i]=new Color(pixels[i].r,pixels[i].r,pixels[i].r,1);Derived(ref nativeAO,"Tanuki Native AO",ao.width,ao.height).SetPixels(pixels);nativeAO.Apply();material.SetTexture("_OcclusionMap",nativeAO);material.SetFloat("_OcclusionStrength",1);}
                else material.SetTexture("_OcclusionMap",null);
                material.SetColor("_EmissionColor",surface==PortableMapMaterials.Profile.Emissive?(emissionColor*emissionStrength).gamma:Color.black);
                material.SetTexture("_EmissionMap",emissionTexture);
                projectionCRC=material.ComputeCRC();sourceHash=SourceHash();
                EditorUtility.SetDirty(this);EditorUtility.SetDirty(material);
            }
            finally{foreach(var copy in new[]{rough,metal,normal,ao})if(copy)DestroyImmediate(copy);}
        }
    }
}
