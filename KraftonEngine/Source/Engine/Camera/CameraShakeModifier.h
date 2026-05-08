#pragma once
#include "Camera/CameraModifier.h"
#include "Math/Rotator.h"

enum class ECameraShakePattern : uint32
{
	Sine   = 0,
	Perlin = 1,
};

struct FCameraShakeParams
{
	ECameraShakePattern Pattern = ECameraShakePattern::Perlin;

	float Duration = 2.5f;

	float BlendInTime = 0.03f;
	float BlendOutTime = 0.08f;

	FVector LocationAmplitude = FVector(0.05f, 0.05f, 0.03f);
	FRotator RotationAmplitude = FRotator(1.0f, 1.0f, 0.5f);
	float FOVAmplitude = 0.0f;

	float Frequency = 1.0f;
	float Roughness = 1.0f;

	bool bApplyInCameraLocalSpace = true;
	bool bSingleInstance = false;

	uint32 Seed = 0;
};

class UCameraShakeModifier : public UCameraModifier
{
public:
	DECLARE_CLASS(UCameraShakeModifier, UCameraModifier)

	UCameraShakeModifier() = default;
	~UCameraShakeModifier() override = default;

	void StartShake(const FCameraShakeParams& InParams);

	bool ModifyCamera(float DeltaTime, FCameraView& InOutView) override;

	bool IsFinished() const;
	const FCameraShakeParams& GetParams() const { return Params; }
	float GetElapsedTime() const { return ElapsedTime; }

private:
	void NormalizeParams();

	float EvalEnvelope(float Time) const;
	float EvalAxis(float Time, int32 AxisIndex) const;

	FVector EvalLocationOffset(float Time) const;
	FRotator EvalRotationOffset(float Time) const;
	float EvalFOVOffset(float Time) const;

private:
	FCameraShakeParams Params;
	float ElapsedTime = 0.0f;
	bool bStarted = false;
};
