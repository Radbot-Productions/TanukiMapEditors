using UnityEngine;

namespace Radbot.MapAuthoring
{
    /// <summary>Photometric portable light settings. Native Light is a local preview.</summary>
    [ExecuteAlways,DisallowMultipleComponent,RequireComponent(typeof(Light)),AddComponentMenu("Tanuki/Map Light")]
    public sealed class PortableMapLight : MonoBehaviour
    {
        public enum Shape { Point,Spot,Directional }
        public Shape shape;
        [ColorUsage(false,false)] public Color color=Color.white;
        [Min(0),Tooltip("Candela for Point/Spot; lux for Directional.")] public float intensity=1000;
        [Min(.01f)] public float range=10;
        [Range(0,89)] public float innerCone=20;
        [Range(.01f,89)] public float outerCone=35;
        public bool shadows=true;
        void OnEnable()=>Refresh();
        void OnValidate()=>Refresh();
        public void Refresh()
        {
            var light=GetComponent<Light>();if(!light)return;
            light.type=shape==Shape.Point?LightType.Point:shape==Shape.Spot?LightType.Spot:LightType.Directional;
            light.color=color;light.range=range;light.spotAngle=outerCone*2;light.innerSpotAngle=innerCone*2;
            light.shadows=shadows?LightShadows.Soft:LightShadows.None;
            // Normalize physical intensity by the selected base exposure. Built-in
            // attenuation remains approximate; the shared Tanuki player is authoritative.
            var environment=GetComponentInParent<PortableMapBaseLighting>();
            light.intensity=intensity*(environment?environment.ExposureFactor:1f/1.2f);
        }
    }
}
