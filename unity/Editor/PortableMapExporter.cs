using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using UnityEditor;
using UnityEngine;
using UnityEngine.Rendering;
using Object = UnityEngine.Object;

namespace Radbot.MapAuthoring.Editor
{
    /// <summary>Deliberately small static-scene GLB writer. No dependency on a Unity runtime or AssetBundle.</summary>
    public static class PortableMapExporter
    {
        public static void ExportSelected()
        {
            var root = Selection.activeGameObject ? Selection.activeGameObject.GetComponentInParent<PortableMapRoot>() : null;
            if (!root) throw new InvalidOperationException("Select a Portable Map Root or one of its children.");
            string destination = EditorUtility.SaveFilePanel("Export Tanuki map", "", root.mapId, "tanukimap");
            if (destination.Length > 0) ExportFile(root, destination);
        }

        /// <summary>Diagnostic folder export for batch fixtures; author-facing export uses ExportFile.</summary>
        public static void Export(PortableMapRoot root, string destination)
        {
            Prepare(root).Write(destination);
        }

        public static void ExportFile(PortableMapRoot root, string filename)
        {
            Prepare(root).WriteFile(filename);
        }

        public static GameObject[] GeometryOutsideMap(PortableMapRoot root)
        {
            if(!root || !root.gameObject.scene.IsValid()) return Array.Empty<GameObject>();
            return root.gameObject.scene.GetRootGameObjects()
                .SelectMany(go=>go.GetComponentsInChildren<Component>())
                .Where(c=>c && (c is Renderer || c is Collider || c is Terrain))
                .Where(c=>!c.transform.IsChildOf(root.transform) && !PortablePreviewPlayer.IsPreview(c.transform))
                .Select(c=>c.gameObject).Distinct().ToArray();
        }

