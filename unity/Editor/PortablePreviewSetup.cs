using UnityEditor;
using UnityEngine;

namespace Radbot.MapAuthoring.Editor
{
    public static class PortablePreviewSetup
    {
        [MenuItem("Tanuki/Components/Player Character",priority=22)]
        static void AddMenu()
        {
            var selected=Selection.activeGameObject?Selection.activeGameObject.GetComponentInParent<PortableMapRoot>():null;
            if(!selected) foreach(var root in UnityEngine.SceneManagement.SceneManager.GetActiveScene().GetRootGameObjects()) {selected=root.GetComponent<PortableMapRoot>();if(selected)break;}
            if(selected) Selection.activeGameObject=Ensure(selected).gameObject;
        }
        static void EnsureReferences(PortableMapRoot map)
        {
            var existing=map.transform.Find("Character Scale References");
            if(existing&&existing.GetComponent<PortableMapPreviewReference>()&&existing.GetComponent<PortableMapPreviewReference>().layoutVersion==3)return;
            if(existing&&existing.GetComponent<PortableMapPreviewReference>())Undo.DestroyObjectImmediate(existing.gameObject);
            var group=new GameObject("Character Scale References");group.transform.SetParent(map.transform,false);group.AddComponent<PortableMapPreviewReference>();
            group.GetComponent<PortableMapPreviewReference>().layoutVersion=3;
            Bounds floor=new Bounds(new Vector3(0,-.1f,0),new Vector3(20,.2f,20));float area=0;
            foreach(var mesh in map.GetComponentsInChildren<MeshFilter>(true))
            {
                if(!mesh.sharedMesh||mesh.GetComponentInParent<PortableMapPreviewReference>())continue;
                var box=mesh.sharedMesh.bounds;var points=new Bounds(map.transform.InverseTransformPoint(mesh.transform.TransformPoint(box.min)),Vector3.zero);
                for(int corner=0;corner<8;corner++)points.Encapsulate(map.transform.InverseTransformPoint(mesh.transform.TransformPoint(box.center+Vector3.Scale(box.extents,new Vector3((corner&1)==0?-1:1,(corner&2)==0?-1:1,(corner&4)==0?-1:1)))));
                if(points.size.y<Mathf.Min(points.size.x,points.size.z)*.3f&&points.size.x*points.size.z>area){floor=points;area=points.size.x*points.size.z;}
            }
            string[] names={"Tanuki","Tengu"};
            for(int i=0;i<names.Length;i++)
            {
                var prefab=AssetDatabase.LoadAssetAtPath<GameObject>("Packages/com.radbot.tanuki-map-authoring/Runtime/References/"+names[i]+".fbx");
                if(!prefab)throw new System.InvalidOperationException("Missing character scale reference: "+names[i]);
                var copy=(GameObject)PrefabUtility.InstantiatePrefab(prefab,group.transform);copy.name=names[i]+" (scale reference)";copy.transform.localPosition=new Vector3(floor.center.x+(i==0?-1:1)*Mathf.Min(1.5f,floor.size.x*.2f),floor.max.y,floor.min.z+Mathf.Min(1.5f,floor.size.z*.15f));
                foreach(var animator in copy.GetComponentsInChildren<Animator>())animator.enabled=false;
            }
        }
        public static PortablePreviewPlayer Ensure(PortableMapRoot map)
        {
            var lighting=map.GetComponent<PortableMapBaseLighting>()??map.gameObject.AddComponent<PortableMapBaseLighting>();lighting.Refresh();
            if(!map.GetComponent<PortableMapSkybox>())map.gameObject.AddComponent<PortableMapSkybox>();
            EnsureReferences(map);
            foreach(var root in map.gameObject.scene.GetRootGameObjects())
                foreach(var player in root.GetComponentsInChildren<PortablePreviewPlayer>(true)) if(player.map==map) return player;
            var go=new GameObject("Player Character");UnityEngine.SceneManagement.SceneManager.MoveGameObjectToScene(go,map.gameObject.scene);
            var result=go.AddComponent<PortablePreviewPlayer>();result.map=map;
            var start=map.GetComponentInChildren<PortablePlayerStart>();go.transform.SetPositionAndRotation(start?start.transform.position:map.transform.position+Vector3.up,start?start.transform.rotation:map.transform.rotation);
            Undo.RegisterCreatedObjectUndo(go,"Create Player Character");return result;
        }
    }
}
