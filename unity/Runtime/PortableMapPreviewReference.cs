using UnityEngine;
namespace Radbot.MapAuthoring
{
    /// <summary>Editor scale/environment helpers never enter the portable scene.</summary>
    [DisallowMultipleComponent,AddComponentMenu("")]
    public sealed class PortableMapPreviewReference : MonoBehaviour {[HideInInspector] public int layoutVersion;}
}
