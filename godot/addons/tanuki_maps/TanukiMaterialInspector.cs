using Godot;
using System;
using System.Collections.Generic;

[Tool]
public partial class TanukiMaterialInspector : EditorInspectorPlugin
{
    EditorPlugin plugin;
    public TanukiMaterialInspector(EditorPlugin plugin){this.plugin=plugin;}
    public override bool _CanHandle(GodotObject obj)=>obj is StandardMaterial3D;
    public override bool _ParseProperty(GodotObject obj,Variant.Type type,string name,PropertyHint hint,string hintText,PropertyUsageFlags usage,bool wide)
        =>name is not ("resource_name" or "resource_local_to_scene" or "resource_path");
    public override void _ParseBegin(GodotObject obj)
    {
        var material=(StandardMaterial3D)obj;
        var box=new VBoxContainer();AddCustomControl(box);
        box.AddChild(new Label{Text="Tanuki Material",ThemeTypeVariation="HeaderSmall"});
        var refresh=new List<Action>();
        void Change(string title,params (string property,Variant value)[] values)
        {
            var undo=plugin.GetUndoRedo();undo.CreateAction(title,UndoRedo.MergeMode.Disable,material);
            foreach(var pair in values){undo.AddDoProperty(material,pair.property,pair.value);undo.AddUndoProperty(material,pair.property,material.Get(pair.property));}
            undo.CommitAction();foreach(var update in refresh)update();
        }
        void Number(string title,Func<float> read,Action<float> write,float max=1,float step=.01f,Func<bool> enabled=null,float min=0)
        {
            box.AddChild(new Label{Text=title});var input=new SpinBox{MinValue=min,MaxValue=max,Step=step,Value=read(),Editable=enabled?.Invoke()??true};box.AddChild(input);
            input.ValueChanged+=value=>write((float)value);refresh.Add(()=>{input.SetValueNoSignal(read());input.Editable=enabled?.Invoke()??true;});
        }
        void Color(string title,Func<Godot.Color> read,Action<Godot.Color> write)
        {
            box.AddChild(new Label{Text=title});var picker=new ColorPickerButton{Color=read(),EditAlpha=false};box.AddChild(picker);picker.ColorChanged+=value=>write(value);refresh.Add(()=>picker.Color=read());
        }
        void Texture(string title,string property,Action<Texture2D> write)
        {
            box.AddChild(new Label{Text=title});var picker=new EditorResourcePicker{BaseType="Texture2D",EditedResource=material.Get(property).AsGodotObject() as Resource};box.AddChild(picker);
            picker.ResourceChanged+=value=>write(value as Texture2D);picker.ResourceSelected+=(resource,inspect)=>EditorInterface.Singleton.InspectObject(resource);
            refresh.Add(()=>picker.EditedResource=material.Get(property).AsGodotObject() as Resource);
        }
        void DataMap(string property,Texture2D texture)
        {
            string slot=property+"_texture",key="tanuki_"+property+"_fallback";
            float fallback=material.Get(slot).AsGodotObject()==null?material.Get(property).AsSingle():material.HasMeta(key)?material.GetMeta(key).AsSingle():property=="roughness"?.7f:0;
            var undo=plugin.GetUndoRedo();undo.CreateAction("Set Tanuki Data Texture",UndoRedo.MergeMode.Disable,material);
            foreach(var pair in new[]{(slot,Variant.From(texture)),(slot+"_channel",Variant.From(0)),(property,Variant.From(texture!=null?1f:fallback))})
            {undo.AddDoProperty(material,pair.Item1,pair.Item2);undo.AddUndoProperty(material,pair.Item1,material.Get(pair.Item1));}
            undo.AddDoMethod(material,"set_meta",key,fallback);
            if(material.HasMeta(key))undo.AddUndoMethod(material,"set_meta",key,material.GetMeta(key));else undo.AddUndoMethod(material,"remove_meta",key);
            undo.CommitAction();foreach(var update in refresh)update();
        }
        var surface=new OptionButton();foreach(string name in new[]{"Opaque PBR","Emissive","Cutout","Transparent"})surface.AddItem(name);box.AddChild(surface);
        int Profile()=>material.Transparency==BaseMaterial3D.TransparencyEnum.AlphaScissor?2:material.Transparency==BaseMaterial3D.TransparencyEnum.Alpha?3:material.EmissionEnabled?1:0;
        surface.Select(Profile());refresh.Add(()=>surface.Select(Profile()));
        surface.ItemSelected+=index=>Change("Set Tanuki Material Surface",("transparency",(int)(index==2?BaseMaterial3D.TransparencyEnum.AlphaScissor:index==3?BaseMaterial3D.TransparencyEnum.Alpha:BaseMaterial3D.TransparencyEnum.Disabled)),("emission_enabled",index==1),("emission_operator",1));
        Color("Base Color",()=>material.AlbedoColor,value=>{value.A=material.AlbedoColor.A;Change("Set Base Color",("albedo_color",value));});
        Texture("Base Color Texture","albedo_texture",value=>Change("Set Base Color Texture",("albedo_texture",value)));
        Number("Roughness",()=>material.Roughness,value=>Change("Set Roughness",("roughness",value)),enabled:()=>material.RoughnessTexture==null);
        Texture("Roughness Texture","roughness_texture",value=>DataMap("roughness",value));
        Number("Metallic",()=>material.Metallic,value=>Change("Set Metallic",("metallic",value)),enabled:()=>material.MetallicTexture==null);
        Texture("Metallic Texture","metallic_texture",value=>DataMap("metallic",value));
        Texture("Normal Texture","normal_texture",value=>Change("Set Normal Texture",("normal_texture",value),("normal_enabled",value!=null)));
        Number("Normal Strength",()=>material.NormalScale,value=>Change("Set Normal Strength",("normal_scale",value)),10);
        Texture("AO Texture","ao_texture",value=>Change("Set AO Texture",("ao_texture",value),("ao_enabled",value!=null),("ao_texture_channel",0)));
        Color("Emission Color",()=>material.Emission,value=>Change("Set Emission Color",("emission",value)));
        Number("Emission Strength",()=>material.EmissionEnergyMultiplier,value=>Change("Set Emission Strength",("emission_energy_multiplier",value)),100000,.1f);
        Texture("Emission Texture","emission_texture",value=>Change("Set Emission Texture",("emission_texture",value),("emission_operator",1)));
        Number("Opacity",()=>material.AlbedoColor.A,value=>{var color=material.AlbedoColor;color.A=value;Change("Set Opacity",("albedo_color",color));});
        Number("Alpha Cutoff",()=>material.AlphaScissorThreshold,value=>Change("Set Alpha Cutoff",("alpha_scissor_threshold",value)));
        foreach(var axis in new[]{0,1})
        {
            int component=axis;
            Number("UV Tiling "+(axis==0?"X":"Y"),()=>material.Uv1Scale[component],value=>{var v=material.Uv1Scale;v[component]=value;Change("Set UV Tiling",("uv1_scale",v));},1000,min:-1000);
            Number("UV Offset "+(axis==0?"X":"Y"),()=>material.Uv1Offset[component],value=>{var v=material.Uv1Offset;v[component]=value;Change("Set UV Offset",("uv1_offset",v));},1000,min:-1000);
        }
        box.AddChild(new Label{Text="Data textures use red. Empty slots use scalar values.",AutowrapMode=TextServer.AutowrapMode.WordSmart});
    }
}
