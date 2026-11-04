using System;
using System.Collections.Generic;
using System.IO;
using UnityEditor;
using UnityEngine;

namespace Radbot.MapAuthoring.Editor
{
    [InitializeOnLoad]
    public static class PortableGameplayPreviewEditor
    {
        const string Preference="Tanuki.GameplayPreview.Executable";
        static readonly List<Tanuki.Authoring.PortableGameplayPreview> running=new();
        static PortableGameplayPreviewEditor(){EditorApplication.update+=Poll;AssemblyReloadEvents.beforeAssemblyReload+=StopOwned;EditorApplication.quitting+=StopOwned;}
        public static bool IsBusy=>running.Count>0;
        public static string Status=>IsBusy?running[running.Count-1].Status:"";
        public static bool Play(PortableMapRoot root,out string status)
        {
            try{string exe=EditorPrefs.GetString(Preference);string package=UnityEditor.PackageManager.PackageInfo.FindForAssembly(typeof(PortableGameplayPreviewEditor).Assembly).resolvedPath;string setup=Path.Combine(package,"Editor","Native~","TanukiPreviewSetup.exe");string remembered=root.exportFile;running.Add(new Tanuki.Authoring.PortableGameplayPreview(exe,Path.GetFullPath("Library/TanukiPreview"),file=>{try{PortableMapExporter.ExportFile(root,file);}finally{root.exportFile=remembered;}},setup));status="Preparing Tanuki preview. The player installs automatically on first use.";return true;}
            catch(Exception error){status=error.Message;return false;}
        }
        static void Poll(){for(int i=running.Count-1;i>=0;i--)if(running[i].HasExited){Debug.Log(running[i].Complete());running.RemoveAt(i);}if(IsBusy)foreach(var window in Resources.FindObjectsOfTypeAll<PortableMapAuthoringWindow>())window.Repaint();}
        static void StopOwned(){foreach(var preview in running){preview.Dispose();}running.Clear();}
    }
}