        /// <summary>The editor's validation and export use the same complete conversion, including materials and collision.</summary>
        public static PreparedMap Prepare(PortableMapRoot root,bool requirePlayable=true)
        {
            if (!root || string.IsNullOrWhiteSpace(root.mapId) || root.mapId.Length > 128) throw new InvalidOperationException("Set a map ID (1-128 characters).");
            if ((root.transform.lossyScale - Vector3.one).sqrMagnitude > .000001f) throw new InvalidOperationException("Map root must have unit scale.");
            if (root.GetComponentsInChildren<PortableMapRoot>(true).Length != 1) throw new InvalidOperationException("Nested map roots are unsupported.");
            var outside=GeometryOutsideMap(root);
            if(outside.Length>0) throw new InvalidOperationException("Move scene geometry under the map root '"+root.name+"' before exporting: "+string.Join(", ",outside.Take(5).Select(go=>go.name))+(outside.Length>5?", …":"")+". Objects outside the map root are not included in the map.");
            var boundaries=root.GetComponentsInChildren<PortableMapBoundary>(true).Where(b=>!PortablePreviewPlayer.IsPreview(b.transform)).ToArray();
            if(requirePlayable && (boundaries.Length!=1 || !boundaries[0].enabled || !boundaries[0].gameObject.activeInHierarchy))
                throw new InvalidOperationException("A map needs exactly one active Map Boundary. Found "+boundaries.Length+".");
            object boundaryRecord=null;
            if (root.GetComponentsInChildren<Terrain>().Any(c=>!PortablePreviewPlayer.IsPreview(c.transform)) || root.GetComponentsInChildren<SkinnedMeshRenderer>().Any(c=>!PortablePreviewPlayer.IsPreview(c.transform)))
                throw new InvalidOperationException("Prototype supports static MeshRenderers. Bake Terrain to meshes; remove skinned geometry.");
            if (root.GetComponentsInChildren<Rigidbody>().Any(c=>!PortablePreviewPlayer.IsPreview(c.transform))) throw new InvalidOperationException("Dynamic rigidbodies are unsupported in static map exports.");
            if (root.GetComponentsInChildren<LODGroup>().Any(c=>!PortablePreviewPlayer.IsPreview(c.transform))) throw new InvalidOperationException("Select/bake one mesh LOD and remove LODGroup before prototype export.");
            var glb = new Glb(root.transform);
            var authoring = new PortableMapAuthoringData.Capture(root.transform);
            if(boundaries.Length==1)
            {
                var boundary=boundaries[0];
                var frame=root.transform.worldToLocalMatrix*boundary.transform.localToWorldMatrix;
                Vector3 x=frame.GetColumn(0),y=frame.GetColumn(1),z=frame.GetColumn(2);
                var size=new Vector3(x.magnitude,y.magnitude,z.magnitude);
                if(!Enum.IsDefined(typeof(PortableMapBoundary.BoundaryShape),boundary.shape) || frame.determinant<=0 ||
                    !Finite(size) || size.x<.01f || size.y<.01f || size.z<.01f ||
                    Mathf.Abs(Vector3.Dot(x.normalized,y.normalized))>.0001f || Mathf.Abs(Vector3.Dot(y.normalized,z.normalized))>.0001f || Mathf.Abs(Vector3.Dot(x.normalized,z.normalized))>.0001f)
                    throw new InvalidOperationException("Map Boundary needs positive dimensions and an unsheared Transform.");
                if(boundary.shape==PortableMapBoundary.BoundaryShape.Cylinder && Mathf.Abs(size.x-size.z)>.001f)
                    throw new InvalidOperationException("A cylinder boundary needs equal X and Z scale.");
                var position=root.transform.InverseTransformPoint(boundary.transform.position);
                if(!Finite(position)) throw new InvalidOperationException("Map Boundary position is invalid.");
                boundaryRecord=D("id","boundary","shape",boundary.shape==PortableMapBoundary.BoundaryShape.Box?"box":"cylinder",
                    "position",Position(position),"rotation",Rotation(Quaternion.Inverse(root.transform.rotation)*boundary.transform.rotation),"size",Position(size));
                authoring.Add("boundary",boundary.transform);
            }
            var meshes = new List<object>(); var colliders = new List<object>(); var markers = new List<object>();
            int serial = 0;
            foreach (var renderer in root.GetComponentsInChildren<MeshRenderer>())
            {
                if (!renderer.enabled || PortablePreviewPlayer.IsPreview(renderer.transform)) continue;
                var filter = renderer.GetComponent<MeshFilter>();
                if (!filter || !filter.sharedMesh) throw new InvalidOperationException("Missing MeshFilter: " + renderer.name);
                if (renderer.sharedMaterials.Length != filter.sharedMesh.subMeshCount) throw new InvalidOperationException("Each submesh needs exactly one material; layered extra materials are unsupported: " + renderer.name);
                int index = glb.AddMesh(filter.sharedMesh, renderer.transform, renderer.sharedMaterials, false);
                string id="mesh-"+serial++;
                meshes.Add(D("id", id, "mesh", index, "visible", true, "collision", false));
                authoring.Add(id,renderer.transform);
            }
            foreach (var collider in root.GetComponentsInChildren<Collider>())
            {
                if (!collider.enabled || PortablePreviewPlayer.IsPreview(collider.transform)) continue;
                if (collider.isTrigger) throw new InvalidOperationException("Trigger colliders need a supported gameplay marker: " + collider.name);
                string id = "collider-" + serial++;
                authoring.Add(id,collider.transform,collider);
                if (collider is MeshCollider mc)
                {
                    if (mc.convex) throw new InvalidOperationException("Convex MeshColliders are unsupported; use primitive or static triangle collision: " + mc.name);
                    int index = glb.AddMesh(mc.sharedMesh, mc.transform, Array.Empty<Material>(), true);
                    meshes.Add(D("id", id, "mesh", index, "visible", false, "collision", true));
                    continue;
                }
                var t = collider.transform;
                // A primitive cannot represent shear from a rotated child under non-uniform parent scale.
                var m = root.transform.worldToLocalMatrix * t.localToWorldMatrix;
                Vector3 x=m.GetColumn(0), y=m.GetColumn(1), z=m.GetColumn(2);
                if (Mathf.Abs(Vector3.Dot(x.normalized,y.normalized)) > .0001f || Mathf.Abs(Vector3.Dot(x.normalized,z.normalized)) > .0001f || Mathf.Abs(Vector3.Dot(y.normalized,z.normalized)) > .0001f)
                    throw new InvalidOperationException("Sheared primitive collider; use a MeshCollider: " + collider.name);
                var scale = new Vector3(x.magnitude,y.magnitude,z.magnitude);
                if (scale.x <= .00001f || scale.y <= .00001f || scale.z <= .00001f) throw new InvalidOperationException("Degenerate collider scale: " + collider.name);
                Vector3 center; Quaternion rotation = Quaternion.Inverse(root.transform.rotation) * t.rotation;
                Dictionary<string,object> record;
                if (collider is BoxCollider box)
                {
                    center=box.center;
                    record=D("id",id,"type","box","size",Position(Vector3.Scale(box.size,scale)));
                }
                else if (collider is SphereCollider sphere)
                {
                    center=sphere.center;
                    record=D("id",id,"type","sphere","radius",sphere.radius*Mathf.Max(scale.x,Mathf.Max(scale.y,scale.z))*100f);
                }
                else if (collider is CapsuleCollider capsule)
                {
                    center=capsule.center;
                    int axis=capsule.direction;
                    Vector3 direction=axis==0 ? Vector3.right : axis==1 ? Vector3.up : Vector3.forward;
                    rotation *= Quaternion.FromToRotation(Vector3.up,direction);
                    float radius=capsule.radius*Mathf.Max(scale[(axis+1)%3],scale[(axis+2)%3]);
                    record=D("id",id,"type","capsule","radius",radius*100f,"halfHeight",Mathf.Max(radius,capsule.height*scale[axis]*.5f)*100f);
                }
                else throw new InvalidOperationException("Unsupported collider: " + collider.GetType().Name);
                record["position"]=Position(root.transform.InverseTransformPoint(t.TransformPoint(center)));
                record["rotation"]=Rotation(rotation);
                colliders.Add(record);
            }
            foreach (var marker in root.GetComponentsInChildren<PortablePlayerStart>())
            {
                if (!marker.enabled || !marker.gameObject.activeInHierarchy || PortablePreviewPlayer.IsPreview(marker.transform)) continue;
                if (marker.spawnTag.Length>128) throw new InvalidOperationException("Spawn tag exceeds 128 characters.");
                string id="start-"+serial++;
                authoring.Add(id,marker.transform);
                markers.Add(D("id",id,"type","playerStart","spawnTag",marker.spawnTag,
                    "position",Position(root.transform.InverseTransformPoint(marker.transform.position)),
                    "rotation",Rotation(Quaternion.Inverse(root.transform.rotation)*marker.transform.rotation)));
            }
            if (meshes.Count==0) throw new InvalidOperationException("Add static geometry before exporting.");
            if(requirePlayable && markers.Count<4) throw new InvalidOperationException("A map needs at least four active Player Starts. Found "+markers.Count+".");
            if (meshes.Count>10000 || colliders.Count>10000 || markers.Count>10000) throw new InvalidOperationException("Prototype limit: 10,000 entries per category.");
            var lights=new List<object>();
            foreach(var native in root.GetComponentsInChildren<Light>())
            {
                if(PortablePreviewPlayer.IsPreview(native.transform))continue;
                if(!native.GetComponent<PortableMapLight>())throw new InvalidOperationException("Add a Tanuki Map Light component to authored lights: "+native.name);
            }
            foreach(var light in root.GetComponentsInChildren<PortableMapLight>())
            {
                if(PortablePreviewPlayer.IsPreview(light.transform))continue;
                if(!light.enabled || !light.GetComponent<Light>().enabled)throw new InvalidOperationException("Remove or enable disabled Map Lights: "+light.name);
                if(!Enum.IsDefined(typeof(PortableMapLight.Shape),light.shape) || !Finite(light.intensity) || light.intensity<0 || !Finite(light.range) || light.range<=0 || light.range>100000 ||
                    !Finite(light.innerCone) || !Finite(light.outerCone) || light.innerCone<0 || light.innerCone>light.outerCone || light.outerCone<=0 || light.outerCone>=90)
                    throw new InvalidOperationException("Invalid portable light settings: "+light.name);
                Color c=light.color.linear;
                if(!Finite(new Vector3(c.r,c.g,c.b)) || Mathf.Min(c.r,Mathf.Min(c.g,c.b))<0 || c.maxColorComponent>1)throw new InvalidOperationException("Portable light color must be in 0–1: "+light.name);
                string id="light-"+serial++;authoring.Add(id,light.transform);
                lights.Add(D("id",id,"type",light.shape.ToString().ToLowerInvariant(),"position",Position(root.transform.InverseTransformPoint(light.transform.position)),
                    "rotation",Rotation(Quaternion.Inverse(root.transform.rotation)*light.transform.rotation),"color",new[]{c.r,c.g,c.b},"intensity",light.intensity,"range",light.range*100,
                    "innerCone",light.innerCone,"outerCone",light.outerCone,"shadows",light.shadows));
            }
            if(lights.Count>256)throw new InvalidOperationException("Portable maps support up to 256 lights.");
            byte[] bytes=glb.Build();
            if (bytes.Length>256*1024*1024) throw new InvalidOperationException("Prototype GLB limit is 256 MiB.");
            string hash; using (var sha=SHA1.Create()) hash=BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-", "").ToLowerInvariant();
            var environment=root.GetComponent<PortableMapBaseLighting>();
            var manifest=D("schemaVersion",5,"baseLighting",environment?environment.Key:"cartoon-street-day","mapId",root.mapId,"sceneSha1",hash,"meshes",meshes,"colliders",colliders,"markers",markers,"boundary",boundaryRecord,"lights",lights);
            if(environment&&environment.overrideSun){var c=environment.sunColor.linear;if(!Finite(environment.sunRotation)||!Finite(new Vector3(c.r,c.g,c.b))||Mathf.Min(c.r,Mathf.Min(c.g,c.b))<0||c.maxColorComponent>1||!Finite(environment.sunIntensity)||environment.sunIntensity<0||environment.sunIntensity>10000000)throw new InvalidOperationException("Invalid sun override.");manifest["baseSun"]=D("rotation",Rotation(Quaternion.Euler(environment.sunRotation)),"color",new[]{c.r,c.g,c.b},"intensity",environment.sunIntensity,"shadows",environment.sunShadows);}
            string json=Json(manifest);
            json=json.Substring(0,json.Length-1)+",\"authoring\":"+authoring.Json()+"}";
            if(Encoding.UTF8.GetByteCount(json)>8*1024*1024) throw new InvalidOperationException("Map manifest exceeds the runtime's 8 MiB limit.");
            return new PreparedMap(bytes,json,meshes.Count,colliders.Count,markers.Count,hash);
        }

