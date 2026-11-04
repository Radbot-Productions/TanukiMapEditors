using Godot;
using System;

[Tool, GlobalClass]
public partial class TanukiMapRoot : Node3D
{
    public enum BaseLightingProfile {CartoonStreetDay,CartoonStreetNight}
    BaseLightingProfile lighting;
    bool overrideSun, sunShadows=true;
    Vector3 sunRotationDegrees;
    float sunIntensity=6;
    Color sunColor=Colors.White;
    internal bool UpdatingSun;
    void RefreshLighting(){if(!UpdatingSun&&IsInsideTree()&&GetNodeOrNull("_PreviewEnvironment")!=null)TanukiMapEnvironment.Apply(this);}
    [Export] public BaseLightingProfile BaseLighting
    {
        get=>lighting;
        set{lighting=value;overrideSun=false;RefreshLighting();}
    }
    [Export] public bool OverrideSun{get=>overrideSun;set{overrideSun=value;RefreshLighting();}}
    [Export] public Vector3 SunRotationDegrees{get=>sunRotationDegrees;set{sunRotationDegrees=value;RefreshLighting();}}
    [Export(PropertyHint.Range,"0,10000000,0.1,or_greater")] public float SunIntensity{get=>sunIntensity;set{sunIntensity=value;RefreshLighting();}}
    [Export(PropertyHint.ColorNoAlpha)] public Color SunColor{get=>sunColor;set{sunColor=value;RefreshLighting();}}
    [Export] public bool SunShadows{get=>sunShadows;set{sunShadows=value;RefreshLighting();}}
    public string LightingKey=>BaseLighting==BaseLightingProfile.CartoonStreetNight?"cartoon-street-night":"cartoon-street-day";
    public void SetLightingKey(string key)
    {
        if(key!="cartoon-street-day"&&key!="cartoon-street-night")throw new System.IO.InvalidDataException("Unsupported base lighting profile.");
        BaseLighting=key=="cartoon-street-night"?BaseLightingProfile.CartoonStreetNight:BaseLightingProfile.CartoonStreetDay;
    }
    [Export] public string MapId { get; set; } = "map-" + Guid.NewGuid().ToString("N");
    [Export] public string MapName { get => Name; set { if (!string.IsNullOrWhiteSpace(value)) Name = value; } }
    [Export] public string WorkshopItemId { get; set; } = "";
    [Export(PropertyHint.File, "*.png")] public string PreviewPng { get; set; } = "";
}
