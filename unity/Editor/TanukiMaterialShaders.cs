using System;
using System.IO;
using System.Linq;
using UnityEditor;
using UnityEngine;
using UnityEngine.Rendering;

namespace Radbot.MapAuthoring.Editor
{
    public static class TanukiMaterialShaders
    {
        public static Material Sample(PortableMapMaterials.Profile profile)
        {
            string package=UnityEditor.PackageManager.PackageInfo.FindForAssembly(typeof(TanukiMaterialShaders).Assembly).assetPath;
            string name=profile==PortableMapMaterials.Profile.Emissive?"PBR Emission":profile.ToString();
            var sample=AssetDatabase.LoadAssetAtPath<Material>(package+"/Runtime/Materials/Tanuki "+name+".mat");
            if(!sample)throw new InvalidOperationException("Tanuki sample material is missing; reinstall the toolkit.");
            return sample;
        }
        public static Material CreateCopy(PortableMapMaterials.Profile profile)
        {
            string folder=AssetDatabase.GetAssetPath(Selection.activeObject);
            if(!AssetDatabase.IsValidFolder(folder))folder=string.IsNullOrEmpty(folder)?"Assets":Path.GetDirectoryName(folder)?.Replace('\\','/');
            if(string.IsNullOrEmpty(folder)||!(folder=="Assets"||folder.StartsWith("Assets/",StringComparison.Ordinal)))folder="Assets";
            var copy=new Material(Sample(profile)){name="Tanuki "+profile};
            if(GraphicsSettings.currentRenderPipeline)copy.shader=Find(profile,true);
            string path=AssetDatabase.GenerateUniqueAssetPath(folder+"/"+copy.name+".mat");AssetDatabase.CreateAsset(copy,path);
            Undo.RegisterCreatedObjectUndo(copy,"Create Tanuki Material");Selection.activeObject=copy;EditorGUIUtility.PingObject(copy);return copy;
        }
        public static void ConvertMap(PortableMapRoot root)
        {
            if(!root)throw new InvalidOperationException("Select a Tanuki map first.");
            foreach(var renderer in root.GetComponentsInChildren<MeshRenderer>(true).Where(r=>!PortablePreviewPlayer.IsPreview(r.transform)))
            {
                var materials=renderer.sharedMaterials;
                for(int slot=0;slot<materials.Length;slot++)
                {
                    var material=materials[slot];if(!material||PortableMapMaterials.IsTanuki(material))continue;
                    string path=AssetDatabase.GetAssetPath(material);
                    if(string.IsNullOrEmpty(path)||!path.EndsWith(".mat",StringComparison.OrdinalIgnoreCase))
                    {
                        const string folder="Assets/TanukiMaps/Materials";Directory.CreateDirectory(folder);AssetDatabase.Refresh();
                        material=new Material(material);AssetDatabase.CreateAsset(material,AssetDatabase.GenerateUniqueAssetPath(folder+"/Tanuki Material.mat"));
                        Undo.RecordObject(renderer,"Use Tanuki Material");materials[slot]=material;renderer.sharedMaterials=materials;
                    }
                    Convert(material);
                }
            }
        }
        [MenuItem("Tanuki/Materials/Use Tanuki Shaders on Selected Map")]
        static void ConvertSelectedMap()
        {
            ConvertMap(Selection.activeGameObject?Selection.activeGameObject.GetComponentInParent<PortableMapRoot>():null);
        }
        static Texture2D ConvertImage(Texture texture,Func<Color,Color> convert,string folder,string name)
        {
            var image=new Texture2D(texture.width,texture.height,TextureFormat.RGBA32,false,true);
            var rt=RenderTexture.GetTemporary(texture.width,texture.height,0,RenderTextureFormat.ARGB32,RenderTextureReadWrite.Linear);
            var previous=RenderTexture.active;
            try
            {
                string source=AssetDatabase.GetAssetPath(texture);
                if(File.Exists(source)&&new[]{".png",".jpg",".jpeg"}.Contains(Path.GetExtension(source).ToLowerInvariant()))image.LoadImage(File.ReadAllBytes(source));
                else {Graphics.Blit(texture,rt);RenderTexture.active=rt;image.ReadPixels(new Rect(0,0,texture.width,texture.height),0,0);}
                image.SetPixels(image.GetPixels().Select(convert).ToArray());image.Apply();
                Directory.CreateDirectory(folder);
                string path=AssetDatabase.GenerateUniqueAssetPath(folder+"/"+name+".png");
                File.WriteAllBytes(path,image.EncodeToPNG());AssetDatabase.ImportAsset(path);
                var importer=(TextureImporter)AssetImporter.GetAtPath(path);importer.sRGBTexture=false;
                importer.textureCompression=TextureImporterCompression.Uncompressed;
                importer.wrapModeU=texture.wrapModeU;importer.wrapModeV=texture.wrapModeV;importer.filterMode=texture.filterMode;importer.SaveAndReimport();
                return AssetDatabase.LoadAssetAtPath<Texture2D>(path);
            }
            finally {RenderTexture.active=previous;RenderTexture.ReleaseTemporary(rt);UnityEngine.Object.DestroyImmediate(image);}
        }
        public static void Convert(Material material)
        {
            if(PortableMapMaterials.IsTanuki(material))return;
            PortableMapMaterials.Validate(material);
            bool urp=GraphicsSettings.currentRenderPipeline;
            var profile=PortableMapMaterials.Classify(material);
            bool emissionEnabled=PortableMapMaterials.HasEmission(material);
            Color color=material.GetColor(material.HasProperty("_BaseColor")?"_BaseColor":"_Color"),emission=material.GetColor("_EmissionColor");
            Texture main=material.mainTexture,normal=material.GetTexture("_BumpMap"),ao=material.GetTexture("_OcclusionMap"),emissionMap=material.GetTexture("_EmissionMap");
            Vector2 scale=material.mainTextureScale,offset=material.mainTextureOffset;
            float rough=1-material.GetFloat(material.HasProperty("_Smoothness")?"_Smoothness":"_Glossiness"),metal=material.GetFloat("_Metallic");
            float normalStrength=material.GetFloat("_BumpScale"),aoStrength=material.GetFloat("_OcclusionStrength"),cutoff=material.GetFloat("_Cutoff");
            Texture packed=null;
            string assetPath=AssetDatabase.GetAssetPath(material);
            string folder=string.IsNullOrEmpty(assetPath)?null:Path.GetDirectoryName(assetPath)?.Replace('\\','/');
            if(string.IsNullOrEmpty(folder))folder="Assets/TanukiMaps/Materials";
            if(material.IsKeywordEnabled("_METALLICGLOSSMAP")&&material.GetTexture("_MetallicGlossMap") is Texture oldPacked)
            {
                float smooth=material.GetFloat(material.HasProperty("_Smoothness")?"_Smoothness":"_GlossMapScale");
                packed=ConvertImage(oldPacked,c=>new Color(1,1-c.a*smooth,c.r,1),folder,"Tanuki-MetallicRoughness");rough=metal=1;
            }
            if(ao)ao=ConvertImage(ao,c=>new Color(c.g,c.g,c.g,1),folder,"Tanuki-AO");
            Undo.RecordObject(material,"Use Tanuki Shader");material.shader=Find(profile,urp);material.shaderKeywords=Array.Empty<string>();
            PortableMapMaterials.Configure(material,profile);
            material.SetColor("_Color",color);material.SetFloat("_Roughness",rough);material.SetFloat("_Metallic",metal);
            material.SetFloat("_BumpScale",normalStrength);material.SetFloat("_OcclusionStrength",aoStrength);material.SetFloat("_Cutoff",cutoff);
            material.SetColor("_EmissionColor",emission);material.SetFloat("_EmissionStrength",1);
            if(material.HasProperty("_EmissionEnabled"))material.SetFloat("_EmissionEnabled",emissionEnabled?1:0);
            material.mainTexture=main;material.mainTextureScale=scale;material.mainTextureOffset=offset;
            material.SetTexture("_BumpMap",normal);material.SetTexture("_OcclusionMap",ao);material.SetTexture("_EmissionMap",emissionMap);material.SetTexture("_MetallicRoughnessMap",packed);
            EditorUtility.SetDirty(material);AssetDatabase.SaveAssetIfDirty(material);
        }
        public static Shader Find(PortableMapMaterials.Profile profile,bool urp)
        {
            string name="Tanuki/"+(urp?"URP/":"")+profile;
            var shader=Shader.Find(name);
            if(!shader&&urp)throw new InvalidOperationException("Install the Tanuki Materials for URP package from the toolkit's Integrations~/URP folder.");
            if(!shader)throw new InvalidOperationException("Missing Tanuki shader: "+name);
            return shader;
        }
    }
}
