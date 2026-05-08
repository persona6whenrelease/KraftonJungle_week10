#include "Camera/CameraShakeModifier.h"

#include "Camera/CameraTypes.h"
#include "Math/MathUtils.h"
#include "Object/ObjectFactory.h"

#include <cmath>

IMPLEMENT_CLASS(UCameraShakeModifier, UCameraModifier)

void UCameraShakeModifier::StartShake(const FCameraShakeParams& InParams)
{
	Params = InParams;
	NormalizeParams();

	ElapsedTime = 0.0f;
	bStarted = true;

	SetAlpha(0.0f);
	SetAlphaInTime(Params.BlendInTime);
	SetAlphaOutTime(Params.BlendOutTime);
	EnableModifier();
}
bool UCameraShakeModifier::ModifyCamera(float DeltaTime, FCameraView& InOutView)
{
	if (!bStarted || !InOutView.bValid)
	{
		return true;
	}

	// DeltaTime is RawDeltaTime, unaffected by HitStop or Slomo.
	ElapsedTime += DeltaTime;

	if (Params.Duration > 0.0f && ElapsedTime > Params.Duration)
	{
		MarkPendingRemove();
	}

	const float LifeEnvelope = EvalEnvelope(ElapsedTime);
	const float FinalWeight = GetAlpha() * LifeEnvelope;

	if (FinalWeight <= 0.0f)
	{
		return true;
	}

	const FVector LocalLocationOffset = EvalLocationOffset(ElapsedTime) * FinalWeight;
	FRotator RotationOffset = EvalRotationOffset(ElapsedTime);

	RotationOffset.Pitch *= FinalWeight;
	RotationOffset.Yaw *= FinalWeight;
	RotationOffset.Roll *= FinalWeight;

	const float FOVOffset = EvalFOVOffset(ElapsedTime) * FinalWeight;

	if (Params.bApplyInCameraLocalSpace)
	{
		const FVector Forward = InOutView.Rotation.GetForwardVector();
		const FVector Right = InOutView.Rotation.GetRightVector();
		const FVector Up = InOutView.Rotation.GetUpVector();

		InOutView.Location += Forward * LocalLocationOffset.X;
		InOutView.Location += Right * LocalLocationOffset.Y;
		InOutView.Location += Up * LocalLocationOffset.Z;
	}
	else
	{
		InOutView.Location += LocalLocationOffset;
	}

	InOutView.Rotation = (InOutView.Rotation * RotationOffset.ToQuaternion()).GetNormalized();
	InOutView.State.FOV = FMath::Max(0.01f, InOutView.State.FOV + FOVOffset);
	InOutView.bValid = true;

	return true;
}
bool UCameraShakeModifier::IsFinished() const
{
	return !bStarted || IsShouldDestroy();
}

void UCameraShakeModifier::NormalizeParams()
{
	Params.Duration = FMath::Max(Params.Duration, 0.0f);
	Params.BlendInTime = FMath::Max(Params.BlendInTime, 0.0f);
	Params.BlendOutTime = FMath::Max(Params.BlendOutTime, 0.0f);
	Params.Frequency = FMath::Max(Params.Frequency, 0.0f);
	Params.Roughness = FMath::Max(Params.Roughness, 0.0f);
}
float UCameraShakeModifier::EvalEnvelope(float Time) const
{
	if (Params.Duration <= 0.0f)
	{
		return 0.0f;
	}

	const float BlendInAlpha = Params.BlendInTime > 0.0f ? FMath::Clamp01(Time / Params.BlendInTime) : 1.0f;
	const float BlendOutAlpha = Params.BlendOutTime > 0.0f ? FMath::Clamp01((Params.Duration - Time) / Params.BlendOutTime) : 1.0f;

	return FMath::SmoothStep(FMath::Min(BlendInAlpha, BlendOutAlpha));
}
float UCameraShakeModifier::EvalAxis(float Time, int32 AxisIndex) const
{
	const float X = Time * Params.Frequency;
	const int32 AxisSeed = static_cast<int32>(Params.Seed + static_cast<uint32>(AxisIndex * 97 + 13));

	switch (Params.Pattern)
	{
	case ECameraShakePattern::Sine:
	{
		const float Phase = static_cast<float>(AxisSeed % 360) * FMath::DegToRad;
		return std::sin(FMath::TwoPi * X + Phase);
	}
	case ECameraShakePattern::Perlin:
	default:
	{
		const float Noise = FMath::PerlinNoise1D(X + static_cast<float>(AxisIndex) * 31.7f, AxisSeed);
		return FMath::Clamp(Noise * Params.Roughness, -1.0f, 1.0f);
	}
	}
}
FVector UCameraShakeModifier::EvalLocationOffset(float Time) const
{
	return FVector(
		EvalAxis(Time, 0) * Params.LocationAmplitude.X,
		EvalAxis(Time, 1) * Params.LocationAmplitude.Y,
		EvalAxis(Time, 2) * Params.LocationAmplitude.Z);
}

FRotator UCameraShakeModifier::EvalRotationOffset(float Time) const
{
	return FRotator(EvalAxis(Time, 3) * Params.RotationAmplitude.Pitch, EvalAxis(Time, 4) * Params.RotationAmplitude.Yaw, EvalAxis(Time, 5) * Params.RotationAmplitude.Roll);
}
float UCameraShakeModifier::EvalFOVOffset(float Time) const
{
	return EvalAxis(Time, 6) * Params.FOVAmplitude;
}
