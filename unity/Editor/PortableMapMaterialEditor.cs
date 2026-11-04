using UnityEditor;
namespace Radbot.MapAuthoring.Editor
{
    // Retains old serialized editor layouts without presenting a custom material editor.
    public sealed class PortableMapMaterialEditor : EditorWindow
    {
        public static void Open(){PortableMapMaterials.CreatePresets();}
        void OnEnable(){EditorApplication.delayCall+=()=>{if(this)Close();};}
    }
}
