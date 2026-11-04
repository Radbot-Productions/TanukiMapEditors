using System;
using System.IO;
using UnityEditor;
using UnityEngine;
using Object=UnityEngine.Object;

namespace Radbot.MapAuthoring.Editor
{
    public static class PortableMapThumbnail
    {
        /// <summary>Render only the map's static geometry in an isolated preview scene. No authored scripts are cloned.</summary>
        public static void Capture(PortableMapRoot map,string filename)
        {
            if(!map) throw new InvalidOperationException("Choose a map to generate its Workshop preview.");
            var preview=new PreviewRenderUtility(); Texture2D image=null;
            GameObject geometry=null;
            try
            {
                geometry=CopyGeometry(map.transform,null,true);
                preview.AddSingleGO(geometry);
                var renderers=geometry.GetComponentsInChildren<MeshRenderer>();
                if(renderers.Length==0) throw new InvalidOperationException("The map has no visible geometry for its preview.");
                Bounds bounds=renderers[0].bounds;
                foreach(var renderer in renderers) bounds.Encapsulate(renderer.bounds);
                var camera=preview.camera;
                camera.clearFlags=CameraClearFlags.SolidColor; camera.backgroundColor=new Color(.15f,.19f,.23f);
                preview.cameraFieldOfView=50; camera.fieldOfView=50; camera.aspect=1.6f;
                Quaternion rotation=SceneView.lastActiveSceneView ? SceneView.lastActiveSceneView.rotation : Quaternion.Euler(33,-45,0);
                camera.transform.rotation=rotation;
                float tanY=Mathf.Tan(camera.fieldOfView*Mathf.Deg2Rad*.5f),tanX=tanY*camera.aspect,distance=.1f;
                for(int corner=0;corner<8;++corner)
                {
                    var offset=new Vector3((corner&1)!=0?bounds.extents.x:-bounds.extents.x,(corner&2)!=0?bounds.extents.y:-bounds.extents.y,(corner&4)!=0?bounds.extents.z:-bounds.extents.z);
                    var local=Quaternion.Inverse(rotation)*offset;
                    distance=Mathf.Max(distance,Mathf.Abs(local.x)/tanX-local.z,Mathf.Abs(local.y)/tanY-local.z);
                }
                distance*=1.12f;
                camera.transform.position=bounds.center-camera.transform.forward*distance;
                camera.nearClipPlane=Mathf.Max(.001f,distance*.0001f); camera.farClipPlane=distance+bounds.size.magnitude*2;
                preview.ambientColor=new Color(.45f,.48f,.53f);
                preview.lights[0].intensity=1.2f; preview.lights[0].transform.rotation=Quaternion.Euler(45,-35,0);
                preview.lights[1].intensity=.6f; preview.lights[1].transform.rotation=Quaternion.Euler(20,145,0);
                preview.BeginStaticPreview(new Rect(0,0,640,400)); preview.Render(true,false);
                image=preview.EndStaticPreview();
                byte[] png=image.EncodeToPNG();
                if(png.Length>=1024*1024) throw new InvalidOperationException("Generated preview exceeds Steam's image limit. Choose a smaller custom PNG.");
                Directory.CreateDirectory(Path.GetDirectoryName(filename)); File.WriteAllBytes(filename,png);
            }
            finally
            {
                if(image) Object.DestroyImmediate(image);
                preview.Cleanup();
                if(geometry) Object.DestroyImmediate(geometry);
            }
        }
        static GameObject CopyGeometry(Transform source,Transform parent,bool root)
        {
            if(PortablePreviewPlayer.IsPreview(source)) return null;
            var copy=new GameObject(source.name) { hideFlags=HideFlags.HideAndDontSave };
            copy.transform.SetParent(parent,false);
            if(!root) { copy.transform.localPosition=source.localPosition; copy.transform.localRotation=source.localRotation; copy.transform.localScale=source.localScale; }
            var renderer=source.GetComponent<MeshRenderer>(); var filter=source.GetComponent<MeshFilter>();
            if(renderer && renderer.enabled && filter && filter.sharedMesh)
            { copy.AddComponent<MeshFilter>().sharedMesh=filter.sharedMesh; copy.AddComponent<MeshRenderer>().sharedMaterials=renderer.sharedMaterials; }
            foreach(Transform child in source) if(child.gameObject.activeSelf) CopyGeometry(child,copy.transform,false);
            return copy;
        }
    }
}
