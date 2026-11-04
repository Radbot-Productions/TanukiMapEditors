using System;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering;
using Object=UnityEngine.Object;

namespace Radbot.MapAuthoring.Editor
{
    // Fixed primitive blockouts. No decorative meshes, textures or random placement.
    public static class PortableSimpleSamples
    {
        public static PortableMapRoot OpenOrCreate(bool room)
        {
            string path=room?PortableMapSample.ScenePath:PortableCourtyardSample.ScenePath;
            if(!EditorSceneManager.SaveCurrentModifiedScenesIfUserWantsTo()) return null;
            if(AssetDatabase.LoadAssetAtPath<SceneAsset>(path))
            {
                EditorSceneManager.OpenScene(path);
                var existing=Object.FindObjectOfType<PortableMapRoot>();
                PortablePreviewSetup.Ensure(existing);
                Selection.activeGameObject=existing.gameObject;return existing;
            }
            string folder=Path.GetDirectoryName(path).Replace('\\','/');
            if(Directory.Exists(folder)) throw new InvalidOperationException("Sample folder already exists without its scene: "+folder);
            Directory.CreateDirectory(folder);AssetDatabase.Refresh();
            EditorSceneManager.NewScene(NewSceneSetup.EmptyScene,NewSceneMode.Single);
            Func<string,Color,Material> material=(name,color)=>
            {
                var result=PortableMapMaterials.Create(PortableMapMaterials.Profile.PBR);result.name=name;result.color=color;
                result.SetFloat("_Roughness",1);
                AssetDatabase.CreateAsset(result,folder+"/"+name+".mat");return result;
            };
            var floorMaterial=material("Floor",new Color(.55f,.55f,.55f));
            var objectMaterial=material("Objects",new Color(.4f,.55f,.7f));
            var rampMaterial=material("Ramp",new Color(.7f,.55f,.35f));
            var root=new GameObject(room?"Room":"Courtyard").AddComponent<PortableMapRoot>();
            root.mapId=room?"simple-room-v2":"simple-courtyard-v2";
            Func<string,Vector3,Vector3,Material,GameObject> box=(name,position,size,mat)=>
            {
                var go=GameObject.CreatePrimitive(PrimitiveType.Cube);go.name=name;go.transform.SetParent(root.transform,false);
                go.transform.localPosition=position;go.transform.localScale=size;go.GetComponent<Renderer>().sharedMaterial=mat;return go;
            };
            float side=room?12:20;
            box("Floor",new Vector3(0,-.1f,0),new Vector3(side,.2f,side),floorMaterial);
            if(room)
            {
                box("Back Wall",new Vector3(0,1.25f,5.9f),new Vector3(12,2.5f,.2f),floorMaterial);
                box("Left Wall",new Vector3(-5.9f,1.25f,0),new Vector3(.2f,2.5f,12),floorMaterial);
                box("Right Wall",new Vector3(5.9f,1.25f,0),new Vector3(.2f,2.5f,12),floorMaterial);
            }
            box("Cube",new Vector3(-3,.5f,2),Vector3.one,objectMaterial);
            box("Low Block",new Vector3(2,.3f,-1),new Vector3(2,.6f,1.5f),objectMaterial);
            box("Tall Block",new Vector3(4,1.5f,3),new Vector3(1,3,1),objectMaterial);
            var ramp=box("Ramp",new Vector3(-1,.45f,1),new Vector3(2,.25f,3),rampMaterial);ramp.transform.localRotation=Quaternion.Euler(15,0,0);
            if(!room)
            {
                box("Platform",new Vector3(1,.8f,6),new Vector3(3,1.6f,2),objectMaterial);
                box("Rotated Block",new Vector3(-6,.65f,-3),new Vector3(2,1.3f,1),objectMaterial).transform.localRotation=Quaternion.Euler(0,25,0);
                var cylinder=GameObject.CreatePrimitive(PrimitiveType.Cylinder);cylinder.name="Cylinder";cylinder.transform.SetParent(root.transform,false);
                cylinder.transform.localPosition=new Vector3(-5,.75f,5);cylinder.transform.localScale=new Vector3(1.5f,.75f,1.5f);
                cylinder.GetComponent<Renderer>().sharedMaterial=objectMaterial;
                Object.DestroyImmediate(cylinder.GetComponent<Collider>());cylinder.AddComponent<MeshCollider>().sharedMesh=cylinder.GetComponent<MeshFilter>().sharedMesh;
            }
            var boundary=root.GetComponentInChildren<PortableMapBoundary>();boundary.transform.localPosition=Vector3.up*3.875f;boundary.transform.localScale=new Vector3(side,8.25f,side);
            foreach(var p in new[]{new Vector3(-2,1,-4),new Vector3(2,1,-4),new Vector3(-4,1,0),new Vector3(4,1,0)})
            {var go=new GameObject("Player Start");go.transform.SetParent(root.transform,false);go.transform.localPosition=p;go.AddComponent<PortablePlayerStart>();}
            var sun=new GameObject("Preview Light").AddComponent<Light>();sun.type=LightType.Directional;sun.intensity=1;sun.transform.rotation=Quaternion.Euler(50,-30,0);
            RenderSettings.ambientMode=AmbientMode.Flat;RenderSettings.ambientLight=new Color(.65f,.65f,.65f);RenderSettings.fog=false;
            var camera=new GameObject("Preview Camera").AddComponent<Camera>();camera.tag="MainCamera";
            camera.transform.position=new Vector3(side,side*.8f,-side);camera.transform.LookAt(Vector3.up);
            camera.orthographic=true;camera.orthographicSize=side*.72f;camera.clearFlags=CameraClearFlags.SolidColor;camera.backgroundColor=new Color(.22f,.22f,.22f);
            PortablePreviewSetup.Ensure(root);EditorSceneManager.SaveScene(root.gameObject.scene,path);Selection.activeGameObject=root.gameObject;
            PortableMapAuthoringWindow.Open().SelectMap(root);return root;
        }
        public static void RenderPreview(string filename)
        {
            var camera=Camera.main;if(!camera) throw new InvalidOperationException("Sample preview camera is missing.");
            var target=new RenderTexture(960,640,24);var previous=RenderTexture.active;var oldTarget=camera.targetTexture;Texture2D image=null;
            try {camera.targetTexture=target;camera.Render();RenderTexture.active=target;image=new Texture2D(960,640,TextureFormat.RGB24,false);image.ReadPixels(new Rect(0,0,960,640),0,0);image.Apply();Directory.CreateDirectory(Path.GetDirectoryName(filename));File.WriteAllBytes(filename,image.EncodeToPNG());}
            finally {camera.targetTexture=oldTarget;RenderTexture.active=previous;if(image)Object.DestroyImmediate(image);target.Release();Object.DestroyImmediate(target);}
        }
    }
}
