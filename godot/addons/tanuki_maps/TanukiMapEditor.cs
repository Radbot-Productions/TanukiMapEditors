using Godot;
using System;
using System.IO;
using System.Linq;
using System.Threading.Tasks;

[Tool]
public partial class TanukiMapEditor : EditorPlugin
{
    VBoxContainer panel;
    TabContainer tabs;
    Label status;
    OptionButton openItems,updateItems,publishMode;
    HBoxContainer updateRow;
    LineEdit openId;
    TanukiWorkshop workshop;
    bool busy;
    bool exiting;
    RootInspector inspector;
    public override void _EnterTree()
    {
        workshop=new TanukiWorkshop();panel=new VBoxContainer{Name="Tanuki Map Editor",CustomMinimumSize=new Vector2(310,300)};
        TanukiMaterials.CreatePresets();
        tabs=new TabContainer{SizeFlagsVertical=Control.SizeFlags.ExpandFill};panel.AddChild(tabs);
        var create=Page("New");Button(create,"Create Empty Map",()=>Show(NewMap()));
        var samples=new MenuButton{Text="Start From Sample"};create.AddChild(samples);samples.GetPopup().AddItem("Room",0);samples.GetPopup().AddItem("Courtyard",1);samples.GetPopup().IdPressed+=id=>Run(()=>Show(Sample(id==1)));
        var open=Page("Open");Label(open,"Steam Workshop");openItems=WorkshopDropdown(open,out _);Button(open,"Open Map",()=>RunAsync(OpenWorkshop));
        var advanced=new VBoxContainer();var toggle=new CheckButton{Text="Open by Workshop ID"};open.AddChild(toggle);open.AddChild(advanced);advanced.Visible=false;toggle.Toggled+=v=>advanced.Visible=v;openId=new LineEdit{PlaceholderText="Workshop ID"};advanced.AddChild(openId);Button(advanced,"Open by ID",()=>RunAsync(async()=>await OpenWorkshopId(ulong.Parse(openId.Text))));
        Button(open,"Open Map File…",()=>ChooseFile(false,path=>{Show(TanukiMapFile.Import(path));return Task.CompletedTask;}));
        var export=Page("Export");publishMode=new OptionButton();publishMode.AddItem("Update existing Workshop item");publishMode.AddItem("Create new Workshop item");export.AddChild(publishMode);updateItems=WorkshopDropdown(export,out updateRow);publishMode.ItemSelected+=i=>updateRow.Visible=i==0;
        Button(export,"Publish to Workshop",()=>RunAsync(Publish));Button(export,"Export Map File…",()=>ChooseFile(true,path=>{TanukiMapFile.Export(Map(),path);status.Text="Map exported.";return Task.CompletedTask;}));
        var materialsMenu=new MenuButton{Text="Materials"};panel.AddChild(materialsMenu);materialsMenu.GetPopup().AddItem("Create Material Presets",0);materialsMenu.GetPopup().AddItem("Repair Map Materials",1);materialsMenu.GetPopup().IdPressed+=id=>Run(()=>{if(id==0)TanukiMaterials.CreatePresets();else RepairMaterials();});
        var components=new MenuButton{Text="Components"};panel.AddChild(components);components.GetPopup().AddItem("Play Boundary",0);components.GetPopup().AddItem("Player Start",1);
        Button(panel,"Play in Tanuki",PlayInTanuki);
        components.GetPopup().AddItem("Point Light",2);components.GetPopup().AddItem("Spot Light",3);components.GetPopup().AddItem("Directional Light",4);components.GetPopup().AddItem("Lighting Settings",5);components.GetPopup().AddItem("Skybox",6);
        components.GetPopup().IdPressed+=id=>Run(()=>{if(id==6)EditorInterface.Singleton.EditNode(TanukiMapEnvironment.EnsureSkybox(Map()));else if(id==5)EditorInterface.Singleton.EditNode(Map());else if(id<2)AddComponent(id==0);else AddLight((TanukiMapLight.LightShape)(id-2));});
        status=new Label{Text="Select the Map Root to edit its settings.",AutowrapMode=TextServer.AutowrapMode.WordSmart};panel.AddChild(status);
        // The compatibility docking API also opens correctly on a fresh editor layout.
#pragma warning disable CS0618
        AddControlToDock(DockSlot.LeftBr,panel);
#pragma warning restore CS0618
        inspector=new RootInspector(this);AddInspectorPlugin(inspector);SetProcess(true);FillItems();
        try{workshop.Connect();}catch(Exception error){status.Text=error.Message;}
        AddToolMenuItem("Tanuki Map Editor",Callable.From(ShowDock));
        CallDeferred(nameof(ShowDock));
        CallDeferred(nameof(ReconcileEditedMap));
        SceneChanged+=root=>{if(root is TanukiMapRoot map){publishMode.Select(string.IsNullOrEmpty(map.WorkshopItemId)?1:0);updateRow.Visible=publishMode.Selected==0;FillItems();CallDeferred(nameof(ShowDock));CallDeferred(nameof(ReconcileEditedMap));}};
    }
    VBoxContainer Page(string name){var page=new VBoxContainer{Name=name};page.AddThemeConstantOverride("separation",10);tabs.AddChild(page);return page;}
    static void Label(Node parent,string text)=>parent.AddChild(new Label{Text=text});
    void Button(Node parent,string text,Action action){var button=new Button{Text=text,CustomMinimumSize=new Vector2(0,32)};button.Pressed+=()=>Run(action);parent.AddChild(button);}
    OptionButton WorkshopDropdown(Node parent,out HBoxContainer row)
    {
        row=new HBoxContainer();parent.AddChild(row);
        var items=new OptionButton{SizeFlagsHorizontal=Control.SizeFlags.ExpandFill};row.AddChild(items);
        var refresh=new Button{Text="Refresh",TooltipText="Refresh your Workshop maps"};refresh.Pressed+=()=>RunAsync(Refresh);row.AddChild(refresh);
        return items;
    }
    void Run(Action action){if(busy)return;try{action();}catch(Exception e){status.Text=e.Message;GD.PushWarning(e.Message);}}
    async void RunAsync(Func<Task> action){if(busy||exiting)return;busy=true;status.Text="Working…";try{await action();}catch(Exception e){if(!exiting){status.Text=e.Message;GD.PushWarning(e.Message);}}finally{busy=false;}}
    readonly System.Collections.Generic.List<Tanuki.Authoring.PortableGameplayPreview> previews=new();
    string PreviewExecutable(){var config=new ConfigFile();config.Load("user://tanuki-preview.cfg");return config.GetValue("preview","executable","").AsString();}
    void PlayInTanuki()
    {
        var root=Map();previews.Add(new Tanuki.Authoring.PortableGameplayPreview(PreviewExecutable(),ProjectSettings.GlobalizePath("user://tanuki-preview"),file=>TanukiMapFile.Export(root,file),ProjectSettings.GlobalizePath("res://addons/tanuki_maps/native/TanukiPreviewSetup.exe")));status.Text="Preparing Tanuki preview. The player installs automatically on first use.";
    }
    public override void _Process(double delta){try{workshop?.Poll();for(int i=previews.Count-1;i>=0;i--)if(previews[i].HasExited){status.Text=previews[i].Complete();previews.RemoveAt(i);}if(previews.Count>0)status.Text=previews[^1].Status;}catch(Exception e){status.Text=e.Message;}}
    public override void _ExitTree(){exiting=true;SetProcess(false);foreach(var preview in previews)preview.Dispose();previews.Clear();RemoveToolMenuItem("Tanuki Map Editor");workshop?.Dispose();if(inspector!=null)RemoveInspectorPlugin(inspector);if(panel!=null){
#pragma warning disable CS0618
        RemoveControlFromDocks(panel);
#pragma warning restore CS0618
        panel.QueueFree();}}
    TanukiMapRoot Map()
    {
        var scene=EditorInterface.Singleton.GetEditedSceneRoot();
        var maps=scene==null?Array.Empty<TanukiMapRoot>():new[]{scene}.Concat(TanukiMapFile.Descendants(scene)).OfType<TanukiMapRoot>().ToArray();
        return maps.Length==1?maps[0]:throw new InvalidOperationException("The active scene must contain exactly one Tanuki Map Root.");
    }
    void RepairMaterials()
    {
        var root=Map();var changes=new System.Collections.Generic.List<(MeshInstance3D node,int slot,Material before,StandardMaterial3D after)>();
        var copies=new System.Collections.Generic.Dictionary<Material,StandardMaterial3D>();var report=new System.Collections.Generic.List<string>();
        StandardMaterial3D Convert(Material source,string context)
        {if(!copies.TryGetValue(source,out var converted)){converted=TanukiMaterials.Convert(source,out string note);copies.Add(source,converted);report.Add(context+" / "+source.ResourceName+": "+note);}return converted;}
        foreach(var node in TanukiMapFile.Descendants(root).OfType<MeshInstance3D>().Where(n=>n.Mesh!=null))
        {
            if(node.MaterialOverlay!=null){changes.Add((node,-2,node.MaterialOverlay,null));report.Add(node.Name+": extra material overlay omitted.");}
            if(node.MaterialOverride!=null)
            {
                try{TanukiMaterials.Validate(node.MaterialOverride,node.Name);}
                catch(InvalidDataException){changes.Add((node,-1,node.MaterialOverride,Convert(node.MaterialOverride,node.Name)));}
                continue;
            }
            for(int slot=0;slot<node.Mesh.GetSurfaceCount();slot++)
            {
                var source=node.GetActiveMaterial(slot);try{TanukiMaterials.Validate(source,node.Name);continue;}catch(InvalidDataException){}
                changes.Add((node,slot,node.GetSurfaceOverrideMaterial(slot),Convert(source,node.Name)));
            }
        }
        if(changes.Count>0)
        {
            var undo=GetUndoRedo();undo.CreateAction("Repair Map Materials",UndoRedo.MergeMode.Disable,root);
            foreach(var change in changes)
            {
                if(change.slot<0){string property=change.slot==-1?"material_override":"material_overlay";undo.AddDoProperty(change.node,property,change.after);undo.AddUndoProperty(change.node,property,change.before);}
                else {undo.AddDoMethod(change.node,"set_surface_override_material",change.slot,change.after);undo.AddUndoMethod(change.node,"set_surface_override_material",change.slot,change.before);}
            }
            undo.CommitAction();EditorInterface.Singleton.MarkSceneAsUnsaved();
        }
        status.Text=report.Count==0?"Materials already satisfy the portable contract.":string.Join("\n",report);
    }
    public static TanukiMapRoot NewMap()
    {
        var root=new TanukiMapRoot{Name="New Map"};TanukiMapFile.Own(root,new TanukiMapBoundary{Name="Play Boundary",Position=new Vector3(0,3.875f,0),Scale=new Vector3(20,8.25f,20)});
        var floor=new StaticBody3D{Name="Floor",Position=new Vector3(0,-.1f,0)};TanukiMapFile.Own(root,floor);
        var floorMaterial=TanukiShaders.Create();floorMaterial.SetShaderParameter("base_color",new Color(.5f,.55f,.6f));
        var mesh=new MeshInstance3D{Name="Geometry",Mesh=new BoxMesh{Size=new Vector3(20,.2f,20)},MaterialOverride=floorMaterial};floor.AddChild(mesh);mesh.Owner=root;
        var collision=new CollisionShape3D{Name="Collision",Shape=new BoxShape3D{Size=new Vector3(20,.2f,20)}};floor.AddChild(collision);collision.Owner=root;
        for(int i=0;i<4;i++)TanukiMapFile.Own(root,new TanukiPlayerStart{Name="Player Start "+(i+1),Position=new Vector3((i-1.5f)*2,1,0)});
        TanukiPreviewSetup.Ensure(root);return root;
    }
    public static TanukiMapRoot Sample(bool courtyard)
    {
        // Both engines start with the same intentionally plain, hand-specified sample files.
        string name=courtyard?"Courtyard":"Room";var root=TanukiMapFile.Import(ProjectSettings.GlobalizePath("res://addons/tanuki_maps/samples/"+name+".tanukimap"),true);TanukiPreviewSetup.Ensure(root);return root;
    }
    void Show(TanukiMapRoot root)
    {
        TanukiPreviewSetup.Ensure(root);
        string folder="res://Maps/"+Guid.NewGuid().ToString("N");Directory.CreateDirectory(ProjectSettings.GlobalizePath(folder));string path=folder+"/Map.tscn";
        try{var packed=new PackedScene();if(packed.Pack(root)!=Error.Ok || ResourceSaver.Save(packed,path)!=Error.Ok)throw new IOException("Could not save the editable map scene.");}
        finally{root.Free();}
        ProjectSettings.SetSetting("application/run/main_scene",path);ProjectSettings.Save();
        EditorInterface.Singleton.CloseScene();EditorInterface.Singleton.OpenSceneFromPath(path);var live=EditorInterface.Singleton.GetEditedSceneRoot();EditorInterface.Singleton.EditNode(live);status.Text="Edit the map in the 3D viewport. Press Play to test it.";
    }
    public void OpenMapFile(string path)=>Show(TanukiMapFile.Import(path));
    public void SetTab(int index){tabs.CurrentTab=index;ShowDock();}
    public void ShowDock(){for(Node parent=panel.GetParent();parent!=null;parent=parent.GetParent())if(parent is EditorDock dock){dock.MakeVisible();return;}panel.Show();}
    public void ReconcileEditedMap()
    {
        if(EditorInterface.Singleton.GetEditedSceneRoot() is not TanukiMapRoot root)return;
        bool missing=!root.GetChildren().OfType<TanukiPreviewPlayer>().Any();TanukiPreviewSetup.Ensure(root);
        if(missing)EditorInterface.Singleton.MarkSceneAsUnsaved();
        if(root.SceneFilePath.Length>0){ProjectSettings.SetSetting("application/run/main_scene",root.SceneFilePath);ProjectSettings.Save();}
    }
    void AddComponent(bool boundary)
    {
        if(boundary)
        {
            var existing=TanukiMapFile.Descendants(Map()).OfType<TanukiMapBoundary>().FirstOrDefault();
            if(existing!=null){EditorInterface.Singleton.EditNode(existing);return;}
        }
        var map=Map();var node=boundary?(Node3D)new TanukiMapBoundary{Name="Play Boundary",Position=new Vector3(0,3.875f,0),Scale=new Vector3(20,8.25f,20)}:new TanukiPlayerStart{Name="Player Start",Position=new Vector3(0,1,0)};
        var undo=GetUndoRedo();undo.CreateAction(boundary?"Add Play Boundary":"Add Player Start");undo.AddDoMethod(map,Node.MethodName.AddChild,node,true);undo.AddDoProperty(node,"owner",map);undo.AddDoReference(node);undo.AddUndoMethod(map,Node.MethodName.RemoveChild,node);undo.CommitAction();EditorInterface.Singleton.EditNode(node);
    }
    void AddLight(TanukiMapLight.LightShape shape)
    {
        var map=Map();var node=new TanukiMapLight{Name=shape+" Light",Shape=shape,Position=new Vector3(0,3,0)};
        var undo=GetUndoRedo();undo.CreateAction("Add Map Light");undo.AddDoMethod(map,Node.MethodName.AddChild,node,true);undo.AddDoProperty(node,"owner",map);undo.AddDoReference(node);undo.AddUndoMethod(map,Node.MethodName.RemoveChild,node);undo.CommitAction();EditorInterface.Singleton.EditNode(node);
    }
    void ChooseFile(bool save,Func<string,Task> action)
    {
        var dialog=new EditorFileDialog{Access=EditorFileDialog.AccessEnum.Filesystem,FileMode=save?EditorFileDialog.FileModeEnum.SaveFile:EditorFileDialog.FileModeEnum.OpenFile,Title=save?"Export Map File":"Open Map File",Filters=new[]{"*.tanukimap ; Tanuki map"}};
        if(save)dialog.CurrentFile=Map().Name.ToString()+".tanukimap";
        panel.AddChild(dialog);dialog.FileSelected+=path=>{RunAsync(()=>action(path));dialog.QueueFree();};dialog.Canceled+=()=>dialog.QueueFree();dialog.PopupCenteredRatio(.6f);
    }
    async Task Refresh(){await workshop.Refresh();FillItems();status.Text=workshop.Account+" — "+workshop.Items.Count+" maps loaded.";}
    void FillItems()
    {
        ulong selected=openItems.Selected>=0?(ulong)openItems.GetItemMetadata(openItems.Selected):0;openItems.Clear();updateItems.Clear();
        openItems.AddItem("Choose a Workshop map");openItems.SetItemMetadata(0,0UL);updateItems.AddItem("Choose a Workshop item");updateItems.SetItemMetadata(0,0UL);
        foreach(var item in workshop.Items){openItems.AddItem(item.Title);openItems.SetItemMetadata(openItems.ItemCount-1,item.Id);if(item.Id==selected)openItems.Select(openItems.ItemCount-1);if(item.Owned){updateItems.AddItem(item.Title);updateItems.SetItemMetadata(updateItems.ItemCount-1,item.Id);}}
        var root=EditorInterface.Singleton.GetEditedSceneRoot() as TanukiMapRoot;if(root!=null && ulong.TryParse(root.WorkshopItemId,out ulong linked) && linked!=0){int index=-1;for(int i=1;i<updateItems.ItemCount;i++)if((ulong)updateItems.GetItemMetadata(i)==linked)index=i;if(index<0){updateItems.AddItem("Linked Workshop item");index=updateItems.ItemCount-1;updateItems.SetItemMetadata(index,linked);}updateItems.Select(index);}
    }
    async Task OpenWorkshop(){ulong id=(ulong)openItems.GetItemMetadata(openItems.Selected);if(id==0)throw new InvalidOperationException("Choose a Workshop map.");await OpenWorkshopId(id);}
    async Task OpenWorkshopId(ulong id){var(file,item)=await workshop.Download(id);var root=TanukiMapFile.Import(file,!item.Owned);root.Name=item.Title;root.WorkshopItemId=item.Owned?id.ToString():"";Show(root);}
    async Task Publish()
    {
        var root=Map();ulong id=publishMode.Selected==1?0:(ulong)updateItems.GetItemMetadata(updateItems.Selected);if(publishMode.Selected==0&&id==0)throw new InvalidOperationException("Choose an existing Workshop item.");
        string job=ProjectSettings.GlobalizePath("user://workshop/"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(job+"/content");string file=job+"/content/map.tanukimap";TanukiMapFile.Export(root,file);
        string preview=job+"/preview.png";if(string.IsNullOrWhiteSpace(root.PreviewPng))await Thumbnail(root,preview);else File.Copy(ProjectSettings.GlobalizePath(root.PreviewPng),preview);
        void Record(ulong item){File.WriteAllText(job+"/item-id.txt",item.ToString());if(GodotObject.IsInstanceValid(root)){root.WorkshopItemId=item.ToString();if(EditorInterface.Singleton.GetEditedSceneRoot()==root){EditorInterface.Singleton.MarkSceneAsUnsaved();publishMode.Select(0);FillItems();}}}
        ulong result=await workshop.Publish(file,root.Name,preview,id,Record);status.Text="Workshop item "+result+" uploaded.";
    }
    public void ShowExport(TanukiMapRoot root){SetTab(2);publishMode.Select(string.IsNullOrEmpty(root.WorkshopItemId)?1:0);updateItems.Visible=publishMode.Selected==0;FillItems();}
    public async Task Thumbnail(TanukiMapRoot root,string file)
    {
        var viewport=new SubViewport{Size=new Vector2I(640,400),OwnWorld3D=true,TransparentBg=false,RenderTargetUpdateMode=SubViewport.UpdateMode.Always};panel.AddChild(viewport);
        try
        {
            Aabb? bounds=null;foreach(var node in TanukiMapFile.Descendants(root).OfType<MeshInstance3D>().Where(n=>n.Mesh!=null&&n.Visible))
            {var t=root.GlobalTransform.AffineInverse()*node.GlobalTransform;var copy=new MeshInstance3D{Mesh=node.Mesh,Transform=t,MaterialOverride=node.MaterialOverride};viewport.AddChild(copy);var b=t*node.GetAabb();bounds=bounds.HasValue?bounds.Value.Merge(b):b;}
            var box=bounds??new Aabb(-Vector3.One,Vector3.One*2);var center=box.GetCenter();float span=Mathf.Max(4,box.Size.Length());
            var camera=new Camera3D{Position=center+new Vector3(.65f,.7f,.85f).Normalized()*span,Current=true};viewport.AddChild(camera);camera.LookAt(center);viewport.AddChild(new DirectionalLight3D{RotationDegrees=new Vector3(-50,-30,0),LightEnergy=1.4f});
            viewport.AddChild(new WorldEnvironment{Environment=new Godot.Environment{BackgroundMode=Godot.Environment.BGMode.Color,BackgroundColor=new Color(.12f,.15f,.19f),AmbientLightSource=Godot.Environment.AmbientSource.Color,AmbientLightColor=Colors.White,AmbientLightEnergy=.6f}});
            await ToSignal(RenderingServer.Singleton,RenderingServer.SignalName.FramePostDraw);await ToSignal(RenderingServer.Singleton,RenderingServer.SignalName.FramePostDraw);
            if(viewport.GetTexture().GetImage().SavePng(file)!=Error.Ok)throw new IOException("Could not generate the preview PNG.");
        }
        finally{viewport.QueueFree();}
    }
    partial class RootInspector : EditorInspectorPlugin
    {
        readonly TanukiMapEditor plugin;public RootInspector(TanukiMapEditor plugin){this.plugin=plugin;}
        public override bool _CanHandle(GodotObject obj)=>obj is TanukiMapRoot;
        public override void _ParseBegin(GodotObject obj){var button=new Button{Text="Export Map…"};button.Pressed+=()=>plugin.ShowExport((TanukiMapRoot)obj);AddCustomControl(button);}
    }
}