        public sealed class PreparedMap
        {
            readonly byte[] scene;
            readonly string json;
            public readonly int MeshCount, ColliderCount, PlayerStartCount;
            public readonly string SceneHash;
            internal PreparedMap(byte[] scene,string json,int meshes,int colliders,int starts,string hash)
            {
                this.scene=scene; this.json=json; MeshCount=meshes; ColliderCount=colliders; PlayerStartCount=starts; SceneHash=hash;
            }

            /// <summary>Explicit diagnostic output: loose inspection copies plus the self-contained map.</summary>
            public void Write(string destination)
            {
            if (string.IsNullOrWhiteSpace(destination)) throw new ArgumentException("Choose an export folder.",nameof(destination));
            Directory.CreateDirectory(destination);
            // Inspection copies are optional. The runtime opens only the transactional map file below.
            File.WriteAllBytes(Path.Combine(destination,"scene.glb"),scene);
            File.WriteAllText(Path.Combine(destination,"map.json"),json,new UTF8Encoding(false));
            WriteFile(Path.Combine(destination,"map.tanukimap"));
            }

            /// <summary>Writes the self-contained map only. JSON and GLB are embedded, never external references.</summary>
            public void WriteFile(string filename)
            {
            if (string.IsNullOrWhiteSpace(filename)) throw new ArgumentException("Choose a .tanukimap filename.",nameof(filename));
            string package=Path.GetFullPath(filename);
            if (!string.Equals(Path.GetExtension(package),".tanukimap",StringComparison.OrdinalIgnoreCase))
                throw new ArgumentException("The export filename must end in .tanukimap.",nameof(filename));
            string parent=Path.GetDirectoryName(package);
            Directory.CreateDirectory(parent);
            byte[] manifest=Encoding.UTF8.GetBytes(json);
            string temporary=Path.Combine(parent,".map-"+Guid.NewGuid().ToString("N")+".tmp");
            try
            {
                using(var output=File.Create(temporary))
                using(var writer=new BinaryWriter(output))
                {
                    writer.Write(0x50414d54); writer.Write(1); writer.Write(manifest.Length); writer.Write(scene.Length);
                    writer.Write(manifest); writer.Write(scene);
                }
                if(File.Exists(package)) File.Replace(temporary,package,null);
                else File.Move(temporary,package);
            }
            finally { if(File.Exists(temporary)) File.Delete(temporary); }
            Debug.Log($"TANUKI_MAP_EXPORTED path={package} meshes={MeshCount} starts={PlayerStartCount} sha1={SceneHash}");
            }
        }

