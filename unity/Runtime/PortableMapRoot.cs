using UnityEngine;

namespace Radbot.MapAuthoring
{
    [DisallowMultipleComponent]
    [AddComponentMenu("Tanuki/Map Root")]
    public sealed class PortableMapRoot : MonoBehaviour
    {
        [Tooltip("Stable identity of this map. Keep it when publishing a revision.")]
        public string mapId = "my-map";
        [UnityEngine.Serialization.FormerlySerializedAs("mapName"),SerializeField,HideInInspector]
        string legacyMapName="";
        public string mapName {get=>gameObject.name;set=>gameObject.name=value;}
        public string DisplayName => gameObject.name;
        public Texture2D previewImage;
        [HideInInspector] public string workshopItemId = "";
        [HideInInspector] public string sourceFile = "";
        [HideInInspector] public string exportFile = "";

        private void Reset()
        {
            mapId=System.Guid.NewGuid().ToString("N");
            mapName=gameObject.name;
            var boundary=new GameObject("Map Boundary");
            boundary.transform.SetParent(transform,false);
            boundary.transform.localPosition=Vector3.up*9.875f;
            boundary.transform.localScale=new Vector3(60,20.25f,60);
            boundary.AddComponent<PortableMapBoundary>();
#if UNITY_EDITOR
            UnityEditor.Undo.RegisterCreatedObjectUndo(boundary,"Create Map Boundary");
#endif
        }
    }
}
