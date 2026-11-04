Shader "Tanuki/URP/Transparent"
{
 Properties {
        [MainColor] _Color ("Base Color / Opacity", Color) = (1,1,1,1)
        [MainTexture] _MainTex ("Base Color Texture (sRGB)", 2D) = "white" {}
        _Roughness ("Roughness", Range(0,1)) = 0.7
        _Metallic ("Metallic", Range(0,1)) = 0
        [NoScaleOffset] _MetallicRoughnessMap ("Metallic (B) / Roughness (G)", 2D) = "white" {}
        [Normal] [NoScaleOffset] _BumpMap ("Normal Texture", 2D) = "bump" {}
        _BumpScale ("Normal Strength", Range(0,10)) = 1
        [NoScaleOffset] _OcclusionMap ("AO Texture (R)", 2D) = "white" {}
        _OcclusionStrength ("AO Strength", Range(0,1)) = 1
        [HideInInspector] [NoScaleOffset] _EmissionMap ("Emission Texture (sRGB)", 2D) = "white" {}
        [HideInInspector] _EmissionColor ("Emission Color", Color) = (0,0,0,1)
        [HideInInspector] _EmissionStrength ("Emission Strength", Float) = 1
        [HideInInspector] _Cutoff ("Alpha Cutoff", Range(0,1)) = 0.5
 }
 SubShader
 {
  Tags { "RenderPipeline"="UniversalPipeline" "RenderType"="Transparent" "Queue"="Transparent" }
  HLSLINCLUDE
  #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"
  #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Lighting.hlsl"
  TEXTURE2D(_MainTex); SAMPLER(sampler_MainTex);
  TEXTURE2D(_MetallicRoughnessMap); SAMPLER(sampler_MetallicRoughnessMap);
  TEXTURE2D(_BumpMap); SAMPLER(sampler_BumpMap);
  TEXTURE2D(_OcclusionMap); SAMPLER(sampler_OcclusionMap);
  TEXTURE2D(_EmissionMap); SAMPLER(sampler_EmissionMap);
  CBUFFER_START(UnityPerMaterial)
  float4 _MainTex_ST, _Color, _EmissionColor;
  float _Roughness, _Metallic, _BumpScale, _OcclusionStrength, _EmissionStrength, _Cutoff;
  CBUFFER_END
  struct Attributes {float4 positionOS:POSITION;float3 normalOS:NORMAL;float4 tangentOS:TANGENT;float2 uv:TEXCOORD0;};
  struct Varyings {float4 positionCS:SV_POSITION;float3 positionWS:TEXCOORD0;half3 normalWS:TEXCOORD1;half4 tangentWS:TEXCOORD2;float2 uv:TEXCOORD3;};
  Varyings Vert(Attributes i)
  {
   Varyings o;
   VertexPositionInputs pos=GetVertexPositionInputs(i.positionOS.xyz);
   VertexNormalInputs norm=GetVertexNormalInputs(i.normalOS,i.tangentOS);
   o.positionCS=pos.positionCS;o.positionWS=pos.positionWS;
   o.normalWS=norm.normalWS;o.tangentWS=half4(norm.tangentWS,i.tangentOS.w*GetOddNegativeScale());
   o.uv=TRANSFORM_TEX(i.uv,_MainTex);return o;
  }
  half4 Base(float2 uv)
  {
   half4 value=SAMPLE_TEXTURE2D(_MainTex,sampler_MainTex,uv)*_Color;
   
   return value;
  }
  ENDHLSL
  Pass
  {
   Name "TanukiForward"
   Tags {"LightMode"="UniversalForwardOnly"}
   Cull Back
   Blend SrcAlpha OneMinusSrcAlpha
   ZWrite Off
   HLSLPROGRAM
   #pragma vertex Vert
   #pragma fragment Frag
   #pragma multi_compile _ _MAIN_LIGHT_SHADOWS _MAIN_LIGHT_SHADOWS_CASCADE _MAIN_LIGHT_SHADOWS_SCREEN
   #pragma multi_compile _ _ADDITIONAL_LIGHTS_VERTEX _ADDITIONAL_LIGHTS
   #pragma multi_compile_fragment _ _ADDITIONAL_LIGHT_SHADOWS
   #pragma multi_compile_fragment _ _SHADOWS_SOFT
   #pragma multi_compile_fog
   half4 Frag(Varyings i):SV_Target
   {
    half4 base=Base(i.uv);
    half3 mr=SAMPLE_TEXTURE2D(_MetallicRoughnessMap,sampler_MetallicRoughnessMap,i.uv).rgb;
    SurfaceData surface=(SurfaceData)0;
    surface.albedo=base.rgb;surface.alpha=base.a;
    surface.metallic=saturate(_Metallic*mr.b);surface.smoothness=1-saturate(_Roughness*mr.g);
    surface.normalTS=UnpackNormalScale(SAMPLE_TEXTURE2D(_BumpMap,sampler_BumpMap,i.uv),_BumpScale);
    surface.occlusion=lerp(1,SAMPLE_TEXTURE2D(_OcclusionMap,sampler_OcclusionMap,i.uv).r,_OcclusionStrength);
    
    half3 n=normalize(i.normalWS),t=normalize(i.tangentWS.xyz),b=cross(n,t)*i.tangentWS.w;
    InputData input=(InputData)0;input.positionWS=i.positionWS;
    input.normalWS=NormalizeNormalPerPixel(TransformTangentToWorld(surface.normalTS,half3x3(t,b,n)));
    input.viewDirectionWS=GetWorldSpaceNormalizeViewDir(i.positionWS);
    input.shadowCoord=TransformWorldToShadowCoord(i.positionWS);
    input.bakedGI=SampleSH(input.normalWS);
    input.normalizedScreenSpaceUV=GetNormalizedScreenSpaceUV(i.positionCS);
    input.shadowMask=half4(1,1,1,1);
    input.vertexLighting=VertexLighting(i.positionWS,input.normalWS);
    half4 result=UniversalFragmentPBR(input,surface);
    result.rgb=MixFog(result.rgb,ComputeFogFactor(i.positionCS.z));return result;
   }
   ENDHLSL
  }

 }
 CustomEditor "Radbot.MapAuthoring.Editor.TanukiMaterialShaderGUI"
 FallBack Off
}
