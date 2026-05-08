#pragma once

#include "Camera/CameraModifier.h"
#include "Math/Vector.h"

/**
 * 전역적인 Vignette 효과를 제어하기 위한 카메라 모디파이어.
 * PlayerCameraManager를 통해 활성화되며, 카메라 전환 중에도 효과를 유지합니다.
 */
class UVignetteModifier : public UCameraModifier
{
public:
	DECLARE_CLASS(UVignetteModifier, UCameraModifier)
	UVignetteModifier() = default;
	~UVignetteModifier() override = default;

	/**
	 * Vignette 효과를 시작합니다.
	 * @param Intensity 목표 강도 (0.0=강함, 1.0=꺼짐)
	 * @param Color 효과 색상
	 * @param Duration 보간 시간 (초)
	 * @param Smoothness 테두리 부드러움 (기본 0.5)
	 */
	void StartVignette(float Intensity, const FVector& Color, float Duration, float Smoothness = 0.5f);

	/**
	 * Vignette 효과를 중단합니다.
	 * @param Duration 서서히 사라지는 시간 (초)
	 */
	void StopVignette(float Duration);

	// --- Getters / Setters ---
	void SetTargetIntensity(float Value) { TargetIntensity = Value; }
	void SetVignetteColor(const FVector& Color) { VignetteColor = Color; }
	void SetVignetteSmoothness(float Value) { VignetteSmoothness = Value; }

	float GetTargetIntensity() const { return TargetIntensity; }
	const FVector& GetVignetteColor() const { return VignetteColor; }
	float GetVignetteSmoothness() const { return VignetteSmoothness; }

	// UCameraModifier Interface
	bool ModifyCamera(float DeltaTime, FCameraView& InOutView) override;

private:
	FVector VignetteColor = FVector::ZeroVector;
	float TargetIntensity = 1.0f;
	float VignetteSmoothness = 0.5f;
};
