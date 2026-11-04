using System;
using System.IO;
using System.Linq;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;
using Object=UnityEngine.Object;

namespace Radbot.MapAuthoring.Editor
{
    public sealed class PortableMapAuthoringWindow : EditorWindow
    {
        enum Page { New, Samples, Open, Workshop, Export, Publish }
        [SerializeField] Page page;
        [SerializeField] PortableMapRoot map;
        [SerializeField] string message="",itemId="";
        [SerializeField] bool hasError;
        [SerializeField] bool createWorkshopItem;
        [SerializeField] string updateItem="";
        Vector2 scroll;
        bool busy,advanced;
        public PortableMapRoot SelectedMap => map;
        public string LastMessage => message;
        public bool HasError => hasError;
        public string PublishingItem => createWorkshopItem?"":updateItem;
        public void ReconcileWorkshopItem(PortableMapRoot root)
        {if(map!=root)return;updateItem=root.workshopItemId;createWorkshopItem=string.IsNullOrEmpty(updateItem);Repaint();}
        public void SetTab(int index) {page=index==0?Page.New:index==1?Page.Open:Page.Export;message="";Repaint();}

        [MenuItem("Tanuki/Map Editor",priority=0)] static void NewMenu() { Open(); }
        [MenuItem("Tanuki/Components/Map Boundary",priority=20)]
        [MenuItem("GameObject/Tanuki/Map Boundary",false,11)] static void BoundaryMenu() { ComponentCommand(true); }
        [MenuItem("Tanuki/Components/Player Start",priority=21)]
        [MenuItem("GameObject/Tanuki/Player Start",false,12)] static void StartMenu() { ComponentCommand(false); }
        [MenuItem("GameObject/Tanuki/Map Root",false,10)] static void RootMenu() { CreateMapRoot(); }

