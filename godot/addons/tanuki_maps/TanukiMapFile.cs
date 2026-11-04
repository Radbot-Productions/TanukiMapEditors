using Godot;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json.Nodes;
using GArray=Godot.Collections.Array;

public static class TanukiMapFile
{
    public static IEnumerable<Node> Descendants(Node root)
    { foreach(var child in root.GetChildren()){if(child is TanukiPreviewPlayer || child.HasMeta("tanuki_preview") || child.Name.ToString() is "_BoundaryPreview" or "_StartPreview")continue;yield return child;foreach(var n in Descendants(child))yield return n;} }
    static JsonArray V(Vector3 v)=>new(v.Z*100,-v.X*100,v.Y*100);
    static JsonArray Size(Vector3 v)=>new(v.Z*100,v.X*100,v.Y*100);
    static JsonArray Q(Quaternion q)=>new(-q.Z,q.X,-q.Y,q.W);
    static float F(JsonNode a,int i)=>a[i]!.GetValue<float>();
    static Vector3 ReadV(JsonNode a)=>new(-F(a,1)*.01f,F(a,2)*.01f,F(a,0)*.01f);
    static Vector3 ReadSize(JsonNode a)=>new(F(a,1)*.01f,F(a,2)*.01f,F(a,0)*.01f);
    static Quaternion ReadQ(JsonNode a)=>new(F(a,1),-F(a,2),-F(a,0),F(a,3));
    static void Frame(JsonObject record,Transform3D t)
    { record["position"]=V(t.Origin);record["rotation"]=Q(t.Basis.Orthonormalized().GetRotationQuaternion()); }
    public static string Hash(string file)=>Convert.ToHexString(SHA1.HashData(File.ReadAllBytes(file))).ToLowerInvariant();
    static void Check(bool valid,string reason){if(!valid)throw new InvalidDataException(reason);}
    static void ValidateSun(JsonObject sun)
    {
        Check(sun["rotation"] is JsonArray q&&q.Count==4&&sun["color"] is JsonArray c&&c.Count==3,"Invalid sun frame/color.");
        for(int i=0;i<4;i++)Check(float.IsFinite(F(sun["rotation"]!,i)),"Invalid sun rotation.");
        Check(Mathf.Abs(ReadQ(sun["rotation"]!).LengthSquared()-1)<.001f,"Sun rotation must be normalized.");
        for(int i=0;i<3;i++){float v=F(sun["color"]!,i);Check(float.IsFinite(v)&&v>=0&&v<=1,"Invalid sun color.");}
        float intensity=sun["intensity"]!.GetValue<float>();Check(float.IsFinite(intensity)&&intensity>=0&&intensity<=10000000,"Invalid sun intensity.");
        _=sun["shadows"]!.GetValue<bool>();
    }
    public static (JsonObject Manifest,byte[] Glb) Read(string path)
    {
        using var stream=File.OpenRead(path);using var r=new BinaryReader(stream);
        Check(stream.Length>=16 && stream.Length<=264L*1024*1024+16,"Invalid map file size.");
        Check(r.ReadUInt32()==0x50414d54 && r.ReadInt32()==1,"Unsupported map file.");
        int j=r.ReadInt32(),g=r.ReadInt32();Check(j>0 && j<=8*1024*1024 && g>0 && g<=256*1024*1024 && 16L+j+g==stream.Length,"Invalid map lengths.");
        var m=JsonNode.Parse(new UTF8Encoding(false,true).GetString(r.ReadBytes(j)))!.AsObject();var glb=r.ReadBytes(g);
        Check(m["schemaVersion"]!.GetValue<int>() is 1 or 2 or 3 or 4 or 5,"Unsupported map schema.");
        if(m["schemaVersion"]!.GetValue<int>()>=4)Check((string?)m["baseLighting"] is "cartoon-street-day" or "cartoon-street-night","Unsupported base lighting profile.");
        if(m.ContainsKey("baseSun")){Check(m["schemaVersion"]!.GetValue<int>()>=5&&m["baseSun"] is JsonObject,"Unsupported sun override.");ValidateSun(m["baseSun"]!.AsObject());}
        if(m["schemaVersion"]!.GetValue<int>()>=3)Check(m["lights"] is JsonArray,"Light schema requires lights.");
        Check(Convert.ToHexString(SHA1.HashData(glb)).Equals((string)m["sceneSha1"]!,StringComparison.OrdinalIgnoreCase),"Map geometry checksum mismatch.");
        using var b=new BinaryReader(new MemoryStream(glb));Check(b.ReadUInt32()==0x46546c67 && b.ReadUInt32()==2 && b.ReadUInt32()==glb.Length,"Invalid GLB.");
        int length=b.ReadInt32();Check(b.ReadUInt32()==0x4e4f534a && length>0 && length<glb.Length-20,"Invalid GLB JSON.");
        var doc=JsonNode.Parse(Encoding.UTF8.GetString(b.ReadBytes(length)))!;
        foreach(var buffer in doc["buffers"]!.AsArray())Check(buffer?["uri"]==null,"External GLB buffers are unsupported.");
        if(doc["images"] is JsonArray images)foreach(var image in images)Check(image?["uri"]==null,"External images are unsupported.");
        return(m,glb);
    }
    public static TanukiMapRoot Import(string path,bool fork=false)
    {
        var(m,glb)=Read(path);var state=new GltfState();var document=new GltfDocument();
        Check(document.AppendFromBuffer(glb,"",state)==Error.Ok,"Godot could not decode this map.");
        foreach(var material in state.GetMaterials().OfType<StandardMaterial3D>())
            if(material.Transparency==BaseMaterial3D.TransparencyEnum.AlphaDepthPrePass)material.Transparency=BaseMaterial3D.TransparencyEnum.Alpha;
        var root=new TanukiMapRoot{Name=System.IO.Path.GetFileNameWithoutExtension(path),MapId=(string)m["mapId"]!};root.SetLightingKey((string?)m["baseLighting"]??"cartoon-street-day");
        if(m["baseSun"] is JsonObject sun){root.UpdatingSun=true;try{root.OverrideSun=true;root.SunRotationDegrees=(ReadQ(sun["rotation"]!)*new Quaternion(Vector3.Up,Mathf.Pi)).GetEuler()*180/Mathf.Pi;root.SunIntensity=(float)sun["intensity"]!;root.SunColor=new Color(F(sun["color"]!,0),F(sun["color"]!,1),F(sun["color"]!,2)).LinearToSrgb();root.SunShadows=(bool)sun["shadows"]!;}finally{root.UpdatingSun=false;}}
        if(m["authoring"]?["nodes"] is JsonArray names && names.Count>0)root.Name=(string)names[0]!["name"]!;
        if(fork)root.MapId="map-"+Guid.NewGuid().ToString("N");
        try
        {
            var bindingNodes=new Dictionary<string,int>();var frames=new List<Transform3D>();var labels=new List<string>();
            if(m["authoring"] is JsonObject authored)
            {
                foreach(var record in authored["nodes"]!.AsArray())
                {var p=record!["position"]!;var q=record["rotation"]!;var s=record["scale"]!;var t=new Transform3D(new Basis(new Quaternion(q["x"]!.GetValue<float>(),-q["y"]!.GetValue<float>(),-q["z"]!.GetValue<float>(),q["w"]!.GetValue<float>())).Scaled(new Vector3(s["x"]!.GetValue<float>(),s["y"]!.GetValue<float>(),s["z"]!.GetValue<float>())),new Vector3(-p["x"]!.GetValue<float>(),p["y"]!.GetValue<float>(),p["z"]!.GetValue<float>()));int parent=record["parent"]!.GetValue<int>();Check(parent<frames.Count && parent>=-1,"Invalid authoring parent.");frames.Add(parent<0?Transform3D.Identity:frames[parent]*t);labels.Add((string)record["name"]!);}
                foreach(var binding in authored["bindings"]!.AsArray()){int index=binding!["node"]!.GetValue<int>();Check(index>=0&&index<frames.Count,"Invalid authoring binding.");bindingNodes.Add((string)binding["id"]!,index);}
            }
            var owners=new Dictionary<int,Node3D>();
            var collisionOwners=m["colliders"]!.AsArray().Select(c=>(string)c!["id"]!).Concat(m["meshes"]!.AsArray().Where(e=>e!["collision"]!.GetValue<bool>()).Select(e=>(string)e!["id"]!)).Where(bindingNodes.ContainsKey).Select(id=>bindingNodes[id]).ToHashSet();
            Node3D Owner(string id,string label,Vector3 center)
            {
                if(bindingNodes.TryGetValue(id,out int index))
                {if(owners.TryGetValue(index,out var existing))return existing;var t=frames[index];t.Basis=t.Basis.Orthonormalized();Node3D node=collisionOwners.Contains(index)?new StaticBody3D():new Node3D();node.Name=labels[index];node.Transform=t;Own(root,node);owners.Add(index,node);return node;}
                var standalone=new Node3D{Name=label,Position=center};Own(root,standalone);return standalone;
            }
            void Collision(Node3D owner,string name,Shape3D shape,Transform3D world)
            {var local=Relative(owner,root).AffineInverse()*world;if(owner is StaticBody3D){var collider=new CollisionShape3D{Name=name,Shape=shape,Transform=local};owner.AddChild(collider);collider.Owner=root;}else{var body=new StaticBody3D{Name=name,Transform=local};owner.AddChild(body);body.Owner=root;var collider=new CollisionShape3D{Name="Collision",Shape=shape};body.AddChild(collider);collider.Owner=root;}}
            foreach(var e in m["meshes"]!.AsArray())
            {
                int index=e!["mesh"]!.GetValue<int>();Check(index>=0 && index<state.GetMeshes().Count,"Invalid mesh index.");
                var mesh=state.GetMeshes()[index].Mesh.GetMesh();
                string id=(string)e["id"]!;
                var owner=Owner(id,state.GetMeshes()[index].ResourceName.Length>0?state.GetMeshes()[index].ResourceName:id,mesh.GetAabb().GetCenter());
                if(e["visible"]!.GetValue<bool>())
                {var visual=new MeshInstance3D{Name="Geometry",Mesh=Bake(mesh,Relative(owner,root).AffineInverse())};
                 for(int s=0;s<visual.Mesh.GetSurfaceCount();s++)if(visual.Mesh.SurfaceGetMaterial(s) is StandardMaterial3D material)
                     visual.SetSurfaceOverrideMaterial(s,TanukiShaders.ForAuthoring(material));
                 owner.AddChild(visual);visual.Owner=root;}
                if(e["collision"]!.GetValue<bool>())Collision(owner,"Collision",mesh.CreateTrimeshShape(),Transform3D.Identity);
            }
            foreach(var c in m["colliders"]!.AsArray())
            {
                string kind=(string)c!["type"]!;Shape3D shape=kind switch{
                    "box"=>new BoxShape3D{Size=ReadSize(c["size"]!)},
                    "sphere"=>new SphereShape3D{Radius=c["radius"]!.GetValue<float>()*.01f},
                    "capsule"=>new CapsuleShape3D{Radius=c["radius"]!.GetValue<float>()*.01f,Height=c["halfHeight"]!.GetValue<float>()*.02f},
                    _=>throw new InvalidDataException("Unsupported collision shape: "+kind)};
                var owner=Owner((string)c["id"]!,kind+" collider",ReadV(c["position"]!));Collision(owner,"Collision",shape,new Transform3D(new Basis(ReadQ(c["rotation"]!)),ReadV(c["position"]!)));
            }
            if(m["lights"] is JsonArray lights)
            {
                Check(lights.Count<=256 && (lights.Count==0 || m["schemaVersion"]!.GetValue<int>()>=3),"Invalid light count or schema.");
                foreach(var value in lights)
                {
                    var l=value!.AsObject();string kind=(string)l["type"]!;Check(kind is "point" or "spot" or "directional","Unsupported light type.");var c=l["color"]!.AsArray();Check(c.Count==3,"Invalid light color.");
                    var light=new TanukiMapLight{Name=(string)l["id"]!,Shape=kind=="point"?TanukiMapLight.LightShape.Point:kind=="spot"?TanukiMapLight.LightShape.Spot:TanukiMapLight.LightShape.Directional,
                        Color=new Color(F(c,0),F(c,1),F(c,2)).LinearToSrgb(),Intensity=l["intensity"]!.GetValue<float>(),Range=l["range"]!.GetValue<float>()*.01f,InnerCone=l["innerCone"]!.GetValue<float>(),OuterCone=l["outerCone"]!.GetValue<float>(),Shadows=l["shadows"]!.GetValue<bool>(),Transform=new Transform3D(new Basis(ReadQ(l["rotation"]!)),ReadV(l["position"]!))};
                    light.Validate();Own(root,light);
                }
            }
            foreach(var p in m["markers"]!.AsArray())
            {Check((string)p!["type"]! == "playerStart","Unsupported gameplay marker.");Own(root,new TanukiPlayerStart{Name=(string)p["id"]!,SpawnTag=(string)p["spawnTag"]!??"",Transform=new Transform3D(new Basis(ReadQ(p["rotation"]!)),ReadV(p["position"]!))});}
            if(m["boundary"] is JsonObject boundary)Own(root,new TanukiMapBoundary{Name="Play Boundary",Shape=(string)boundary["shape"]! == "cylinder"?TanukiMapBoundary.BoundaryShape.Cylinder:TanukiMapBoundary.BoundaryShape.Box,Transform=new Transform3D(new Basis(ReadQ(boundary["rotation"]!)).Scaled(ReadSize(boundary["size"]!)),ReadV(boundary["position"]!))});
            else Own(root,new TanukiMapBoundary{Name="Play Boundary",Position=new Vector3(0,4,0),Scale=new Vector3(20,8,20)});
            return root;
        }
        catch{root.Free();throw;}
    }
    public static void Own(Node root,Node child){root.AddChild(child,true);child.Owner=root;}
    static void AddCollision(Node root,string name,Shape3D shape,Transform3D frame)
    {var body=new StaticBody3D{Name=name,Transform=frame};Own(root,body);var collider=new CollisionShape3D{Shape=shape,Name="Collision"};body.AddChild(collider);collider.Owner=root;}
    static Transform3D Relative(Node3D node,Node3D root)
    {var frame=node.Transform;for(var p=node.GetParent();p!=root;p=p.GetParent()){Check(p is Node3D,"All map objects must be under the Map Root.");frame=((Node3D)p).Transform*frame;}return frame;}
    static ArrayMesh Bake(Mesh source,Transform3D frame,MeshInstance3D instance=null)
    {
        Check(Mathf.Abs(frame.Basis.Determinant())>.000001f,"Mesh has a zero scale.");var mesh=new ArrayMesh();
        for(int s=0;s<source.GetSurfaceCount();s++)
        {
            Check(source is not ArrayMesh amSource || amSource.SurfaceGetPrimitiveType(s)==Mesh.PrimitiveType.Triangles,"Only static triangle geometry is supported.");
            var arrays=source.SurfaceGetArrays(s);var vertices=arrays[(int)Mesh.ArrayType.Vertex].AsVector3Array();
            for(int i=0;i<vertices.Length;i++)vertices[i]=frame*vertices[i];arrays[(int)Mesh.ArrayType.Vertex]=vertices;
            var normals=arrays[(int)Mesh.ArrayType.Normal].AsVector3Array();if(normals.Length>0){for(int i=0;i<normals.Length;i++)normals[i]=(frame.Basis.Inverse().Transposed()*normals[i]).Normalized();arrays[(int)Mesh.ArrayType.Normal]=normals;}
            // Importers regenerate tangents from the final baked geometry and UVs.
            arrays[(int)Mesh.ArrayType.Tangent]=default;
            // Mirrored transforms must reverse triangle winding after baking.
            if(frame.Basis.Determinant()<0){var indices=arrays[(int)Mesh.ArrayType.Index].AsInt32Array();if(indices.Length==0)indices=Enumerable.Range(0,vertices.Length).ToArray();for(int i=0;i<indices.Length;i+=3)(indices[i],indices[i+2])=(indices[i+2],indices[i]);arrays[(int)Mesh.ArrayType.Index]=indices;}
            mesh.AddSurfaceFromArrays(Mesh.PrimitiveType.Triangles,arrays);
            var material=instance?.GetActiveMaterial(s)??source.SurfaceGetMaterial(s);
            TanukiMaterials.Validate(material,(instance?.Name.ToString()??source.ResourceName)+" / material slot "+s);
            mesh.SurfaceSetMaterial(s,material is ShaderMaterial?TanukiShaders.ToStandard(material):material??new StandardMaterial3D());
        }
        return mesh;
    }
    public static void Export(TanukiMapRoot root,string path)
    {
        Check(root!=null && root.Scale.IsEqualApprox(Vector3.One),"Select a Map Root with unit scale.");
        Check(!string.IsNullOrWhiteSpace(root.MapId) && root.MapId.Length<=128,"Invalid Map ID.");
        var nodes=Descendants(root).ToArray();Check(!nodes.OfType<TanukiMapRoot>().Any(),"Nested Map Roots are unsupported.");
        Check(!nodes.Any(n=>n is RigidBody3D || n is CsgShape3D || n is AnimationPlayer),"Bake dynamic, CSG, and animated content to static meshes first.");
        var bounds=nodes.OfType<TanukiMapBoundary>().ToArray();Check(bounds.Length==1,"A map needs exactly one Play Boundary.");
        var starts=nodes.OfType<TanukiPlayerStart>().ToArray();Check(starts.Length>=4,"A map needs at least four Player Starts.");
        var boundary=bounds[0];var bt=Relative(boundary,root);var size=bt.Basis.Scale;
        Check(size.X>.01 && size.Y>.01 && size.Z>.01 && bt.Basis.Determinant()>0,"Boundary dimensions must be positive.");
        Check(bt.Basis.Orthonormalized().Scaled(size).IsEqualApprox(bt.Basis),"Boundary cannot be sheared.");
        Check(boundary.Shape!=TanukiMapBoundary.BoundaryShape.Cylinder || Mathf.IsEqualApprox(size.X,size.Z),"Cylinder boundary X and Z dimensions must match.");
        var br=new JsonObject{["id"]="boundary",["shape"]=boundary.Shape==TanukiMapBoundary.BoundaryShape.Box?"box":"cylinder",["size"]=Size(size)};Frame(br,bt);
        var markers=new JsonArray();foreach(var start in starts){var r=new JsonObject{["id"]=start.Name.ToString(),["type"]="playerStart",["spawnTag"]=start.SpawnTag};Frame(r,Relative(start,root));markers.Add(r);}
        Check(!nodes.OfType<Light3D>().Any(),"Use TanukiMapLight elements for portable lights; ordinary scene lights are preview-only outside the map root.");
        var portableLights=nodes.OfType<TanukiMapLight>().ToArray();Check(portableLights.Length<=256,"Portable maps support up to 256 lights.");
        var lights=new JsonArray();foreach(var light in portableLights){var l=light.Settings("light-"+lights.Count);Frame(l,Relative(light,root));lights.Add(l);}
        var staging=new Node3D{Name="Geometry"};var entries=new List<(string Name,Node3D Owner,bool Visible,bool Collision)>();int serial=0;
        Node3D AuthoringOwner(Node3D node)=>node.GetParent() is StaticBody3D body && body.GetChildren().OfType<MeshInstance3D>().Count()<=1?body:node;
        try
        {
            foreach(var node in nodes.OfType<MeshInstance3D>().Where(n=>n.Mesh!=null && n.Visible))
            {Check(node.MaterialOverlay==null,"Material overlays are outside the portable contract: "+node.Name+". Use Repair Materials.");string name="mesh_"+serial++;staging.AddChild(new MeshInstance3D{Name=name,Mesh=Bake(node.Mesh,Relative(node,root),node)});entries.Add((name,AuthoringOwner(node),true,false));}
            foreach(var collider in nodes.OfType<CollisionShape3D>().Where(n=>!n.Disabled))
            {
                Check(collider.GetParent() is StaticBody3D,"Use StaticBody3D for map collision.");
                Check(collider.Shape!=null,"Missing collision shape.");
                // Bake all supported shapes to triangles; this retains rotated/scaled collision exactly.
                Mesh shapeMesh;string name="collision_"+serial++;
                if(collider.Shape is BoxShape3D box)shapeMesh=new BoxMesh{Size=box.Size};
                else if(collider.Shape is SphereShape3D sphere)shapeMesh=new SphereMesh{Radius=sphere.Radius,Height=sphere.Radius*2};
                else if(collider.Shape is CapsuleShape3D capsule)shapeMesh=new CapsuleMesh{Radius=capsule.Radius,Height=capsule.Height};
                else if(collider.Shape is ConcavePolygonShape3D concave){var a=new GArray();a.Resize((int)Mesh.ArrayType.Max);a[(int)Mesh.ArrayType.Vertex]=concave.Data;var am=new ArrayMesh();am.AddSurfaceFromArrays(Mesh.PrimitiveType.Triangles,a);shapeMesh=am;}
                else throw new InvalidDataException("Unsupported collision shape: "+collider.Shape.GetClass());
                staging.AddChild(new MeshInstance3D{Name=name,Mesh=Bake(shapeMesh,Relative(collider,root))});entries.Add((name,AuthoringOwner(collider),false,true));
            }
            Check(entries.Any(e=>e.Visible),"Add static geometry before exporting.");Check(entries.Count<=10000,"Map exceeds 10,000 mesh entries.");
            var doc=new GltfDocument();var state=new GltfState();Check(doc.AppendFromScene(staging,state)==Error.Ok,"Could not prepare GLB.");
            byte[] glb=CanonicalizeGlb(doc.GenerateBuffer(state),state);Check(glb.Length<=256*1024*1024,"Geometry exceeds 256 MiB.");
            var meshes=new JsonArray();foreach(var entry in entries){var gltf=state.GetNodes().Single(n=>n.ResourceName==entry.Name);meshes.Add(new JsonObject{["id"]=entry.Name,["mesh"]=gltf.Mesh,["visible"]=entry.Visible,["collision"]=entry.Collision});}
            var authorNodes=new JsonArray();var bindings=new JsonArray();
            JsonObject NodeRecord(string name,Transform3D frame,int parent)
            {var p=frame.Origin;var s=frame.Basis.Scale;var q=frame.Basis.Orthonormalized().GetRotationQuaternion();return new JsonObject{["name"]=name,["parent"]=parent,["position"]=new JsonObject{["x"]=-p.X,["y"]=p.Y,["z"]=p.Z},["rotation"]=new JsonObject{["x"]=q.X,["y"]=-q.Y,["z"]=-q.Z,["w"]=q.W},["scale"]=new JsonObject{["x"]=s.X,["y"]=s.Y,["z"]=s.Z}};}
            authorNodes.Add(NodeRecord(root.Name,Transform3D.Identity,-1));
            var authoredOwners=new Dictionary<Node3D,int>();
            void Bind(string id,Node3D owner){if(!authoredOwners.TryGetValue(owner,out int node)){node=authorNodes.Count;var frame=Relative(owner,root);frame.Basis=frame.Basis.Orthonormalized();if(owner==boundary)frame=bt;authorNodes.Add(NodeRecord(owner.Name,frame,0));authoredOwners.Add(owner,node);}bindings.Add(new JsonObject{["id"]=id,["node"]=node});}
            foreach(var entry in entries)Bind(entry.Name,entry.Owner);
            foreach(var start in starts)Bind(start.Name,start);Bind("boundary",boundary);
            for(int i=0;i<portableLights.Length;i++)Bind("light-"+i,portableLights[i]);
            var authoring=new JsonObject{["version"]=1,["nodes"]=authorNodes,["bindings"]=bindings};
            var m=new JsonObject{["schemaVersion"]=5,["baseLighting"]=root.LightingKey,["mapId"]=root.MapId,["sceneSha1"]=Convert.ToHexString(SHA1.HashData(glb)).ToLowerInvariant(),["meshes"]=meshes,["colliders"]=new JsonArray(),["markers"]=markers,["boundary"]=br,["authoring"]=authoring,["lights"]=lights};
            if(root.OverrideSun){var c=root.SunColor.SrgbToLinear();var sun=new JsonObject{["rotation"]=Q(Quaternion.FromEuler(root.SunRotationDegrees*Mathf.Pi/180)*new Quaternion(Vector3.Up,-Mathf.Pi)),["color"]=new JsonArray(c.R,c.G,c.B),["intensity"]=root.SunIntensity,["shadows"]=root.SunShadows};ValidateSun(sun);m["baseSun"]=sun;}
            byte[] json=Encoding.UTF8.GetBytes(m.ToJsonString());Check(json.Length<=8*1024*1024,"Manifest exceeds 8 MiB.");
            path=System.IO.Path.GetFullPath(path);Directory.CreateDirectory(System.IO.Path.GetDirectoryName(path)!);string temp=path+"."+Guid.NewGuid().ToString("N")+".tmp";
            try{using(var w=new BinaryWriter(File.Create(temp))){w.Write(0x50414d54);w.Write(1);w.Write(json.Length);w.Write(glb.Length);w.Write(json);w.Write(glb);}File.Move(temp,path,true);}finally{if(File.Exists(temp))File.Delete(temp);}
        }
        finally{staging.Free();}
    }
    static byte[] CanonicalizeGlb(byte[] bytes,GltfState state)
    {
        using var reader=new BinaryReader(new MemoryStream(bytes));reader.ReadBytes(12);int oldLength=reader.ReadInt32();reader.ReadInt32();
        var doc=JsonNode.Parse(Encoding.UTF8.GetString(reader.ReadBytes(oldLength)))!.AsObject();
        // Godot's scene-root hint has no meaning in a baked portable map.
        if(doc["extensionsUsed"] is JsonArray used)for(int i=used.Count-1;i>=0;i--)if((string)used[i]=="GODOT_single_root")used.RemoveAt(i);
        if(doc["extensions"] is JsonObject extensions)extensions.Remove("GODOT_single_root");
        if(doc["materials"] is JsonArray materials)for(int index=0;index<materials.Count;index++)
        {
            var material=materials[index];
            string mode=(string)material!["alphaMode"]??"OPAQUE";
            bool emission=material["emissiveFactor"] is JsonArray factor&&factor.Any(v=>v!.GetValue<float>()>0);
            material["extras"]=new JsonObject{["tanukiProfile"]=mode=="BLEND"?"Transparent":mode=="MASK"?"Cutout":emission?"Emissive":"PBR"};
            // Godot applies UV1 to every StandardMaterial texture, but its GLB
            // writer omits that transform on normal/AO/emission references.
            // Write the shared transform explicitly on every exported map.
            var source=(BaseMaterial3D)state.GetMaterials()[index];var scale=source.Uv1Scale;var offset=source.Uv1Offset;
            foreach(var reference in new[]{material["pbrMetallicRoughness"]?["baseColorTexture"],material["pbrMetallicRoughness"]?["metallicRoughnessTexture"],material["normalTexture"],material["occlusionTexture"],material["emissiveTexture"]}.OfType<JsonObject>())
            {
                if(Mathf.IsEqualApprox(scale.X,1)&&Mathf.IsEqualApprox(scale.Y,1)&&Mathf.IsZeroApprox(offset.X)&&Mathf.IsZeroApprox(offset.Y))continue;
                if(reference["extensions"] is not JsonObject textureExtensions){textureExtensions=new JsonObject();reference["extensions"]=textureExtensions;}
                textureExtensions["KHR_texture_transform"]=new JsonObject{["scale"]=new JsonArray(scale.X,scale.Y),["offset"]=new JsonArray(offset.X,offset.Y)};
                if(doc["extensionsUsed"] is not JsonArray declared){declared=new JsonArray();doc["extensionsUsed"]=declared;}
                if(!declared.Any(e=>(string)e=="KHR_texture_transform"))declared.Add("KHR_texture_transform");
            }
        }
        byte[] json=Encoding.UTF8.GetBytes(doc.ToJsonString());int padded=(json.Length+3)&~3;int tail=20+oldLength;
        using var stream=new MemoryStream();using(var writer=new BinaryWriter(stream,Encoding.UTF8,true))
        {writer.Write(0x46546c67);writer.Write(2);writer.Write(20+padded+bytes.Length-tail);writer.Write(padded);writer.Write(0x4e4f534a);writer.Write(json);for(int i=json.Length;i<padded;i++)writer.Write((byte)32);writer.Write(bytes,tail,bytes.Length-tail);}
        return stream.ToArray();
    }
}
