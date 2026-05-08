// Vignette.hlsl
// Fullscreen Triangle VS + Vignette PS
// PostProcess 패스의 AlphaBlend 합성에 의해 SceneColor 위에 가장자리 색상이 lerp 됨.

#include "Common/Functions.hlsli"
#include "Common/VertexLayouts.hlsli"
#include "Common/SystemSamplers.hlsli"

// b2 (PerShader0): Vignette parameters
cbuffer VignetteBuffer : register(b2)
{
    float2 VignetteCenter;     // UV 공간 (0~1). Phase 2에서 Pawn 스크린 UV로 갱신
    float  VignetteIntensity;  // smoothstep 시작 거리 (작을수록 더 일찍 어두워짐)
    float  VignetteSmoothness; // smoothstep 폭
    float3 VignetteColor;      // 가장자리 색상
    float  _Pad0;
};

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

float4 PS(PS_Input_UV input) : SV_TARGET
{
    float dist = length(input.uv - VignetteCenter);
    
    // Intensity가 작을수록 마스크가 넓어짐 (중심에서 가까운 곳부터 1.0이 됨)
    float mask = smoothstep(VignetteIntensity, VignetteIntensity + VignetteSmoothness, dist);
    
    // mask 0 = 배경, mask 1 = VignetteColor
    return float4(0,0,0, mask);
}