        public static PortableMapAuthoringWindow Open()
        {
            var window=GetWindow<PortableMapAuthoringWindow>("Tanuki Map Editor");
            window.minSize=new Vector2(360,260); window.Show(); return window;
        }
        static PortableMapAuthoringWindow Show(Page value)
        {
            var window=Open(); window.page=value; window.message=""; window.hasError=false; window.FollowSelection();
            if(value==Page.Workshop) PortableMapWorkshopPublisher.BrowseSubscribed();
            return window;
        }
        public static void ShowExport(PortableMapRoot root,bool workshop=false)
        {var window=Show(workshop?Page.Publish:Page.Export);window.SelectMap(root);window.createWorkshopItem=!root || string.IsNullOrEmpty(root.workshopItemId);window.updateItem=root?root.workshopItemId:"";}
        public static void ShowFileExport(PortableMapRoot root)
        {var window=Show(Page.Export);window.SelectMap(root);window.AfterGUI(window.ExportFileDialog);}
        [MenuItem("Tanuki/Components/Skybox",priority=26)]
        static void SkyboxMenu()
        {
            var map=CurrentMap();if(!map)return;
            Selection.activeObject=map.GetComponent<PortableMapSkybox>()??Undo.AddComponent<PortableMapSkybox>(map.gameObject);
        }
        [MenuItem("Tanuki/Components/Lighting Settings",priority=25)]
        static void LightingSettingsMenu()
        {
            var map=CurrentMap();if(!map)return;
            var settings=map.GetComponent<PortableMapBaseLighting>()??Undo.AddComponent<PortableMapBaseLighting>(map.gameObject);
            Selection.activeObject=settings;
        }
        static PortableMapRoot CurrentMap()
        {
            var scene=SceneManager.GetActiveScene();
            var roots=scene.IsValid()?scene.GetRootGameObjects().SelectMany(g=>g.GetComponentsInChildren<PortableMapRoot>(true)).ToArray():Array.Empty<PortableMapRoot>();
            return roots.Length==1?roots[0]:null;
        }
        static void ComponentCommand(bool boundary)
        {
            var root=CurrentMap();
            if(!root) {Show(Page.New).Fail("Select a map in the scene first.");return;}
            if(boundary)
            {
                var existing=root.GetComponentsInChildren<PortableMapBoundary>(true);
                if(existing.Length>0) {Selection.activeGameObject=existing[0].gameObject;return;}
                CreateBoundary(root);
            }
            else AddPlayerStart(root);
        }
        public static PortableMapBoundary CreateBoundary(PortableMapRoot root)
        {
            var existing=root.GetComponentInChildren<PortableMapBoundary>(true);
            if(existing){Selection.activeGameObject=existing.gameObject;return existing;}
            var go=new GameObject("Map Boundary");go.transform.SetParent(root.transform,false);
            go.transform.localPosition=Vector3.up*9.875f;go.transform.localScale=new Vector3(60,20.25f,60);
            var result=go.AddComponent<PortableMapBoundary>();Undo.RegisterCreatedObjectUndo(go,"Create Map Boundary");Selection.activeGameObject=go;return result;
        }
        public static PortableMapRoot CreateMapRoot()
        {
            Undo.IncrementCurrentGroup();int group=Undo.GetCurrentGroup();
            var go=new GameObject("New Map");var root=go.AddComponent<PortableMapRoot>();
            root.mapId=Guid.NewGuid().ToString("N");root.mapName=go.name;
            if(root.GetComponentsInChildren<PortableMapBoundary>(true).Length==0) CreateBoundary(root);
            Undo.RegisterCreatedObjectUndo(go,"Create Tanuki Map");Undo.CollapseUndoOperations(group);
            Selection.activeGameObject=go;
            foreach(var window in Resources.FindObjectsOfTypeAll<PortableMapAuthoringWindow>()) window.SelectMap(root);
            return root;
        }
        [MenuItem("Tanuki/Components/Point Light",priority=22)] static void PointLightMenu()=>AddLight(PortableMapLight.Shape.Point);
        [MenuItem("Tanuki/Components/Spot Light",priority=23)] static void SpotLightMenu()=>AddLight(PortableMapLight.Shape.Spot);
        [MenuItem("Tanuki/Components/Directional Light",priority=24)] static void DirectionalLightMenu()=>AddLight(PortableMapLight.Shape.Directional);
        public static PortableMapLight AddLight(PortableMapLight.Shape shape)
        {
            var root=CurrentMap();if(!root)throw new InvalidOperationException("Select a map first.");
            var go=new GameObject(shape+" Light");go.transform.SetParent(root.transform,false);go.transform.localPosition=Vector3.up*3;
            var light=go.AddComponent<PortableMapLight>();light.shape=shape;light.Refresh();Undo.RegisterCreatedObjectUndo(go,"Add Map Light");Selection.activeGameObject=go;return light;
        }
        public PortablePlayerStart AddPlayerStart() => AddPlayerStart(map);
        public static PortablePlayerStart AddPlayerStart(PortableMapRoot root)
        {
            if(!root) throw new InvalidOperationException("Select a map first.");
            var go=new GameObject("Player Start");go.transform.SetParent(root.transform,false);go.transform.localPosition=Vector3.up;
            var start=go.AddComponent<PortablePlayerStart>();Undo.RegisterCreatedObjectUndo(go,"Add Player Start");Selection.activeGameObject=go;return start;
        }
        public void SelectMap(PortableMapRoot root)
        {
            if(root && (EditorUtility.IsPersistent(root)||!root.gameObject.scene.IsValid())) throw new InvalidOperationException("Select a map in an open scene.");
            map=root;Repaint();
        }
        void OnEnable() {Selection.selectionChanged+=FollowSelection;EditorApplication.hierarchyChanged+=Repaint;FollowSelection();if(map&&!EditorApplication.isPlayingOrWillChangePlaymode)PortablePreviewSetup.Ensure(map);if(!Application.isBatchMode)EditorApplication.delayCall+=()=>{if(this)PortableMapWorkshopPublisher.Connect();};}
        void OnDisable() {Selection.selectionChanged-=FollowSelection;EditorApplication.hierarchyChanged-=Repaint;}
        void FollowSelection() {var current=CurrentMap();if(current && current!=map) SelectMap(current);Repaint();}
        void OnInspectorUpdate() {if(PortableMapWorkshopPublisher.IsPending) Repaint();}
        void Changed(string text) {message=text;hasError=false;Repaint();}
        void Fail(string text) {message=text;hasError=true;Repaint();}
        void AfterGUI(Action action)
        {
            if(busy||PortableMapWorkshopPublisher.IsPending) return;
            // Scene replacement and asset imports can reset IMGUI's layout stack.
            // Let the current GUI event unwind before changing editor state.
            busy=true;
            void Execute()
            {
                EditorApplication.update-=Execute;
                if(!this)return;
                busy=false;action();
            }
            EditorApplication.update+=Execute;
            Repaint();
        }
        bool Run(Action action)
        {
            if(busy||PortableMapWorkshopPublisher.IsPending) return false;
            busy=true;
            try {action();return true;}catch(ExitGUIException) {throw;}catch(Exception error) {Fail(error.Message);return false;}
            finally {busy=false;EditorUtility.ClearProgressBar();Repaint();}
        }
        public bool ValidateMap() => Run(()=>{PortableMapExporter.Prepare(map);Changed("Map is ready to export.");});
        public bool RepairMaterials() => Run(()=>Changed(PortableMapMaterials.Repair(map)));
        public bool ExportMap(string filename) => Run(()=>
        {
            PortableMapExporter.ExportFile(map,filename);
            Undo.RecordObject(map,"Remember Map Export");map.exportFile=Path.GetFullPath(filename);EditorUtility.SetDirty(map);
            Changed("Exported "+map.exportFile);
        });
        public bool OpenMapFile(string filename,string folder,bool fork) => Run(()=>
        {
            var root=PortableMapImporter.Import(filename,folder,fork);
            root.sourceFile=Path.GetFullPath(filename);root.mapName=Path.GetFileNameWithoutExtension(filename);
            EditorUtility.SetDirty(root);SelectMap(root);Selection.activeGameObject=root.gameObject;
        });
        public bool ReplaceMapFile(string filename,string folder,bool fork=false) => Run(()=>
        {
            var root=PortableMapImporter.Import(filename,folder,fork,true);
            root.sourceFile=Path.GetFullPath(filename);root.mapName=Path.GetFileNameWithoutExtension(filename);
            PortablePreviewSetup.Ensure(root);SelectMap(root);Selection.activeGameObject=root.gameObject;EditorUtility.SetDirty(root);
        });
        public static string ImportFolder(string name)
        {
            if(!AssetDatabase.IsValidFolder("Assets/TanukiMaps")) AssetDatabase.CreateFolder("Assets","TanukiMaps");
            string safe=string.Concat((name??"Map").Select(c=>char.IsLetterOrDigit(c)||c==' '||c=='-'||c=='_'?c:'_')).Trim();
            return AssetDatabase.GenerateUniqueAssetPath("Assets/TanukiMaps/"+(safe.Length>0?safe:"Map"));
        }
        public bool CreateEmptyMap() => Run(()=>
        {
            EditorSceneManager.NewScene(NewSceneSetup.EmptyScene,NewSceneMode.Single);var root=CreateMapRoot();
            var boundary=root.GetComponentInChildren<PortableMapBoundary>();boundary.transform.localPosition=Vector3.up*3.875f;boundary.transform.localScale=new Vector3(20,8.25f,20);
            var floor=GameObject.CreatePrimitive(PrimitiveType.Cube);floor.name="Floor";floor.transform.SetParent(root.transform,false);floor.transform.localPosition=new Vector3(0,-.1f,0);floor.transform.localScale=new Vector3(20,.2f,20);
            var material=PortableMapMaterials.Create(PortableMapMaterials.Profile.PBR);material.name="Floor";material.color=new Color(.5f,.55f,.6f);
            Directory.CreateDirectory("Assets/TanukiMaps/Materials");AssetDatabase.Refresh();
            AssetDatabase.CreateAsset(material,AssetDatabase.GenerateUniqueAssetPath("Assets/TanukiMaps/Materials/Floor.mat"));
            floor.GetComponent<Renderer>().sharedMaterial=material;
            for(int i=0;i<4;i++){var start=AddPlayerStart(root);start.transform.localPosition=new Vector3((i-1.5f)*2,1,0);}
            PortablePreviewSetup.Ensure(root);SelectMap(root);Selection.activeGameObject=root.gameObject;
        });
        public bool StartFromSample(bool courtyard) => Run(()=>
        {
            EditorSceneManager.NewScene(NewSceneSetup.EmptyScene,NewSceneMode.Single);
            PortableMapRoot template;
            if(courtyard) template=PortableCourtyardSample.OpenOrCreate();
            else
            {
                const string path=PortableMapSample.ScenePath;
                if(!AssetDatabase.LoadAssetAtPath<SceneAsset>(path)) PortableMapSample.Create();else EditorSceneManager.OpenScene(path);
                template=CurrentMap();
            }
            if(!template) return;
            TanukiMaterialShaders.ConvertMap(template);
            string folder=ImportFolder(courtyard?"Courtyard":"Room");Directory.CreateDirectory(folder);AssetDatabase.Refresh();
            if(!EditorSceneManager.SaveScene(template.gameObject.scene,folder+"/Map.unity")) throw new IOException("Could not create the map scene.");
            template.mapId=Guid.NewGuid().ToString("N");template.mapName=courtyard?"Courtyard":"Room";
            template.workshopItemId="";template.sourceFile="";template.exportFile="";
            if(template.GetComponentsInChildren<PortableMapBoundary>(true).Length==0) CreateBoundary(template);
            int count=template.GetComponentsInChildren<PortablePlayerStart>().Count(s=>s.enabled);
            for(int i=count;i<4;i++) {var start=AddPlayerStart(template);start.transform.localPosition=new Vector3((i-1.5f)*2,1,-3);}
            if(!EditorSceneManager.SaveScene(template.gameObject.scene)) throw new IOException("Could not save the new sample map.");
            PortablePreviewSetup.Ensure(template);SelectMap(template);Selection.activeGameObject=template.gameObject;
        });
        void ExportFileDialog()
        {
            if(!map) {Fail("Select the map root in the scene.");return;}
            string file=EditorUtility.SaveFilePanel("Export Map File",string.IsNullOrEmpty(map.exportFile)?"":Path.GetDirectoryName(map.exportFile),
                string.IsNullOrEmpty(map.exportFile)?map.DisplayName:Path.GetFileNameWithoutExtension(map.exportFile),"tanukimap");
            if(file.Length>0) ExportMap(file);
        }
        void OnGUI()
        {
            var active=CurrentMap();if(active!=map)map=active;
            if(!map)EditorGUILayout.HelpBox("The active scene must contain exactly one Tanuki Map Root. Create or open a map to preview it.",MessageType.Info);
            if(PortableGameplayPreviewEditor.IsBusy)EditorGUILayout.HelpBox(PortableGameplayPreviewEditor.Status,MessageType.Info);
            using(new EditorGUI.DisabledScope(!map||EditorApplication.isPlayingOrWillChangePlaymode))
                if(GUILayout.Button("Play in Tanuki")){PortableGameplayPreviewEditor.Play(map,out message);Repaint();}
            scroll=EditorGUILayout.BeginScrollView(scroll);EditorGUILayout.Space(8);
            using(new EditorGUI.DisabledScope(busy||EditorApplication.isPlayingOrWillChangePlaymode||PortableMapWorkshopPublisher.IsPending))
            {
                int current=page==Page.New||page==Page.Samples?0:page==Page.Open||page==Page.Workshop?1:2;
                int tab=GUILayout.Toolbar(current,new[]{"New","Open","Export"});
                if(tab!=current) SetTab(tab);
                EditorGUILayout.Space(10);
                if(page==Page.New)
                {
                    Heading("New Map");if(Button("Create Empty Map")) AfterGUI(()=>CreateEmptyMap());if(Button("Start From Sample…")) page=Page.Samples;
                }
                else if(page==Page.Samples)
                {
                    Heading("Start From Sample");if(Button("Courtyard")) AfterGUI(()=>StartFromSample(true));if(Button("Room")) AfterGUI(()=>StartFromSample(false));
                }
                else if(page==Page.Open)
                {
                    Heading("Open Map");
                    if(Button("Steam Workshop…")) {page=Page.Workshop;PortableMapWorkshopPublisher.BrowseSubscribed();}
                    if(Button("Map File…")) AfterGUI(()=>{string file=EditorUtility.OpenFilePanel("Open Map File","","tanukimap");if(file.Length>0) ReplaceMapFile(file,ImportFolder(Path.GetFileNameWithoutExtension(file)));});
                }
                else if(page==Page.Workshop)
                {
                    Heading("Open From Workshop");SteamAccount();
                    var items=PortableMapWorkshopPublisher.Items;
                    var openIds=new[]{""}.Concat(items.Select(i=>i.id)).ToList();
                    var openLabels=new[]{"Choose a Workshop map"}.Concat(items.Select(i=>i.title+(i.owned?"  ·  Your map":""))).ToList();
                    int previous=openIds.IndexOf(itemId);
                    if(previous<0 && !string.IsNullOrEmpty(itemId)){openIds.Add(itemId);openLabels.Add("Selected Workshop item");previous=openIds.Count-1;}
                    int next=WorkshopDropdown("Workshop Map",Mathf.Max(0,previous),openLabels.ToArray());
                    if(next!=Mathf.Max(0,previous)) itemId=openIds[next];
                    using(new EditorGUI.DisabledScope(string.IsNullOrEmpty(itemId))) if(Button("Open Map")) PortableMapWorkshopPublisher.DownloadAndOpenSource(itemId,ImportFolder(items.FirstOrDefault(i=>i.id==itemId)?.title??"Workshop Map"));
                    advanced=EditorGUILayout.Foldout(advanced,"Open by Workshop ID",true);
                    if(advanced) {itemId=EditorGUILayout.TextField("Workshop ID",itemId);if(Button("Open")) PortableMapWorkshopPublisher.DownloadAndOpenSource(itemId,ImportFolder("Workshop Map"));}
                }
                else if(page==Page.Export)
                {
                    Heading("Export Map");if(!map) EditorGUILayout.HelpBox("Select the map root in the scene.",MessageType.Warning);
                    using(new EditorGUI.DisabledScope(!map))
                    {
                        if(Button("Steam Workshop…")) {page=Page.Publish;createWorkshopItem=string.IsNullOrEmpty(map.workshopItemId);updateItem=map.workshopItemId;PortableMapWorkshopPublisher.Connect();}
                        if(Button("Map File…")) AfterGUI(ExportFileDialog);
                        EditorGUILayout.Space(8);
                        using(new EditorGUILayout.HorizontalScope())
                        {
                            EditorGUILayout.LabelField("Materials",EditorStyles.miniLabel);
                            if(GUILayout.Button("Repair",EditorStyles.miniButton,GUILayout.Width(70))) AfterGUI(()=>RepairMaterials());
                        }
                    }
                }
                else
                {
                    Heading("Steam Workshop");SteamAccount();
                    if(map)
                    {
                        var data=new SerializedObject(map);data.Update();
                        EditorGUI.BeginChangeCheck();string title=EditorGUILayout.TextField("Title",map.DisplayName);
                        if(EditorGUI.EndChangeCheck()) RenameMap(map,title);
                        createWorkshopItem=EditorGUILayout.Popup("Publish",createWorkshopItem?1:0,new[]{"Update existing Workshop item","Create new Workshop item"})==1;
                        if(!createWorkshopItem)
                        {
                            var owned=PortableMapWorkshopPublisher.Items.Where(i=>i.owned).ToArray();
                            var ids=new[]{""}.Concat(owned.Select(i=>i.id)).ToList();
                            var labels=new[]{"Choose a Workshop item"}.Concat(owned.Select(i=>i.title)).ToList();
                            int selected=ids.IndexOf(updateItem);
                            if(selected<0 && !string.IsNullOrEmpty(updateItem)) {ids.Add(updateItem);labels.Add("Linked Workshop item");selected=ids.Count-1;}
                            int chosen=WorkshopDropdown("Workshop Item",Mathf.Max(0,selected),labels.ToArray());
                            if(chosen!=Mathf.Max(0,selected)) updateItem=ids[chosen];
                            advanced=EditorGUILayout.Foldout(advanced,"Advanced",true);
                            if(advanced) updateItem=EditorGUILayout.TextField("Item ID",updateItem);
                        }
                        EditorGUILayout.PropertyField(data.FindProperty("previewImage"),new GUIContent("Preview PNG (optional)"));data.ApplyModifiedProperties();
                        using(new EditorGUI.DisabledScope(!PortableMapWorkshopPublisher.CanUpload(out _)||(!createWorkshopItem&&string.IsNullOrWhiteSpace(updateItem))))
                            if(Button(createWorkshopItem?"Create Workshop Item":"Update Workshop Item")) AfterGUI(()=>PortableMapWorkshopPublisher.ExportAndUpload(map,map.DisplayName,PreviewPath(map),PublishingItem));
                    }
                    else EditorGUILayout.HelpBox("Select the map root in the scene.",MessageType.Warning);
                }
            }
            if(!string.IsNullOrEmpty(message)) EditorGUILayout.HelpBox(message,hasError?MessageType.Error:MessageType.Info);
            if((page==Page.Workshop||page==Page.Publish)&&!string.IsNullOrEmpty(PortableMapWorkshopPublisher.Status)) EditorGUILayout.HelpBox(PortableMapWorkshopPublisher.Status,PortableMapWorkshopPublisher.HasError?MessageType.Error:MessageType.None);
            EditorGUILayout.EndScrollView();
            NavigationFooter();
        }
        void NavigationFooter()
        {
            if(page!=Page.Samples && page!=Page.Workshop && page!=Page.Publish)return;
            GUILayout.Box(GUIContent.none,GUILayout.ExpandWidth(true),GUILayout.Height(1));
            using(new EditorGUILayout.HorizontalScope())
            using(new EditorGUI.DisabledScope(busy||EditorApplication.isPlayingOrWillChangePlaymode||PortableMapWorkshopPublisher.IsPending))
            {
                GUILayout.FlexibleSpace();
                if(GUILayout.Button("← Back",EditorStyles.miniButton,GUILayout.Width(82),GUILayout.Height(24)))
                {SetTab(page==Page.Samples?0:page==Page.Workshop?1:2);scroll=Vector2.zero;}
            }
            EditorGUILayout.Space(6);
        }
        public static string PreviewPath(PortableMapRoot root) {return root.previewImage?Path.GetFullPath(AssetDatabase.GetAssetPath(root.previewImage)):"";}
        public static void RenameMap(PortableMapRoot root,string name)
        {Undo.RecordObject(root.gameObject,"Rename Map");root.mapName=name;EditorUtility.SetDirty(root.gameObject);EditorSceneManager.MarkSceneDirty(root.gameObject.scene);}
        static bool Button(string label) {return GUILayout.Button(label,GUILayout.MinHeight(30));}
        static void Heading(string label) {EditorGUILayout.LabelField(label,EditorStyles.boldLabel);EditorGUILayout.Space(8);}
        static int WorkshopDropdown(string label,int selected,string[] choices)
        {
            using(new EditorGUILayout.HorizontalScope())
            {
                int next=EditorGUILayout.Popup(label,selected,choices);
                if(GUILayout.Button(new GUIContent("Refresh","Refresh your Workshop maps"),EditorStyles.miniButton,GUILayout.Width(60))) PortableMapWorkshopPublisher.BrowseSubscribed();
                return next;
            }
        }
        static void SteamAccount()
        {
            using(new EditorGUILayout.HorizontalScope()) {EditorGUILayout.LabelField(PortableMapWorkshopPublisher.Account);if(GUILayout.Button("Connect",GUILayout.Width(70))) PortableMapWorkshopPublisher.Connect();}
        }
        public static void OpenSampleAndShow() {var root=PortableCourtyardSample.OpenOrCreate();if(root) Selection.activeGameObject=root.gameObject;}
    }
    [CustomEditor(typeof(PortableMapRoot))]
    public sealed class PortableMapRootInspector : UnityEditor.Editor
    {
        public override void OnInspectorGUI()
        {
            var root=(PortableMapRoot)target;serializedObject.Update();
            EditorGUI.BeginChangeCheck();string name=EditorGUILayout.TextField("Map Name",root.DisplayName);
            if(EditorGUI.EndChangeCheck()) PortableMapAuthoringWindow.RenameMap(root,name);
            using(new EditorGUI.DisabledScope(true)) EditorGUILayout.PropertyField(serializedObject.FindProperty("mapId"),new GUIContent("Map ID"));
            EditorGUILayout.PropertyField(serializedObject.FindProperty("previewImage"),new GUIContent("Preview PNG (optional)"));serializedObject.ApplyModifiedProperties();
            EditorGUILayout.Space();
            var outside=PortableMapExporter.GeometryOutsideMap(root);
            if(outside.Length>0) EditorGUILayout.HelpBox("Move scene geometry under this map root before exporting: "+string.Join(", ",outside.Take(5).Select(go=>go.name)),MessageType.Warning);
            using(new EditorGUI.DisabledScope(PortableMapWorkshopPublisher.IsPending||EditorApplication.isPlayingOrWillChangePlaymode))
            {
                if(GUILayout.Button(string.IsNullOrEmpty(root.workshopItemId)?"Publish to Workshop…":"Update Workshop…",GUILayout.MinHeight(30))) PortableMapAuthoringWindow.ShowExport(root,true);
                if(GUILayout.Button("Export Map File…")) PortableMapAuthoringWindow.ShowFileExport(root);
            }
            EditorGUILayout.Space();if(!string.IsNullOrEmpty(root.workshopItemId)) EditorGUILayout.LabelField("Workshop Item",root.workshopItemId);
        }
    }
}
