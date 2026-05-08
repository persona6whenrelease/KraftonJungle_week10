#include "Camera/CameraVignetteModifier.h"
#include "Math/MathUtils.h"
#include "Camera/PlayerCameraManager.h"

IMPLEMENT_CLASS(UVignetteModifier, UCameraModifier)

void UVignetteModifier::StartVignette(float Intensity, const FVector& Color, float Duration, float Smoothness)
{
	VignetteColor = Color;
	TargetIntensity = Clamp(Intensity, 0.0f, 1.0f);
	VignetteSmoothness = Clamp(Smoothness, 0.0f, 1.0f);

	SetAlphaInTime(Duration > 0.0f ? Duration : 0.0f);
	SetAlpha(0.0f); 
	EnableModifier();
}

void UVignetteModifier::StopVignette(float Duration)
{
	SetAlphaOutTime(Duration > 0.0f ? Duration : 0.0f);
	DisableModifier(false);
}

bool UVignetteModifier::ModifyCamera(float DeltaTime, FCameraView& InOutView)
{
	// Alpha=0 일 때 Intensity=1.0 (효과 없음), Alpha=1 일 때 Intensity=TargetIntensity (효과 최대)
	const float CurrentAlpha = GetAlpha();
	const float EffectiveIntensity = 1.0f + (TargetIntensity - 1.0f) * CurrentAlpha;

	// 더 강한(작은 값) Vignette가 우선순위를 갖도록 함
	if (EffectiveIntensity < InOutView.PostProcess.VignetteIntensity)
	{
		InOutView.PostProcess.VignetteIntensity = EffectiveIntensity;
		InOutView.PostProcess.VignetteColor = VignetteColor;
		InOutView.PostProcess.VignetteSmoothness = VignetteSmoothness;
	}

	return true;
}
