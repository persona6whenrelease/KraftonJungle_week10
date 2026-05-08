#pragma once

#include "Camera/CameraModifier.h"
#include "Math/Vector.h"

class UCameraFadeModifier : public UCameraModifier
{
public:
	DECLARE_CLASS(UCameraFadeModifier, UCameraModifier)
	UCameraFadeModifier() = default;
	~UCameraFadeModifier() override = default;

	// Phase 3 Lua API와 1:1 매핑되는 페이드 헬퍼.
	// Duration은 modifier의 Alpha를 0→1로 끌어올리는 시간(=페이드 인 시간), TargetAlpha는 완전 페이드 시 합성 강도.
	void StartFadeIn(float Duration, float TargetAlpha, const FVector& Color);
	void StartFadeOut(float Duration);

	void SetTargetFadeAlpha(float InAlpha) { TargetFadeAlpha = InAlpha; }
	void SetFadeColor(const FVector& InColor) { FadeColor = InColor; }
	float GetTargetFadeAlpha() const { return TargetFadeAlpha; }
	const FVector& GetFadeColor() const { return FadeColor; }

	bool ModifyCamera(float DeltaTime, FCameraView& InOutView) override;

private:
	FVector FadeColor = FVector::ZeroVector;
	float TargetFadeAlpha = 1.0f;
};
