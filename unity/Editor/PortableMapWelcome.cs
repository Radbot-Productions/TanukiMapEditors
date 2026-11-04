using UnityEditor;
using UnityEngine;

namespace Radbot.MapAuthoring.Editor
{
    [InitializeOnLoad]
    static class PortableMapWelcome
    {
        static PortableMapWelcome()
        {
            if (!Application.isBatchMode) EditorApplication.update += OpenFirstTime;
        }

        static void OpenFirstTime()
        {
            if (EditorApplication.isCompiling || EditorApplication.isUpdating || EditorApplication.isPlayingOrWillChangePlaymode) return;
            EditorApplication.update -= OpenFirstTime;
            string key = "Tanuki.MapEditor.Welcome." + Hash128.Compute(Application.dataPath);
            if (EditorPrefs.GetBool(key)) return;
            PortableMapAuthoringWindow.Open();
            EditorPrefs.SetBool(key, true);
        }
    }
}
