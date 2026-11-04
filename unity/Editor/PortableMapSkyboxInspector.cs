using UnityEditor;
using UnityEngine;
namespace Radbot.MapAuthoring.Editor
{
    [CustomEditor(typeof(PortableMapSkybox))]
    public sealed class PortableMapSkyboxInspector : UnityEditor.Editor
    {
        public override void OnInspectorGUI()
        {
            var lighting=((PortableMapSkybox)target).GetComponent<PortableMapBaseLighting>();
            var settings=new SerializedObject(lighting);settings.Update();
            EditorGUILayout.PropertyField(settings.FindProperty("profile"),new GUIContent("Sky Preset"));settings.ApplyModifiedProperties();
            EditorGUILayout.HelpBox("Skybox and Base Lighting share this preset. The local sky uses a gradient, sun disk and static clouds; Play in Tanuki uses the original game sky.",MessageType.Info);
        }
    }
}
