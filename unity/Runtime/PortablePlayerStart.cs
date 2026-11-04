using UnityEngine;

namespace Radbot.MapAuthoring
{
    [DisallowMultipleComponent]
    [AddComponentMenu("Tanuki/Player Start")]
    public sealed class PortablePlayerStart : MonoBehaviour
    {
        [Tooltip("Optional spawn group/tag consumed by Unreal's existing PlayerStart selection.")]
        public string spawnTag = "";

        private void OnDrawGizmos()
        {
            Gizmos.color = Color.cyan;
            Gizmos.DrawWireSphere(transform.position, .3f);
            Gizmos.DrawLine(transform.position, transform.position + transform.forward);
        }
    }
}
