using System;
using System.IO;
using UnityEngine;
namespace Radbot.MapAuthoring.Editor
{
    public static class PortableCourtyardSample
    {
        public const string ScenePath="Assets/TanukiBlockoutSamplesV2/Courtyard/Courtyard.unity";
        public static void OpenMenu(){OpenOrCreate();}
        public static PortableMapRoot OpenOrCreate()=>PortableSimpleSamples.OpenOrCreate(false);
        public static void RenderPreview(string filename)=>PortableSimpleSamples.RenderPreview(filename);
        public static void BuildAndExport()
        {
            var args=Environment.GetCommandLineArgs();int i=Array.IndexOf(args,"-tanukiExport");if(i<0)throw new InvalidOperationException("Missing -tanukiExport folder.");
            var root=OpenOrCreate();if(!root)throw new InvalidOperationException("Sample creation cancelled.");
            Physics.SyncTransforms();
            foreach(var start in root.GetComponentsInChildren<PortablePlayerStart>())
                foreach(var hit in Physics.OverlapCapsule(start.transform.position+Vector3.up*.56f,start.transform.position-Vector3.up*.56f,.4f,~0,QueryTriggerInteraction.Ignore))
                    if(hit.transform.IsChildOf(root.transform))throw new InvalidOperationException("Sample start overlaps "+hit.name);
            var data=PortableMapExporter.Prepare(root);data.Write(args[i+1]);RenderPreview(Path.Combine(args[i+1],"courtyard-unity.png"));
            File.WriteAllText(Path.Combine(args[i+1],"success.txt"),$"meshes={data.MeshCount} colliders={data.ColliderCount} starts={data.PlayerStartCount}");
            Debug.Log("TANUKI_COURTYARD_EXPORT_PASSED");
        }
    }
}
