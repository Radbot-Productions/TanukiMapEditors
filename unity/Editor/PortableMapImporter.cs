using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.SceneManagement;
using Object=UnityEngine.Object;

namespace Radbot.MapAuthoring.Editor
{
    /// <summary>Imports the supported Tanuki static-map GLB subset. Never loads scripts or external URIs.</summary>
    public static class PortableMapImporter
    {
        [Serializable] public sealed class Manifest
        {
            public int schemaVersion;
            public string mapId,sceneSha1;
            public Entry[] meshes;
            public Primitive[] colliders;
            public Marker[] markers;
            public PortableMapAuthoringData authoring;
            public Boundary boundary;
            public MapLight[] lights;
            public string baseLighting;
            public SunOverride baseSun;
        }
        [Serializable] public sealed class Entry {public string id; public int mesh; public bool visible,collision;}
        [Serializable] public sealed class Primitive {public string id,type; public float[] position,rotation,size; public float radius,halfHeight;}
        [Serializable] public sealed class Marker {public string id,type,spawnTag; public float[] position,rotation;}
        [Serializable] public sealed class Boundary {public string id,shape; public float[] position,rotation,size;}
        [Serializable] public sealed class MapLight {public string id,type;public float[] position,rotation,color;public float intensity,range,innerCone,outerCone;public bool shadows;}
        [Serializable] public sealed class SunOverride {public float[] rotation,color;public float intensity;public bool shadows;}
        [Serializable] sealed class Document {public GMesh[] meshes; public Accessor[] accessors; public View[] bufferViews; public GMaterial[] materials; public Image[] images; public Texture[] textures; public Sampler[] samplers;public Buffer[] buffers;public string[] extensionsUsed;}
        [Serializable] sealed class Buffer {public int byteLength; public string uri;}
        [Serializable] sealed class GMesh {public string name; public Part[] primitives;}
        [Serializable] sealed class Part {public Attributes attributes; public int indices,material=-1,mode=4;}
        [Serializable] sealed class Attributes {public int POSITION=-1,NORMAL=-1,TEXCOORD_0=-1;}
        [Serializable] sealed class Accessor {public int bufferView,byteOffset,componentType,count; public string type; public bool normalized;}
        [Serializable] sealed class View {public int buffer,byteOffset,byteLength,byteStride;}
        [Serializable] sealed class Image {public int bufferView; public string mimeType,uri;}
        [Serializable] sealed class Texture {public int source,sampler=-1;}
        [Serializable] sealed class Sampler {public int wrapS=10497,wrapT=10497,magFilter=9729,minFilter=9729;}
        [Serializable] sealed class GMaterial {public string name,alphaMode; public float alphaCutoff=.5f;public bool doubleSided;public float[] emissiveFactor; public Pbr pbrMetallicRoughness;public TextureRef normalTexture,occlusionTexture,emissiveTexture;public MaterialExtensions extensions;}
        [Serializable] sealed class Pbr {public float[] baseColorFactor=new[]{1f,1f,1f,1f}; public float metallicFactor=1,roughnessFactor=1; public TextureRef baseColorTexture,metallicRoughnessTexture;}
        [Serializable] sealed class TextureRef {public int index,texCoord;public float scale=1,strength=1;public TextureExtensions extensions;}
        [Serializable] sealed class TextureExtensions {public TextureTransform KHR_texture_transform;}
        [Serializable] sealed class TextureTransform {public float[] offset=new[]{0f,0f},scale=new[]{1f,1f};public float rotation;public int texCoord;}
        [Serializable] sealed class MaterialExtensions {public EmissionStrength KHR_materials_emissive_strength;}
        [Serializable] sealed class EmissionStrength {public float emissiveStrength=1;}
        sealed class Package {public Manifest manifest; public Document doc; public byte[] binary;}
        static void Require(bool condition,string reason) {if(!condition) throw new InvalidDataException(reason);}
        static T At<T>(T[] a,int i) {Require(a!=null && i>=0 && i<a.Length,"Invalid map array reference."); return a[i];}
        static bool Finite(float f) => !float.IsNaN(f) && !float.IsInfinity(f) && Mathf.Abs(f)<=10000000;
        static bool Finite(Vector3 v) => Finite(v.x)&&Finite(v.y)&&Finite(v.z);
        static Vector3 V(float[] a) {Require(a!=null && a.Length==3 && a.All(Finite),"Invalid vector."); return new Vector3(a[1],a[2],a[0])*.01f;}
        static Quaternion Q(float[] a) {Require(a!=null && a.Length==4 && a.All(Finite),"Invalid rotation."); var q=new Quaternion(a[1],a[2],a[0],a[3]); Require(Quaternion.Dot(q,q)>.0001f,"Invalid rotation."); return q.normalized;}
        static Package Read(string filename)
        {
            using(var r=new BinaryReader(File.OpenRead(filename)))
            {
                Require(r.BaseStream.Length>=16 && r.BaseStream.Length<=264L*1024*1024+16,"Invalid map size.");
                Require(r.ReadUInt32()==0x50414d54 && r.ReadInt32()==1,"Unsupported Tanuki map container.");
                int j=r.ReadInt32(),g=r.ReadInt32();
                Require(j>0 && j<=8*1024*1024 && g>=28 && g<=256*1024*1024 && 16L+j+g==r.BaseStream.Length,"Invalid container lengths.");
                string manifestJson=new UTF8Encoding(false,true).GetString(r.ReadBytes(j));
                var manifestFields=PortableMapJson.Read(manifestJson);
                var m=JsonUtility.FromJson<Manifest>(manifestJson);
                if(m!=null&&!manifestFields.ContainsKey("baseSun"))m.baseSun=null;
                if(manifestFields.TryGetValue("baseSun",out var sunField))
                {
                    Require(m!=null&&m.schemaVersion>=5&&sunField is Dictionary<string,object>,"Unsupported base sun override.");
                    var fields=(Dictionary<string,object>)sunField;Require(fields.TryGetValue("shadows",out var shadows)&&shadows is bool,"Invalid sun shadows.");
                    Require(fields.TryGetValue("intensity",out var intensity)&&intensity is double,"Missing sun intensity.");
                    Require(m.baseSun!=null,"Invalid sun override.");Q(m.baseSun.rotation);var color=V(m.baseSun.color);
                    Require(color.x>=0&&color.y>=0&&color.z>=0&&color.x<=1&&color.y<=1&&color.z<=1&&Finite(m.baseSun.intensity)&&m.baseSun.intensity>=0&&m.baseSun.intensity<=10000000,"Invalid sun color/intensity.");
                }
                if(m!=null && (!manifestFields.ContainsKey("authoring") || manifestFields["authoring"]==null)) m.authoring=null;
                if(m!=null && (!manifestFields.ContainsKey("boundary") || manifestFields["boundary"]==null)) m.boundary=null;
                if(m?.boundary!=null)
                {
                    Require(m.boundary.id=="boundary" && (m.boundary.shape=="box"||m.boundary.shape=="cylinder"),"Unsupported map boundary.");
                    var size=V(m.boundary.size);V(m.boundary.position);Q(m.boundary.rotation);
                    Require(size.x>0 && size.y>0 && size.z>0 && (m.boundary.shape!="cylinder"||Mathf.Abs(size.x-size.z)<.001f),"Invalid map boundary dimensions.");
                }
                Require(m!=null && (m.schemaVersion==1||m.schemaVersion==2||m.schemaVersion==3||m.schemaVersion==4||m.schemaVersion==5) && !string.IsNullOrWhiteSpace(m.mapId) && m.mapId.Length<=128,"Unsupported map manifest.");
                if(m.schemaVersion>=2) Require(m.boundary!=null && m.markers!=null && m.markers.Length>=4,"A playable map requires a boundary and four player starts.");
                if(m.schemaVersion>=3)Require(m.lights!=null,"Light schema requires a lights array.");
                if(m.schemaVersion>=4)Require(m.baseLighting=="cartoon-street-day"||m.baseLighting=="cartoon-street-night","Unsupported base lighting profile.");
                m.lights=m.lights??Array.Empty<MapLight>();
                Require(m.lights.Length<=256 && (m.lights.Length==0 || m.schemaVersion>=3),"Invalid light count or schema.");
                foreach(var l in m.lights)
                {
                    V(l.position);Q(l.rotation);
                    Require((l.type=="point"||l.type=="spot"||l.type=="directional") && l.color!=null && l.color.Length==3 && l.color.All(v=>Finite(v)&&v>=0&&v<=1) &&
                        Finite(l.intensity)&&l.intensity>=0&&Finite(l.range)&&l.range>0&&Finite(l.innerCone)&&Finite(l.outerCone)&&l.innerCone>=0&&l.innerCone<=l.outerCone&&l.outerCone>0&&l.outerCone<90,"Invalid portable light.");
                }
                Require(m.meshes!=null && m.meshes.Length>0 && m.meshes.Length<=10000 && m.colliders!=null && m.colliders.Length<=10000 && m.markers!=null && m.markers.Length>0 && m.markers.Length<=10000,"Invalid map entry counts.");
                foreach(var c in m.colliders)
                {
                    V(c.position); Q(c.rotation);
                    if(c.type=="box") {var size=V(c.size); Require(size.x>0 && size.y>0 && size.z>0,"Invalid box dimensions.");}
                    else if(c.type=="sphere") Require(Finite(c.radius) && c.radius>0,"Invalid sphere radius.");
                    else if(c.type=="capsule") Require(Finite(c.radius) && c.radius>0 && Finite(c.halfHeight) && c.halfHeight>=c.radius,"Invalid capsule dimensions.");
                    else throw new InvalidDataException("Unsupported collider type.");
                }
                foreach(var marker in m.markers) {V(marker.position); Q(marker.rotation); Require(marker.type=="playerStart" && marker.spawnTag!=null && marker.spawnTag.Length<=128,"Unsupported marker.");}
                var ids=m.meshes.Select(e=>e.id).Concat(m.colliders.Select(e=>e.id)).Concat(m.markers.Select(e=>e.id)).Concat(m.lights.Select(e=>e.id)).ToArray();
                Require(ids.All(id=>!string.IsNullOrEmpty(id) && id.Length<=128) && ids.Distinct().Count()==ids.Length,"Duplicate or invalid map entry IDs.");
                byte[] glb=r.ReadBytes(g);
                using(var sha=SHA1.Create()) Require(string.Equals(BitConverter.ToString(sha.ComputeHash(glb)).Replace("-",""),m.sceneSha1,StringComparison.OrdinalIgnoreCase),"Map geometry checksum mismatch.");
                using(var b=new BinaryReader(new MemoryStream(glb)))
                {
                    Require(b.ReadUInt32()==0x46546c67 && b.ReadInt32()==2 && b.ReadInt32()==g,"Invalid GLB header.");
                    int n=b.ReadInt32(); Require(n>0 && n<=g-28 && b.ReadUInt32()==0x4e4f534a,"Invalid GLB JSON.");
                    string glbJson=Encoding.UTF8.GetString(b.ReadBytes(n));
                    var glbFields=PortableMapJson.Read(glbJson);
                    var doc=JsonUtility.FromJson<Document>(glbJson);
                    if(doc.materials!=null && glbFields.TryGetValue("materials",out var materialFields))
                        for(int i=0;i<doc.materials.Length;i++)
                        {
                            var fields=(Dictionary<string,object>)((List<object>)materialFields)[i];
                            var material=doc.materials[i];
                            var pbr=fields.TryGetValue("pbrMetallicRoughness",out var pbrValue)?(Dictionary<string,object>)pbrValue:new Dictionary<string,object>();
                            if(pbrValue==null)material.pbrMetallicRoughness=new Pbr();
                            TextureRef Present(Dictionary<string,object> values,string key,TextureRef reference)
                            {
                                if(!values.TryGetValue(key,out var raw)||raw==null)return null;
                                var data=(Dictionary<string,object>)raw;
                                if(!data.TryGetValue("extensions",out var extensions)||extensions==null)reference.extensions=null;
                                else if(!((Dictionary<string,object>)extensions).ContainsKey("KHR_texture_transform"))reference.extensions.KHR_texture_transform=null;
                                return reference;
                            }
                            material.pbrMetallicRoughness.baseColorTexture=Present(pbr,"baseColorTexture",material.pbrMetallicRoughness.baseColorTexture);
                            material.pbrMetallicRoughness.metallicRoughnessTexture=Present(pbr,"metallicRoughnessTexture",material.pbrMetallicRoughness.metallicRoughnessTexture);
                            material.normalTexture=Present(fields,"normalTexture",material.normalTexture);
                            material.occlusionTexture=Present(fields,"occlusionTexture",material.occlusionTexture);
                            material.emissiveTexture=Present(fields,"emissiveTexture",material.emissiveTexture);
                        }
                    int size=b.ReadInt32(); Require(size>=0 && b.ReadUInt32()==0x004e4942 && b.BaseStream.Position+size==g,"Invalid GLB binary chunk.");
                    Require(doc!=null && doc.buffers!=null && doc.buffers.Length==1 && string.IsNullOrEmpty(doc.buffers[0].uri) && doc.buffers[0].byteLength<=size,"External or invalid GLB buffer.");
                    return new Package {manifest=m,doc=doc,binary=b.ReadBytes(size)};
                }
            }
        }
        public static Manifest Inspect(string filename) => Read(filename).manifest;
        public static string FileHash(string filename) {using(var s=SHA1.Create()) using(var f=File.OpenRead(filename)) return BitConverter.ToString(s.ComputeHash(f)).Replace("-","");}

