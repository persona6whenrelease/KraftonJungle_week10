// Fade.hlsl
// Fullscreen Triangle VS + Fade PS
// PostProcess 패스의 AlphaBlend 합성에 의해 SceneColor 위에 FadeColor가 FadeAlpha 만큼 lerp 됨.

#include "Common/Functions.hlsli"

// b2 (PerShader0): Fade parameters
cbuffer FadeBuffer : register(b2)
{
    float3 FadeColor;
    float  FadeAlpha;
};

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

float4 PS(PS_Input_UV input) : SV_TARGET
{
    return float4(FadeColor, FadeAlpha);
}
