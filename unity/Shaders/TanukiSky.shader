Shader "Hidden/Tanuki/AuthoringSky"
{
    Properties { _Panorama("Sky Panorama",2D)="black"{} _Exposure("Exposure",Float)=1 }
    SubShader
    {
        Tags { "Queue"="Background" "RenderType"="Background" "PreviewType"="Skybox" }
        Cull Off ZWrite Off
        Pass
        {
            CGPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #include "UnityCG.cginc"
            sampler2D _Panorama;float _Exposure;
            struct V {float4 vertex:POSITION;};struct F {float4 position:SV_POSITION;float3 direction:TEXCOORD0;};
            F vert(V v){F o;o.position=UnityObjectToClipPos(v.vertex);o.direction=v.vertex.xyz;return o;}
            half4 frag(F f):SV_Target
            {
                float3 unity=normalize(f.direction);float3 direction=float3(unity.z,unity.x,unity.y);
                float2 uv=float2(atan2(direction.y,direction.x)/(2*UNITY_PI)+.5,asin(clamp(direction.z,-1,1))/UNITY_PI+.5);
                return half4(tex2D(_Panorama,uv).rgb*_Exposure,1);
            }
            ENDCG
        }
    }
}