        public static PortableMapRoot Import(string filename,string assetFolder,bool fork=false,bool replaceCurrent=false)
        {
            if(EditorApplication.isPlayingOrWillChangePlaymode) throw new InvalidOperationException("Import maps from Edit mode.");
            for(int i=0;!replaceCurrent && i<SceneManager.sceneCount;i++)
                if(string.IsNullOrEmpty(SceneManager.GetSceneAt(i).path))
                    throw new InvalidOperationException("Save your untitled scene before opening a map. Unity cannot add a scene alongside an untitled scene; your current work has been retained.");
            var package=Read(filename);
            assetFolder=(assetFolder??"").Replace('\\','/').TrimEnd('/');
            Require(assetFolder.StartsWith("Assets/",StringComparison.Ordinal) && !assetFolder.Split('/').Any(s=>s==".." || s=="." || s.Length==0),"Choose a new folder inside Assets.");
            Require(AssetDatabase.IsValidFolder(Path.GetDirectoryName(assetFolder).Replace('\\','/')) && !Directory.Exists(assetFolder) && !File.Exists(assetFolder),"Choose a new, unused asset folder with an existing parent.");
            var previous=SceneManager.GetActiveScene(); Scene scene=default;
            bool ownsFolder=false;
            try
            {
                Require(AssetDatabase.CreateFolder(Path.GetDirectoryName(assetFolder).Replace('\\','/'),Path.GetFileName(assetFolder)).Length>0,"Cannot create asset folder."); ownsFolder=true;
                scene=EditorSceneManager.NewScene(NewSceneSetup.EmptyScene,replaceCurrent?NewSceneMode.Single:NewSceneMode.Additive);
                var root=Build(package,scene,assetFolder,fork);
                PortablePreviewSetup.Ensure(root);
                // Validate through the actual exporter before committing the generated scene.
                PortableMapExporter.Prepare(root,false);
                Require(EditorSceneManager.SaveScene(scene,assetFolder+"/Map.unity"),"Cannot save imported scene.");
                SceneManager.SetActiveScene(scene); Selection.activeGameObject=root.gameObject;
                return root;
            }
            catch
            {
                if(scene.IsValid()) {if(replaceCurrent) EditorSceneManager.NewScene(NewSceneSetup.EmptyScene,NewSceneMode.Single);else EditorSceneManager.CloseScene(scene,true);}
                if(previous.IsValid() && previous.isLoaded) SceneManager.SetActiveScene(previous);
                if(ownsFolder) AssetDatabase.DeleteAsset(assetFolder);
                throw;
            }
        }
        static PortableMapRoot Build(Package p,Scene scene,string folder,bool fork)
        {
            var m=p.manifest;
            var rootObject=new GameObject("Imported Tanuki Map"); SceneManager.MoveGameObjectToScene(rootObject,scene);
            var root=rootObject.AddComponent<PortableMapRoot>(); root.mapId=fork ? Guid.NewGuid().ToString("N") : m.mapId;
            foreach(var initial in root.GetComponentsInChildren<PortableMapBoundary>(true)) Object.DestroyImmediate(initial.gameObject);
            root.mapName=Path.GetFileName(folder);
            (root.GetComponent<PortableMapBaseLighting>()??root.gameObject.AddComponent<PortableMapBaseLighting>()).SetKey(m.baseLighting??"cartoon-street-day");
            if(m.baseSun!=null){var settings=root.GetComponent<PortableMapBaseLighting>();var sun=m.baseSun;var q=sun.rotation;settings.overrideSun=true;settings.sunRotation=new Quaternion(q[1],q[2],q[0],q[3]).eulerAngles;settings.sunIntensity=sun.intensity;settings.sunColor=new Color(sun.color[0],sun.color[1],sun.color[2]).gamma;settings.sunShadows=sun.shadows;settings.Refresh();}
            var nodes=new List<Transform>(); var bindings=new Dictionary<string,PortableMapAuthoringData.Binding>();
            if(m.authoring!=null)
            {
                var a=m.authoring; Require(a.version==1 && a.nodes!=null && a.nodes.Length>0 && a.nodes.Length<=30000 && a.bindings!=null,"Unsupported or invalid authoring metadata.");
                for(int i=0;i<a.nodes.Length;i++)
                {
                    var n=a.nodes[i]; Require(n!=null && n.name!=null && n.name.Length<=1024 && n.parent==(i==0 ? -1 : n.parent) && (i==0 || n.parent>=0 && n.parent<i),"Invalid authoring hierarchy.");
                    var t=i==0 ? root.transform : new GameObject(n.name).transform;
                    if(i>0) t.SetParent(nodes[n.parent],false);
                    Require(Finite(n.position.x)&&Finite(n.position.y)&&Finite(n.position.z)&&Finite(n.scale.x)&&Finite(n.scale.y)&&Finite(n.scale.z)&&Finite(n.rotation.x)&&Finite(n.rotation.y)&&Finite(n.rotation.z)&&Finite(n.rotation.w)&&Quaternion.Dot(n.rotation,n.rotation)>.0001f,"Invalid authoring transform.");
                    t.name=n.name; t.localPosition=i==0 ? Vector3.zero : n.position; t.localRotation=i==0 ? Quaternion.identity : n.rotation.normalized; t.localScale=i==0 ? Vector3.one : n.scale; nodes.Add(t);
                }
                foreach(var b in a.bindings) {Require(b!=null && b.id!=null && !bindings.ContainsKey(b.id) && b.node>=0 && b.node<nodes.Count && Finite(b.center) && Finite(b.size) && Finite(b.radius) && Finite(b.height),"Invalid authoring binding."); bindings.Add(b.id,b);}
                var ids=m.meshes.Select(e=>e.id).Concat(m.colliders.Select(e=>e.id)).Concat(m.markers.Select(e=>e.id)).Concat(m.lights.Select(e=>e.id)).Concat(m.boundary!=null?new[]{m.boundary.id}:Array.Empty<string>()).ToArray();
                Require(bindings.Count==ids.Length && ids.All(bindings.ContainsKey),"Authoring metadata does not match map content.");
            }
            Transform Owner(string id,string name)
            {
                if(bindings.TryGetValue(id,out var b)) return nodes[b.node];
                var go=new GameObject(name); go.transform.SetParent(root.transform,false); return go.transform;
            }
            var materials=Materials(p,folder);
            int serial=0;
            foreach(var e in m.meshes)
            {
                Require(e.visible || e.collision,"Mesh has no supported use.");
                var source=At(p.doc.meshes,e.mesh); var t=Owner(e.id,source.name??e.id);
                Matrix4x4 frame=root.transform.worldToLocalMatrix*t.localToWorldMatrix;
                Require(Mathf.Abs(frame.determinant)>.000001f,"Degenerate imported transform.");
                var mesh=Mesh(p,source,frame);
                AssetDatabase.CreateAsset(mesh,folder+"/Mesh-"+serial+++".asset");
                if(e.visible)
                {
                    Require(!t.GetComponent<MeshRenderer>() && !t.GetComponent<MeshFilter>(),"Multiple renderers on one authoring node.");
                    t.gameObject.AddComponent<MeshFilter>().sharedMesh=mesh;
                    t.gameObject.AddComponent<MeshRenderer>().sharedMaterials=source.primitives.Select(s=>At(materials,s.material)).ToArray();
                }
                if(e.collision) t.gameObject.AddComponent<MeshCollider>().sharedMesh=mesh;
            }
            foreach(var c in m.colliders)
            {
                var t=Owner(c.id,c.type+" collider"); bool authored=bindings.TryGetValue(c.id,out var b);
                if(authored)
                {
                    if(c.type=="box") Require(b.size.x>0 && b.size.y>0 && b.size.z>0,"Invalid authoring box size.");
                    if(c.type=="sphere" || c.type=="capsule") Require(b.radius>0,"Invalid authoring collider radius.");
                    if(c.type=="capsule") Require(b.height>0 && b.direction>=0 && b.direction<=2,"Invalid authoring capsule settings.");
                }
                if(!authored) {t.localPosition=V(c.position); t.localRotation=Q(c.rotation);}
                if(c.type=="box") {var v=t.gameObject.AddComponent<BoxCollider>(); v.center=authored ? b.center : Vector3.zero; v.size=authored ? b.size : V(c.size);}
                else if(c.type=="sphere") {var v=t.gameObject.AddComponent<SphereCollider>(); v.center=authored ? b.center : Vector3.zero; v.radius=authored ? b.radius : c.radius*.01f;}
                else if(c.type=="capsule") {var v=t.gameObject.AddComponent<CapsuleCollider>(); v.center=authored ? b.center : Vector3.zero; v.radius=authored ? b.radius : c.radius*.01f; v.height=authored ? b.height : c.halfHeight*.02f; v.direction=authored ? b.direction : 1;}
                else throw new InvalidDataException("Unsupported collider type.");
            }
            foreach(var marker in m.markers)
            {
                Require(marker.type=="playerStart" && marker.spawnTag!=null && marker.spawnTag.Length<=128,"Unsupported map marker.");
                var t=Owner(marker.id,"Player Start");
                if(!bindings.ContainsKey(marker.id)) {t.localPosition=V(marker.position); t.localRotation=Q(marker.rotation);}
                Require(!t.GetComponent<PortablePlayerStart>(),"Duplicate player start component.");
                t.gameObject.AddComponent<PortablePlayerStart>().spawnTag=marker.spawnTag;
            }
            foreach(var l in m.lights)
            {
                var t=Owner(l.id,l.type+" light");
                if(!bindings.ContainsKey(l.id)){t.localPosition=V(l.position);t.localRotation=Q(l.rotation);}
                var light=t.gameObject.AddComponent<PortableMapLight>();light.shape=l.type=="point"?PortableMapLight.Shape.Point:l.type=="spot"?PortableMapLight.Shape.Spot:PortableMapLight.Shape.Directional;
                light.color=new Color(l.color[0],l.color[1],l.color[2]).gamma;light.intensity=l.intensity;light.range=l.range*.01f;light.innerCone=l.innerCone;light.outerCone=l.outerCone;light.shadows=l.shadows;light.Refresh();
            }
            if(m.boundary!=null)
            {
                var b=m.boundary;var t=Owner(b.id,"Map Boundary");
                if(!bindings.ContainsKey(b.id)) {t.localPosition=V(b.position);t.localRotation=Q(b.rotation);t.localScale=V(b.size);}
                var frame=root.transform.worldToLocalMatrix*t.localToWorldMatrix;
                Vector3 x=frame.GetColumn(0),y=frame.GetColumn(1),z=frame.GetColumn(2);
                Require(Vector3.Distance(root.transform.InverseTransformPoint(t.position),V(b.position))<.001f &&
                    Vector3.Distance(new Vector3(x.magnitude,y.magnitude,z.magnitude),V(b.size))<.001f &&
                    Quaternion.Angle(Quaternion.Inverse(root.transform.rotation)*t.rotation,Q(b.rotation))<.01f,"Boundary authoring transform does not match its exported frame.");
                t.gameObject.AddComponent<PortableMapBoundary>().shape=b.shape=="box"?PortableMapBoundary.BoundaryShape.Box:PortableMapBoundary.BoundaryShape.Cylinder;
            }
            else PortableMapAuthoringWindow.CreateBoundary(root);
            return root;
        }
        static byte[] Bytes(Package p,int index)
        {
            var v=At(p.doc.bufferViews,index); Require(v.buffer==0 && v.byteOffset>=0 && v.byteLength>=0 && (long)v.byteOffset+v.byteLength<=p.binary.Length,"Invalid GLB buffer view.");
            var result=new byte[v.byteLength]; Array.Copy(p.binary,v.byteOffset,result,0,v.byteLength); return result;
        }
        static float[] Values(Package p,int index,int width,bool integers=false)
        {
            var a=At(p.doc.accessors,index); var v=At(p.doc.bufferViews,a.bufferView);
            Require(a.count>0 && a.count<=10000000 && a.type==(width==1 ? "SCALAR" : width==2 ? "VEC2" : "VEC3") && !a.normalized,"Unsupported GLB accessor.");
            int component=a.componentType==5121 ? 1 : a.componentType==5123 ? 2 : 4;
            Require(integers ? a.componentType==5125 || a.componentType==5123 || a.componentType==5121 : a.componentType==5126,"Unsupported GLB component type.");
            int stride=v.byteStride==0 ? component*width : v.byteStride;
            var bytes=Bytes(p,a.bufferView);
            Require(a.byteOffset>=0 && stride>=component*width && (long)a.byteOffset+(long)(a.count-1)*stride+component*width<=bytes.Length,"GLB accessor exceeds its buffer.");
            var result=new float[checked(a.count*width)];
            using(var r=new BinaryReader(new MemoryStream(bytes)))
                for(int i=0;i<a.count;i++) {r.BaseStream.Position=a.byteOffset+(long)i*stride; for(int k=0;k<width;k++) {float f=integers ? component==1 ? r.ReadByte() : component==2 ? r.ReadUInt16() : r.ReadUInt32() : r.ReadSingle(); Require(Finite(f),"Invalid GLB numeric value."); result[i*width+k]=f;}}
            return result;
        }
        static Mesh Mesh(Package p,GMesh source,Matrix4x4 frame)
        {
            Require(source.primitives!=null && source.primitives.Length>0 && source.primitives.Length<=1024,"Invalid mesh primitives.");
            var vertices=new List<Vector3>(); var normals=new List<Vector3>(); var uvs=new List<Vector2>(); var triangles=new List<int[]>();
            bool hasNormals=true,hasUV=true;
            foreach(var part in source.primitives)
            {
                Require(part.mode==4 && part.attributes!=null,"Only triangle geometry is supported.");
                var v=Values(p,part.attributes.POSITION,3); int offset=vertices.Count;
                var n=part.attributes.NORMAL>=0 ? Values(p,part.attributes.NORMAL,3) : null;
                var uv=part.attributes.TEXCOORD_0>=0 ? Values(p,part.attributes.TEXCOORD_0,2) : null;
                Require((n==null || n.Length==v.Length) && (uv==null || uv.Length/2==v.Length/3),"Invalid vertex attributes.");
                for(int i=0;i<v.Length/3;i++)
                {
                    vertices.Add(frame.inverse.MultiplyPoint3x4(new Vector3(-v[i*3],v[i*3+1],v[i*3+2])));
                    normals.Add(n==null ? Vector3.up : frame.transpose.MultiplyVector(new Vector3(-n[i*3],n[i*3+1],n[i*3+2])).normalized);
                    uvs.Add(uv==null ? Vector2.zero : new Vector2(uv[i*2],1-uv[i*2+1]));
                }
                hasNormals &= n!=null; hasUV &= uv!=null;
                var raw=Values(p,part.indices,1,true); Require(raw.Length%3==0,"Invalid triangle indices.");
                var indices=raw.Select(f=>{Require(f>=0 && f<v.Length/3 && f==Mathf.Floor(f),"Invalid triangle index."); return (int)f+offset;}).ToArray();
                if(frame.determinant>0) for(int i=0;i<indices.Length;i+=3) {int x=indices[i]; indices[i]=indices[i+2]; indices[i+2]=x;}
                triangles.Add(indices);
            }
            var mesh=new Mesh {name=source.name??"Imported mesh",indexFormat=IndexFormat.UInt32}; mesh.SetVertices(vertices); mesh.subMeshCount=triangles.Count;
            for(int i=0;i<triangles.Count;i++) mesh.SetTriangles(triangles[i],i);
            if(hasNormals) mesh.SetNormals(normals); else mesh.RecalculateNormals();
            if(hasUV){mesh.SetUVs(0,uvs);mesh.RecalculateTangents();} mesh.RecalculateBounds(); return mesh;
        }
        static Material[] Materials(Package p,string folder)
        {
            Require((p.doc.extensionsUsed??Array.Empty<string>()).All(e=>e=="KHR_texture_transform"||e=="KHR_materials_emissive_strength"),"Map uses material or geometry extensions outside the Tanuki contract.");
            var images=new List<byte[]>();
            foreach(var image in p.doc.images??Array.Empty<Image>())
            {
                Require(string.IsNullOrEmpty(image.uri) && image.mimeType=="image/png","Only embedded PNG textures are supported.");
                byte[] bytes=Bytes(p,image.bufferView);
                Require(bytes.Length>=24 && bytes[0]==137 && bytes[1]==80 && bytes[2]==78 && bytes[3]==71,"Invalid PNG.");
                uint Dimension(int offset) => ((uint)bytes[offset]<<24)|((uint)bytes[offset+1]<<16)|((uint)bytes[offset+2]<<8)|bytes[offset+3];
                Require(Dimension(16)>0 && Dimension(16)<=8192 && Dimension(20)>0 && Dimension(20)<=8192,"Texture exceeds supported dimensions.");
                images.Add(bytes);
            }
            var cache=new Dictionary<string,Texture2D>();
            Texture2D Load(TextureRef reference,string kind="color",float metallic=1,float roughness=1)
            {
                Require(reference.texCoord==0,"Only UV0 is supported for portable materials.");
                var entry=At(p.doc.textures,reference.index);var sampler=entry.sampler<0?new Sampler():At(p.doc.samplers,entry.sampler);
                string key=reference.index+"-"+kind+"-"+metallic.ToString("R",System.Globalization.CultureInfo.InvariantCulture)+"-"+roughness.ToString("R",System.Globalization.CultureInfo.InvariantCulture);
                if(cache.TryGetValue(key,out var existing))return existing;
                byte[] bytes=At(images.ToArray(),entry.source);
                if(kind=="metalRough"||kind=="occlusion")
                {
                    var copy=new Texture2D(2,2,TextureFormat.RGBA32,false,true);
                    try
                    {
                        Require(copy.LoadImage(bytes),"Invalid material texture.");var pixels=copy.GetPixels();
                        for(int i=0;i<pixels.Length;i++){var c=pixels[i];pixels[i]=kind=="metalRough"?new Color(c.b*metallic,0,0,1-c.g*roughness):new Color(c.r,c.r,c.r,1);}
                        copy.SetPixels(pixels);copy.Apply();bytes=copy.EncodeToPNG();
                    }
                    finally{Object.DestroyImmediate(copy);}
                }
                string path=folder+"/Texture-"+cache.Count+".png";File.WriteAllBytes(path,bytes);AssetDatabase.ImportAsset(path);
                var importer=(TextureImporter)AssetImporter.GetAtPath(path);
                importer.textureType=kind=="normal"?TextureImporterType.NormalMap:TextureImporterType.Default;
                importer.sRGBTexture=kind=="color";importer.textureCompression=TextureImporterCompression.Uncompressed;importer.mipmapEnabled=sampler.minFilter>=9984;importer.maxTextureSize=8192;
                TextureWrapMode Wrap(int mode)=>mode==33071?TextureWrapMode.Clamp:mode==33648?TextureWrapMode.Mirror:TextureWrapMode.Repeat;
                Require(new[]{10497,33071,33648}.Contains(sampler.wrapS)&&new[]{10497,33071,33648}.Contains(sampler.wrapT),"Unsupported texture wrap mode.");
                importer.wrapModeU=Wrap(sampler.wrapS);importer.wrapModeV=Wrap(sampler.wrapT);importer.filterMode=sampler.magFilter==9728?FilterMode.Point:sampler.minFilter==9987?FilterMode.Trilinear:FilterMode.Bilinear;
                importer.SaveAndReimport();var texture=AssetDatabase.LoadAssetAtPath<Texture2D>(path);cache.Add(key,texture);return texture;
            }
            var result=new List<Material>();
            foreach(var source in p.doc.materials??Array.Empty<GMaterial>())
            {
                var pbr=source.pbrMetallicRoughness; Require(pbr!=null && pbr.baseColorFactor!=null && pbr.baseColorFactor.Length==4 && pbr.baseColorFactor.All(Finite) && Finite(pbr.metallicFactor) && Finite(pbr.roughnessFactor),"Invalid material.");
                Require(!source.doubleSided,"Portable materials use back-face culling.");
                float emissionStrength=source.extensions?.KHR_materials_emissive_strength?.emissiveStrength??1;
                var emissive=source.emissiveFactor??new[]{0f,0f,0f};Require(emissive.Length==3&&emissive.All(v=>Finite(v)&&v>=0)&&Finite(emissionStrength)&&emissionStrength>=0,"Invalid emission.");
                bool emission=emissive.Any(v=>v>0);
                Require(string.IsNullOrEmpty(source.alphaMode)||new[]{"OPAQUE","MASK","BLEND"}.Contains(source.alphaMode),"Unsupported alpha mode.");
                Require(!emission||string.IsNullOrEmpty(source.alphaMode)||source.alphaMode=="OPAQUE","Emission requires the opaque Emissive profile.");
                var profile=source.alphaMode=="BLEND"?PortableMapMaterials.Profile.Transparent:source.alphaMode=="MASK"?PortableMapMaterials.Profile.Cutout:emission?PortableMapMaterials.Profile.Emissive:PortableMapMaterials.Profile.PBR;
                var material=PortableMapMaterials.Create(profile);material.name=source.name??"Imported material";
                var f=pbr.baseColorFactor; var color=new Color(f[0],f[1],f[2],f[3]).gamma;
                material.SetColor(material.HasProperty("_BaseColor") ? "_BaseColor" : "_Color",color); material.SetFloat("_Metallic",pbr.metallicFactor);
                material.SetFloat("_Roughness",pbr.roughnessFactor);
                Vector2? commonScale=null,commonOffset=null;
                void SetTexture(string property,TextureRef reference,string kind="color",float metallic=1,float roughness=1)
                {
                    if(reference==null)return;
                    var transform=reference.extensions?.KHR_texture_transform;
                    var scale=Vector2.one;var offset=Vector2.zero;
                    if(transform!=null)
                    {
                        Require(transform.texCoord==0&&Mathf.Abs(transform.rotation)<.000001f&&transform.scale?.Length==2&&transform.offset?.Length==2&&transform.scale.All(Finite)&&transform.offset.All(Finite),"Only UV0 scale and offset are supported.");
                        scale=new Vector2(transform.scale[0],transform.scale[1]);offset=new Vector2(transform.offset[0],1-scale.y-transform.offset[1]);
                    }
                    Require(!commonScale.HasValue||((commonScale.Value-scale).sqrMagnitude<.000001f&&(commonOffset.Value-offset).sqrMagnitude<.000001f),"Material textures must share UV0 tiling and offset: "+material.name);
                    commonScale=scale;commonOffset=offset;material.mainTextureScale=scale;material.mainTextureOffset=offset;
                    material.SetTexture(property,Load(reference,kind,metallic,roughness));
                }
                SetTexture(material.HasProperty("_BaseMap")?"_BaseMap":"_MainTex",pbr.baseColorTexture);
                SetTexture("_MetallicRoughnessMap",pbr.metallicRoughnessTexture,"data");
                SetTexture("_BumpMap",source.normalTexture,"normal");if(source.normalTexture!=null){material.EnableKeyword("_NORMALMAP");material.SetFloat("_BumpScale",source.normalTexture.scale);}
                SetTexture("_OcclusionMap",source.occlusionTexture,"occlusion");if(source.occlusionTexture!=null)material.SetFloat("_OcclusionStrength",source.occlusionTexture.strength);
                SetTexture("_EmissionMap",source.emissiveTexture);if(emission)material.SetColor("_EmissionColor",new Color(emissive[0]*emissionStrength,emissive[1]*emissionStrength,emissive[2]*emissionStrength).gamma);
                if(source.alphaMode=="MASK")material.SetFloat("_Cutoff",source.alphaCutoff);
                PortableMapMaterials.Validate(material);
                AssetDatabase.CreateAsset(material,folder+"/Material-"+result.Count+".mat"); result.Add(material);
            }
            return result.ToArray();
        }
    }
}
