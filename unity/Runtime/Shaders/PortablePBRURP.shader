Shader "Tanuki/Portable PBR URP"
{
    Properties
    {
        [MainColor] _BaseColor ("Base Color", Color) = (0.6,0.6,0.6,1)
        [MainTexture] _BaseMap ("Base Color Texture", 2D) = "white" {}
        _Smoothness ("Smoothness Projection", Range(0,1)) = 0.3
        _Metallic ("Metallic", Range(0,1)) = 0
        _MetallicGlossMap ("Metallic Roughness Projection", 2D) = "white" {}
        _BumpMap ("Normal", 2D) = "bump" {}
        _BumpScale ("Normal Strength", Float) = 1
        _OcclusionMap ("AO", 2D) = "white" {}
        _OcclusionStrength ("AO Projection", Range(0,1)) = 1
        _EmissionColor ("Emission Color", Color) = (0,0,0,1)
        _EmissionMap ("Emission Texture", 2D) = "white" {}
        _Cutoff ("Alpha Cutoff", Range(0,1)) = 0.5
        [HideInInspector] _GlossMapScale ("", Float) = 1
        [HideInInspector] _SmoothnessTextureChannel ("", Float) = 0
        [HideInInspector] _SpecularHighlights ("", Float) = 1
        [HideInInspector] _GlossyReflections ("", Float) = 1
        [HideInInspector] _Mode ("", Float) = 0
        [HideInInspector] _SrcBlend ("", Float) = 1
        [HideInInspector] _DstBlend ("", Float) = 0
        [HideInInspector] _ZWrite ("", Float) = 1
        [HideInInspector] _UVSec ("", Float) = 0
        [HideInInspector] _Surface ("", Float) = 0
        [HideInInspector] _Blend ("", Float) = 0
        [HideInInspector] _Cull ("", Float) = 2
        [HideInInspector] _AlphaClip ("", Float) = 0
        [HideInInspector] _AlphaToMask ("", Float) = 0
    }
    SubShader
    {
        Tags { "RenderPipeline"="UniversalPipeline" "RenderType"="Opaque" "Queue"="Geometry" }
        LOD 300
        UsePass "Universal Render Pipeline/Lit/ForwardLit"
        UsePass "Universal Render Pipeline/Lit/ShadowCaster"
        UsePass "Universal Render Pipeline/Lit/DepthOnly"
        UsePass "Universal Render Pipeline/Lit/DepthNormals"
        UsePass "Universal Render Pipeline/Lit/Meta"
    }
    CustomEditor "Radbot.MapAuthoring.Editor.PortableMapMaterialShaderGUI"
    Fallback Off
}
