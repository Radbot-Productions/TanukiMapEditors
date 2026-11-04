using System;
using UnityEngine;
using UnityEngine.Rendering;
namespace Radbot.MapAuthoring
{
    [ExecuteAlways,DisallowMultipleComponent,AddComponentMenu("Tanuki/Lighting Settings")]
    public sealed class PortableMapBaseLighting : MonoBehaviour
    {
        public enum LightingProfile { CartoonStreetDay,CartoonStreetNight }
        [Tooltip("Replaces base illumination only. Individually authored map lights are retained.")]
        public LightingProfile profile;
        public bool overrideSun;
        [Tooltip("Local sun rotation in degrees.")] public Vector3 sunRotation;
        [Min(0),Tooltip("Directional sunlight in lux.")] public float sunIntensity=6;
        [ColorUsage(false,false)] public Color sunColor=Color.white;
        public bool sunShadows=true;
        [SerializeField,HideInInspector] LightingProfile appliedProfile;
        public string Key=>profile==LightingProfile.CartoonStreetNight?"cartoon-street-night":"cartoon-street-day";
        [Serializable] public sealed class Sun {public float intensity;public float[] color,quaternion;}
        [Serializable] public sealed class Profile {public string key;public Sun[] suns;public float skyIntensity,exposureBias,minEV100,maxEV100,referenceEV100;public float[] skyColor,skyZenith,skyHorizon,skyOverall,skyLuminanceGain;}
        [Serializable] sealed class Collection {public Profile[] profiles;}
        public Profile Settings()
        {
            var data=Resources.Load<TextAsset>("Tanuki/lighting-profiles");
            if(!data)throw new InvalidOperationException("The plugin lighting-profile data is missing.");
            return Array.Find(JsonUtility.FromJson<Collection>(data.text).profiles,p=>p.key==Key)??throw new InvalidOperationException("Unknown base lighting profile.");
        }
        public float ExposureFactor
        {
            get{var p=Settings();float ev=Mathf.Approximately(p.minEV100,p.maxEV100)?p.minEV100:p.referenceEV100;return Mathf.Pow(2,p.exposureBias-ev)/(1.2f*Mathf.PI);}
        }
        public void SetKey(string key)
        {
            if(key!="cartoon-street-day"&&key!="cartoon-street-night")throw new InvalidOperationException("Unsupported base lighting profile.");
            profile=key=="cartoon-street-night"?LightingProfile.CartoonStreetNight:LightingProfile.CartoonStreetDay;overrideSun=false;Refresh();
        }
        void OnEnable()=>Schedule();
        void OnValidate(){if(appliedProfile!=profile)overrideSun=false;Schedule();}
        void Schedule()
        {
#if UNITY_EDITOR
            if(!Application.isPlaying){UnityEditor.EditorApplication.delayCall+=()=>{if(this&&!UnityEditor.EditorUtility.IsPersistent(this))Refresh();};return;}
#endif
            Refresh();
        }
        public void Refresh()
        {
            var p=Settings();var helper=transform.Find("_TanukiBaseLighting");
            if(!overrideSun){var sun=p.suns[0];var q=sun.quaternion;sunRotation=new Quaternion(q[1],q[2],q[0],q[3]).eulerAngles;sunIntensity=sun.intensity;sunColor=new Color(sun.color[0],sun.color[1],sun.color[2]).gamma;sunShadows=true;}
            appliedProfile=profile;
            if(!helper){var go=new GameObject("_TanukiBaseLighting");go.transform.SetParent(transform,false);go.AddComponent<PortableMapPreviewReference>();helper=go.transform;}
            for(int i=0;i<Mathf.Max(helper.childCount,p.suns.Length);i++)
            {
                Light light;
                if(i<helper.childCount)light=helper.GetChild(i).GetComponent<Light>();
                else{var go=new GameObject("Base Sun");go.transform.SetParent(helper,false);light=go.AddComponent<Light>();}
                light.enabled=i<p.suns.Length;if(!light.enabled)continue;
                var sun=p.suns[i];light.type=LightType.Directional;light.shadows=LightShadows.Soft;
                light.color=new Color(sun.color[0],sun.color[1],sun.color[2]).gamma;
                var q=sun.quaternion;light.transform.localRotation=new Quaternion(q[1],q[2],q[0],q[3]);
                light.intensity=sun.intensity*ExposureFactor;
                if(overrideSun){light.transform.localRotation=Quaternion.Euler(sunRotation);light.color=sunColor;light.intensity=sunIntensity*ExposureFactor;light.shadows=sunShadows?LightShadows.Soft:LightShadows.None;}
            }
            RenderSettings.ambientMode=AmbientMode.Flat;
            var c=p.skyColor??new[]{1f,1f,1f};var zenith=p.skyZenith??c;var horizon=p.skyHorizon??c;var overall=p.skyOverall??c;var gain=p.skyLuminanceGain??new[]{1f,1f,1f};
            Color Sky(float[] values)=>new Color(values[0]*c[0]*overall[0]*gain[0],values[1]*c[1]*overall[1]*gain[1],values[2]*c[2]*overall[2]*gain[2]);
            RenderSettings.ambientLight=Color.Lerp(Sky(horizon),Sky(zenith),.5f)*p.skyIntensity*ExposureFactor;
            var shader=Shader.Find("Hidden/Tanuki/AuthoringSky");
            if(shader)
            {
                var sky=RenderSettings.skybox;
                if(!sky||sky.shader!=shader){sky=new Material(shader){name="Tanuki Base Sky",hideFlags=HideFlags.DontSave};RenderSettings.skybox=sky;}
                sky.SetTexture("_Panorama",Resources.Load<Texture2D>("Tanuki/"+Key));sky.SetFloat("_Exposure",ExposureFactor*Mathf.PI);
            }
            foreach(var light in GetComponentsInChildren<PortableMapLight>(true))light.Refresh();
        }
    }
}
