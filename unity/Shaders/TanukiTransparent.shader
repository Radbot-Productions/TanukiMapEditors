Shader "Tanuki/Transparent"
{
    Properties
    {
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
        Tags { "RenderType"="Transparent" "Queue"="Transparent" }
        LOD 300
        Cull Back
        CGPROGRAM
        #pragma target 3.0
        #pragma surface surf Standard fullforwardshadows alpha:fade
        
        #include "TanukiSurface.cginc"
        ENDCG
    }
    CustomEditor "Radbot.MapAuthoring.Editor.TanukiMaterialShaderGUI"
    FallBack Off
}