        // UE X=Unity Z, UE Y=Unity X, UE Z=Unity Y. Units in manifest are centimetres.
        static float[] Position(Vector3 v) => new[] {v.z*100f,v.x*100f,v.y*100f};
        static bool Finite(float v) => !float.IsNaN(v)&&!float.IsInfinity(v)&&Mathf.Abs(v)<=10000000;
        static bool Finite(Vector3 v) => !float.IsNaN(v.x)&&!float.IsNaN(v.y)&&!float.IsNaN(v.z)&&!float.IsInfinity(v.x)&&!float.IsInfinity(v.y)&&!float.IsInfinity(v.z)&&Mathf.Max(Mathf.Abs(v.x),Mathf.Abs(v.y),Mathf.Abs(v.z))<=100000;
        static float[] Rotation(Quaternion q) => new[] {q.z,q.x,q.y,q.w};
        internal static Dictionary<string,object> D(params object[] pairs)
        {
            var d=new Dictionary<string,object>(); for (int i=0;i<pairs.Length;i+=2) d.Add((string)pairs[i],pairs[i+1]); return d;
        }
        internal static string Json(object value)
        {
            if (value==null) return "null";
            if (value is string s) return "\""+s.Replace("\\","\\\\").Replace("\"","\\\"").Replace("\n","\\n").Replace("\r","\\r").Replace("\t","\\t")+"\"";
            if (value is bool b) return b ? "true" : "false";
            if (value is IDictionary dictionary)
            {
                var parts=new List<string>(); foreach(DictionaryEntry e in dictionary) parts.Add(Json(e.Key)+":"+Json(e.Value)); return "{"+string.Join(",",parts)+"}";
            }
            if (value is IEnumerable enumerable) { var parts=new List<string>(); foreach(var item in enumerable) parts.Add(Json(item)); return "["+string.Join(",",parts)+"]"; }
            double n=Convert.ToDouble(value,CultureInfo.InvariantCulture);
            if (double.IsNaN(n)||double.IsInfinity(n)) throw new InvalidOperationException("Non-finite map value.");
            if (value is float f) return f.ToString("R",CultureInfo.InvariantCulture);
            if (value is double d) return d.ToString("R",CultureInfo.InvariantCulture);
            return Convert.ToString(value,CultureInfo.InvariantCulture);
        }

