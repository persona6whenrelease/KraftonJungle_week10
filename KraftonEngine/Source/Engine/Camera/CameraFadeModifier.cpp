#include "Camera/CameraFadeModifier.h"
#include "Math/MathUtils.h"
#include "Camera/PlayerCameraManager.h"
#include "Core/Log.h"

IMPLEMENT_CLASS(UCameraFadeModifier, UCameraModifier)

void UCameraFadeModifier::StartFadeIn(float Duration, float TargetAlpha, const FVector& Color)
{
	FadeColor = Color;
	TargetFadeAlpha = Clamp(TargetAlpha, 0.0f, 1.0f);
	SetAlphaInTime(Duration > 0.0f ? Duration : 0.0f);
	SetAlpha(0.0f);
	EnableModifier();
}

void UCameraFadeModifier::StartFadeOut(float Duration)
{
	SetAlphaOutTime(Duration > 0.0f ? Duration : 0.0f);
	DisableModifier(false);
}

bool UCameraFadeModifier::ModifyCamera(float DeltaTime, FCameraView& InOutView)
{
	const float Effective = Clamp(GetAlpha() * TargetFadeAlpha, 0.0f, 1.0f);
	if (Effective > InOutView.PostProcess.FadeAlpha)
	{
		InOutView.PostProcess.FadeAlpha = Effective;
		InOutView.PostProcess.FadeColor = FadeColor;
	}
	return true;
}
