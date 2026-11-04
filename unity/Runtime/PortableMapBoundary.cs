using UnityEngine;

namespace Radbot.MapAuthoring
{
    [DisallowMultipleComponent]
    [AddComponentMenu("Tanuki/Map Boundary")]
    public sealed class PortableMapBoundary : MonoBehaviour
    {
        public enum BoundaryShape { Box, Cylinder }
        public BoundaryShape shape;

        // A unit box or unit-diameter cylinder. Native Transform tools own placement and size.
        void OnDrawGizmos()
        {
            var previous=Gizmos.matrix;
            Gizmos.matrix=transform.localToWorldMatrix;
            Gizmos.color=new Color(.2f,.7f,1f,.08f);
            if(shape==BoundaryShape.Box) Gizmos.DrawCube(Vector3.zero,Vector3.one);
            Gizmos.color=new Color(.2f,.7f,1f,.8f);
            if(shape==BoundaryShape.Box) Gizmos.DrawWireCube(Vector3.zero,Vector3.one);
            else
            {
#if UNITY_EDITOR
                var oldMatrix=UnityEditor.Handles.matrix;var oldColor=UnityEditor.Handles.color;
                UnityEditor.Handles.matrix=transform.localToWorldMatrix;UnityEditor.Handles.color=new Color(.2f,.7f,1f,.06f);
                for(int side=0;side<32;side++)
                {
                    float a=side*Mathf.PI/16,b=(side+1)*Mathf.PI/16;
                    var p=new Vector3(Mathf.Cos(a)*.5f,0,Mathf.Sin(a)*.5f);var q=new Vector3(Mathf.Cos(b)*.5f,0,Mathf.Sin(b)*.5f);
                    UnityEditor.Handles.DrawAAConvexPolygon(p-Vector3.up*.5f,p+Vector3.up*.5f,q+Vector3.up*.5f,q-Vector3.up*.5f);
                }
                UnityEditor.Handles.matrix=oldMatrix;UnityEditor.Handles.color=oldColor;
#endif
                for(int i=0;i<64;i++)
                {
                    float a=i*Mathf.PI/32,b=(i+1)*Mathf.PI/32;
                    var p=new Vector3(Mathf.Cos(a)*.5f,0,Mathf.Sin(a)*.5f);
                    var q=new Vector3(Mathf.Cos(b)*.5f,0,Mathf.Sin(b)*.5f);
                    Gizmos.DrawLine(p+Vector3.up*.5f,q+Vector3.up*.5f);
                    Gizmos.DrawLine(p-Vector3.up*.5f,q-Vector3.up*.5f);
                    if(i%8==0) Gizmos.DrawLine(p-Vector3.up*.5f,p+Vector3.up*.5f);
                }
            }
            Gizmos.matrix=previous;
        }
    }
}