        sealed class Glb
        {
            readonly Transform root;
            readonly MemoryStream binary=new MemoryStream();
            readonly List<object> views=new List<object>(), accessors=new List<object>(), meshes=new List<object>(), nodes=new List<object>(), materials=new List<object>(), images=new List<object>(), textures=new List<object>();
            readonly Dictionary<Material,int> materialIds=new Dictionary<Material,int>();
            readonly Dictionary<(Texture source,string kind,float multiplier),int> textureIds=new Dictionary<(Texture,string,float),int>();
            readonly List<object> samplers=new List<object>();
            public Glb(Transform root) { this.root=root; }
            int View(byte[] data)
            {
                while(binary.Length%4!=0) binary.WriteByte(0);
                int offset=(int)binary.Length; binary.Write(data,0,data.Length);
                views.Add(D("buffer",0,"byteOffset",offset,"byteLength",data.Length)); return views.Count-1;
            }
            int Accessor(byte[] data,int componentType,int count,string type,float[] min=null,float[] max=null)
            {
                var a=D("bufferView",View(data),"componentType",componentType,"count",count,"type",type);
                if(min!=null) { a["min"]=min; a["max"]=max; }
                accessors.Add(a); return accessors.Count-1;
            }
            static byte[] Floats(IEnumerable<float> values) { using(var m=new MemoryStream()) { using(var w=new BinaryWriter(m)) foreach(float v in values) w.Write(v); return m.ToArray(); } }
            static Vector3 Gltf(Vector3 v) => new Vector3(-v.x,v.y,v.z);
            public int AddMesh(Mesh mesh,Transform transform,Material[] assigned,bool collision)
            {
                if(!mesh || !mesh.isReadable) throw new InvalidOperationException("Mesh must be readable for export: "+(mesh ? mesh.name : "null"));
                var frame=root.worldToLocalMatrix*transform.localToWorldMatrix;
                if(Mathf.Abs(frame.determinant)<.000001f) throw new InvalidOperationException("Degenerate mesh transform: "+transform.name);
                var vertices=mesh.vertices.Select(v=>Gltf(frame.MultiplyPoint3x4(v))).ToArray();
                if(vertices.Length==0) throw new InvalidOperationException("Empty mesh: "+mesh.name);
                Vector3 min=vertices[0],max=vertices[0]; foreach(var v in vertices) { min=Vector3.Min(min,v); max=Vector3.Max(max,v); }
                int positions=Accessor(Floats(vertices.SelectMany(v=>new[]{v.x,v.y,v.z})),5126,vertices.Length,"VEC3",new[]{min.x,min.y,min.z},new[]{max.x,max.y,max.z});
                var attributes=D("POSITION",positions);
                var normals=mesh.normals;
                if(!collision && normals.Length==vertices.Length)
                {
                    var normalMatrix=frame.inverse.transpose;
                    attributes["NORMAL"]=Accessor(Floats(normals.Select(v=>Gltf(normalMatrix.MultiplyVector(v)).normalized).SelectMany(v=>new[]{v.x,v.y,v.z})),5126,normals.Length,"VEC3");
                }
                var uv=mesh.uv;
                if(!collision && uv.Length==vertices.Length) attributes["TEXCOORD_0"]=Accessor(Floats(uv.SelectMany(v=>new[]{v.x,1-v.y})),5126,uv.Length,"VEC2");
                var primitives=new List<object>();
                for(int sub=0;sub<mesh.subMeshCount;sub++)
                {
                    if(mesh.GetTopology(sub)!=MeshTopology.Triangles) throw new InvalidOperationException("Only triangle meshes are supported.");
                    var indices=mesh.GetTriangles(sub);
                    if(indices.Length==0) continue;
                    // Reverse winding across the handedness conversion; an authored mirror cancels that reversal.
                    if(frame.determinant>0) for(int i=0;i<indices.Length;i+=3) { int t=indices[i]; indices[i]=indices[i+2]; indices[i+2]=t; }
                    byte[] data; using(var m=new MemoryStream()) { using(var w=new BinaryWriter(m)) foreach(int i in indices) w.Write((uint)i); data=m.ToArray(); }
                    var p=D("attributes",attributes,"indices",Accessor(data,5125,indices.Length,"SCALAR"),"mode",4);
                    if(!collision)
                    {
                        if(sub>=assigned.Length || !assigned[sub]) throw new InvalidOperationException("Missing material: "+transform.name);
                        if(PortableMapMaterials.HasTextures(assigned[sub]) && uv.Length!=vertices.Length) throw new InvalidOperationException("Textured mesh has no UV0: "+transform.name+" / slot "+sub);
                        try{p["material"]=Material(assigned[sub]);}catch(InvalidOperationException error){throw new InvalidOperationException(transform.name+" / material slot "+sub+": "+error.Message,error);}
                    }
                    primitives.Add(p);
                }
                int index=meshes.Count;
                meshes.Add(D("name",transform.name,"primitives",primitives)); nodes.Add(D("mesh",index)); return index;
            }
            int Material(Material material)
            {
                if(materialIds.TryGetValue(material,out int existing)) return existing;
                PortableMapMaterials.Validate(material);
                var profile=PortableMapMaterials.Classify(material);
                string colorKey=material.HasProperty("_BaseColor") ? "_BaseColor" : "_Color";
                Color color=material.GetColor(colorKey).linear;
                var pbr=D("baseColorFactor",new[]{color.r,color.g,color.b,color.a},"metallicFactor",material.GetFloat("_Metallic"),
                    "roughnessFactor",PortableMapMaterials.IsTanuki(material)?material.GetFloat("_Roughness"):1f-material.GetFloat(material.HasProperty("_Smoothness") ? "_Smoothness" : "_Glossiness"));
                Texture texture=material.mainTexture;
                object Reference(Texture value,string kind="color",float multiplier=1)
                {
                    var reference=D("index",Texture(value,kind,multiplier));
                    Vector2 scale=material.mainTextureScale,offset=material.mainTextureOffset;
                    if(scale!=Vector2.one || offset!=Vector2.zero)
                        reference["extensions"]=D("KHR_texture_transform",D("scale",new[]{scale.x,scale.y},"offset",new[]{offset.x,1-scale.y-offset.y}));
                    return reference;
                }
                if(texture)
                {
                    pbr["baseColorTexture"]=Reference(texture);
                }
                var result=D("name",material.name,"pbrMetallicRoughness",pbr,"doubleSided",false,"extras",D("tanukiProfile",profile.ToString()));
                if(profile==PortableMapMaterials.Profile.Cutout){result["alphaMode"]="MASK";result["alphaCutoff"]=material.GetFloat("_Cutoff");}
                if(profile==PortableMapMaterials.Profile.Transparent)result["alphaMode"]="BLEND";
                Texture Map(string key)=>material.HasProperty(key)?material.GetTexture(key):null;
                bool tanuki=PortableMapMaterials.IsTanuki(material);
                if(tanuki&&Map("_MetallicRoughnessMap"))pbr["metallicRoughnessTexture"]=Reference(Map("_MetallicRoughnessMap"),"data");
                if(material.IsKeywordEnabled("_METALLICGLOSSMAP")&&Map("_MetallicGlossMap"))
                {
                    float smooth=material.GetFloat(material.HasProperty("_Smoothness")?"_Smoothness":"_GlossMapScale");
                    pbr["metallicRoughnessTexture"]=Reference(Map("_MetallicGlossMap"),"metalRough",smooth);pbr["metallicFactor"]=1f;pbr["roughnessFactor"]=1f;
                }
                if((tanuki||material.IsKeywordEnabled("_NORMALMAP"))&&Map("_BumpMap"))
                {var normal=(Dictionary<string,object>)Reference(Map("_BumpMap"),"normal");normal["scale"]=material.GetFloat("_BumpScale");result["normalTexture"]=normal;}
                if(Map("_OcclusionMap"))
                {var ao=(Dictionary<string,object>)Reference(Map("_OcclusionMap"),tanuki?"tanukiAO":"occlusion",material.GetFloat("_OcclusionStrength"));ao["strength"]=1f;result["occlusionTexture"]=ao;}
                if(PortableMapMaterials.HasEmission(material))
                {
                    Color emission=material.GetColor("_EmissionColor").linear*(tanuki?material.GetFloat("_EmissionStrength"):1);float strength=Mathf.Max(1,emission.maxColorComponent);
                    result["emissiveFactor"]=new[]{emission.r/strength,emission.g/strength,emission.b/strength};
                    if(strength>1)result["extensions"]=D("KHR_materials_emissive_strength",D("emissiveStrength",strength));
                    if(Map("_EmissionMap"))result["emissiveTexture"]=Reference(Map("_EmissionMap"));
                }
                int index=materials.Count; materials.Add(result); materialIds.Add(material,index); return index;
            }
            int Texture(Texture source,string kind="color",float multiplier=1)
            {
                var key=(source,kind,multiplier);
                if(textureIds.TryGetValue(key,out int existing)) return existing;
                if(source.dimension!=TextureDimension.Tex2D || source.width>8192 || source.height>8192) throw new InvalidOperationException("Only 2D textures up to 8192 pixels are supported.");
                var rt=RenderTexture.GetTemporary(source.width,source.height,0,RenderTextureFormat.ARGB32,kind=="color"?RenderTextureReadWrite.sRGB:RenderTextureReadWrite.Linear);
                RenderTexture previous=RenderTexture.active; Texture2D copy=null;
                try
                {
                    string path=AssetDatabase.GetAssetPath(source);
                    copy=new Texture2D(source.width,source.height,TextureFormat.RGBA32,false,kind!="color");
                    if(kind!="normal"&&File.Exists(path)&&new[]{".png",".jpg",".jpeg"}.Contains(Path.GetExtension(path).ToLowerInvariant()))
                    {if(!copy.LoadImage(File.ReadAllBytes(path)))throw new InvalidOperationException("Cannot read texture: "+path);}
                    else if(kind!="normal"&&source is Texture2D readable&&readable.isReadable)copy.SetPixels(readable.GetPixels());
                    else
                    {
                        Material decoder=null;
                        try
                        {
                            if(kind=="normal")decoder=new Material(Shader.Find("Hidden/Tanuki/ReadNormal"));
                            if(decoder)Graphics.Blit(source,rt,decoder);else Graphics.Blit(source,rt);
                            RenderTexture.active=rt;copy.ReadPixels(new Rect(0,0,source.width,source.height),0,0);
                        }
                        finally{if(decoder)Object.DestroyImmediate(decoder);}
                    }
                    if(copy.width>8192||copy.height>8192)throw new InvalidOperationException("Texture source exceeds 8192 pixels: "+source.name);
                    if(kind=="metalRough"||kind=="occlusion"||kind=="tanukiAO")
                    {
                        var pixels=copy.GetPixels();
                        for(int i=0;i<pixels.Length;i++){var c=pixels[i];float ao=1-multiplier*(1-(kind=="tanukiAO"?c.r:c.g));pixels[i]=kind=="metalRough"?new Color(1,1-c.a*multiplier,c.r,1):new Color(ao,ao,ao,1);}
                        copy.SetPixels(pixels);
                    }
                    copy.Apply();
                    images.Add(D("bufferView",View(copy.EncodeToPNG()),"mimeType","image/png"));
                }
                finally { RenderTexture.active=previous; RenderTexture.ReleaseTemporary(rt); if(copy) Object.DestroyImmediate(copy); }
                int Wrap(TextureWrapMode mode)=>mode==TextureWrapMode.Clamp?33071:mode==TextureWrapMode.Mirror?33648:10497;
                bool mipmaps=source is Texture2D texture2D&&texture2D.mipmapCount>1;
                int minFilter=source.filterMode==FilterMode.Point?(mipmaps?9984:9728):source.filterMode==FilterMode.Trilinear&&mipmaps?9987:mipmaps?9985:9729;
                int index=textures.Count;samplers.Add(D("wrapS",Wrap(source.wrapModeU),"wrapT",Wrap(source.wrapModeV),"magFilter",source.filterMode==FilterMode.Point?9728:9729,"minFilter",minFilter));
                textures.Add(D("source",images.Count-1,"sampler",samplers.Count-1));textureIds.Add(key,index);return index;
            }
            public byte[] Build()
            {
                var doc=D("asset",D("version","2.0","generator","Tanuki Unity Map Authoring 0.1"),"scene",0,
                    "scenes",new[]{D("nodes",Enumerable.Range(0,nodes.Count).ToArray())},"nodes",nodes,"meshes",meshes,
                    "accessors",accessors,"bufferViews",views,"buffers",new[]{D("byteLength",binary.Length)});
                if(materials.Count>0) doc["materials"]=materials;
                if(images.Count>0) { doc["images"]=images; doc["textures"]=textures;doc["samplers"]=samplers; }
                doc["extensionsUsed"]=new[]{"KHR_texture_transform","KHR_materials_emissive_strength"};
                byte[] json=Encoding.UTF8.GetBytes(Json(doc)); int jsonLength=(json.Length+3)&~3;
                while(binary.Length%4!=0) binary.WriteByte(0);
                using(var output=new MemoryStream())
                {
                    using(var w=new BinaryWriter(output))
                    {
                        w.Write(0x46546c67); w.Write(2); w.Write(12+8+jsonLength+8+(int)binary.Length);
                        w.Write(jsonLength); w.Write(0x4e4f534a); w.Write(json); for(int i=json.Length;i<jsonLength;i++) w.Write((byte)32);
                        w.Write((int)binary.Length); w.Write(0x004e4942); w.Write(binary.ToArray());
                    }
                    return output.ToArray();
                }
            }
        }
    }
}
