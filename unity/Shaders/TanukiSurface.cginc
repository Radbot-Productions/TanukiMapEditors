#ifndef TANUKI_SURFACE_INCLUDED
#define TANUKI_SURFACE_INCLUDED
#include "UnityStandardUtils.cginc"
sampler2D _MainTex, _MetallicRoughnessMap, _BumpMap, _OcclusionMap, _EmissionMap;
fixed4 _Color, _EmissionColor;
half _Roughness, _Metallic, _BumpScale, _OcclusionStrength, _EmissionStrength, _EmissionEnabled, _Cutoff;
struct Input { float2 uv_MainTex; };
void surf(Input IN, inout SurfaceOutputStandard o)
{
    fixed4 base = tex2D(_MainTex, IN.uv_MainTex) * _Color;
    half4 mr = tex2D(_MetallicRoughnessMap, IN.uv_MainTex);
    o.Albedo = base.rgb;
    o.Alpha = base.a;
#ifdef TANUKI_CUTOUT
    clip(base.a - _Cutoff);
#endif
    o.Metallic = saturate(_Metallic * mr.b);
    o.Smoothness = 1 - saturate(_Roughness * mr.g);
    o.Normal = UnpackScaleNormal(tex2D(_BumpMap, IN.uv_MainTex), _BumpScale);
    o.Occlusion = lerp(1, tex2D(_OcclusionMap, IN.uv_MainTex).r, _OcclusionStrength);
#ifdef TANUKI_EMISSIVE
    if(_EmissionEnabled>.5)o.Emission = tex2D(_EmissionMap, IN.uv_MainTex).rgb * _EmissionColor.rgb * _EmissionStrength;
#endif
}
#endif
