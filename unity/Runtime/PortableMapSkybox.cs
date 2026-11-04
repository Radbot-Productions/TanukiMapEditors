using UnityEngine;
namespace Radbot.MapAuthoring
{
    /// <summary>One profile-driven skybox. Its colors, sun and clouds follow Base Lighting.</summary>
    [ExecuteAlways,DisallowMultipleComponent,RequireComponent(typeof(PortableMapBaseLighting)),AddComponentMenu("Tanuki/Skybox")]
    public sealed class PortableMapSkybox : MonoBehaviour
    {
        public string Profile=>GetComponent<PortableMapBaseLighting>().Key;
    }
}
