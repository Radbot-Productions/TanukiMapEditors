using System;
using System.Collections.Generic;
using UnityEngine;

namespace Radbot.MapAuthoring.Editor
{
    // Optional, engine-neutral authoring extension. Runtime geometry remains authoritative.
    [Serializable] public sealed class PortableMapAuthoringData
    {
        public int version=1;
        public Node[] nodes;
        public Binding[] bindings;
        [Serializable] public sealed class Node
        {
            public string name;
            public int parent;
            public Vector3 position,scale;
            public Quaternion rotation;
        }
        [Serializable] public sealed class Binding
        {
            public string id;
            public int node;
            public Vector3 center,size;
            public float radius,height;
            public int direction;
        }
        public sealed class Capture
        {
            readonly Dictionary<Transform,int> indices=new Dictionary<Transform,int>();
            readonly List<Node> nodes=new List<Node>();
            readonly List<Binding> bindings=new List<Binding>();
            public Capture(Transform root)
            {
                foreach(var t in root.GetComponentsInChildren<Transform>())
                {
                    if(PortablePreviewPlayer.IsPreview(t)) continue;
                    if(nodes.Count>=30000 || t.name.Length>1024) throw new InvalidOperationException("Authoring metadata supports up to 30,000 objects with names up to 1,024 characters.");
                    int depth=0; for(var parent=t; parent!=root; parent=parent.parent) if(++depth>128) throw new InvalidOperationException("Authoring hierarchy exceeds 128 levels.");
                    indices.Add(t,nodes.Count);
                    nodes.Add(new Node {name=t.name,parent=t==root ? -1 : indices[t.parent],
                        position=t==root ? Vector3.zero : t.localPosition,rotation=t==root ? Quaternion.identity : t.localRotation,
                        scale=t==root ? Vector3.one : t.localScale});
                }
            }
            public void Add(string id,Transform t,Collider collider=null)
            {
                var b=new Binding {id=id,node=indices[t]};
                if(collider is BoxCollider box) {b.center=box.center; b.size=box.size;}
                if(collider is SphereCollider sphere) {b.center=sphere.center; b.radius=sphere.radius;}
                if(collider is CapsuleCollider capsule) {b.center=capsule.center; b.radius=capsule.radius; b.height=capsule.height; b.direction=capsule.direction;}
                bindings.Add(b);
            }
            public string Json() => JsonUtility.ToJson(new PortableMapAuthoringData {nodes=nodes.ToArray(),bindings=bindings.ToArray()});
        }
    }
}
