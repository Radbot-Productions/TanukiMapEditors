using System;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using Object=UnityEngine.Object;

namespace Radbot.MapAuthoring.Editor
{
    public static class PortableMapSample
    {
        public const string ScenePath="Assets/TanukiBlockoutSamplesV2/Room/Room.unity";
        public static void Create(){PortableSimpleSamples.OpenOrCreate(true);}

        /// <summary>Batch entry point: Unity -batchmode -projectPath ... -executeMethod ...BuildAndExport -tanukiExport ... -quit</summary>
        public static void BuildAndExport()
        {
            var args=Environment.GetCommandLineArgs(); int index=Array.IndexOf(args,"-tanukiExport");
            if(index<0 || index+1>=args.Length) throw new InvalidOperationException("Missing -tanukiExport directory.");
            const string sample=ScenePath;
            if(AssetDatabase.LoadAssetAtPath<SceneAsset>(sample)) EditorSceneManager.OpenScene(sample);
            else Create();
            var root=Object.FindObjectOfType<PortableMapRoot>();
            PortableMapExporter.Export(root,args[index+1]);
            // Prove unsupported authoring fails instead of silently dropping gameplay collision.
            var box=root.GetComponentInChildren<BoxCollider>(); box.isTrigger=true;
            bool rejected=false;
            try { PortableMapExporter.Export(root,Path.Combine(args[index+1],"invalid-trigger")); }
            catch(InvalidOperationException) { rejected=true; }
            finally { box.isTrigger=false; }
            if(!rejected) throw new InvalidOperationException("Trigger rejection regression.");
            Debug.Log("TANUKI_UNITY_EXPORT_TESTS_PASSED");
        }
    }
}
