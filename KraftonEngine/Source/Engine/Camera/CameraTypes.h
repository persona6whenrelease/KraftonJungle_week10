#pragma once

#include "Core/CoreTypes.h"
#include "Math/Quat.h"
#include "Math/Vector.h"

struct FCameraState
{
	float FOV = 3.14159265358979f / 3.0f;
	float AspectRatio = 16.0f / 9.0f;
	float NearZ = 0.1f;
	float FarZ = 1000.0f;
	float OrthoWidth = 10.0f;
	bool bIsOrthogonal = false;
};

struct FCameraView
{
	FVector Location = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity;
	FCameraState State;
	bool bValid = false;
};

enum class ECameraViewMode : int32
{
	Static = 0,
	FirstPerson = 1,
	ThirdPerson = 2,
	OrthographicFollow = 3,
	Custom = 4,
};

enum class ECameraBlendFunction : int32
{
	Linear = 0,
	EaseIn = 1,
	EaseOut = 2,
	EaseInOut = 3,
};

enum class ECameraProjectionSwitchMode : int32
{
	SwitchAtStart = 0,
	SwitchAtHalf = 1,
	SwitchAtEnd = 2,
};

struct FCameraTransitionSettings
{
	float BlendTime = 0.35f;
	ECameraBlendFunction Function = ECameraBlendFunction::EaseInOut;
	ECameraProjectionSwitchMode ProjectionSwitchMode = ECameraProjectionSwitchMode::SwitchAtHalf;

	bool bBlendLocation = true;
	bool bBlendRotation = true;
	bool bBlendFOV = true;
	bool bBlendOrthoWidth = true;
	bool bBlendNearFar = false;
};

using FCameraBlendParams = FCameraTransitionSettings;

struct FCameraSmoothingSettings
{
	bool bEnableSmoothing = true;
	float LocationLagSpeed = 12.0f;
	float RotationLagSpeed = 12.0f;
	float FOVLagSpeed = 10.0f;
	float OrthoWidthLagSpeed = 10.0f;
};

struct FCameraComponentReference
{
	uint32 OwnerActorUUID = 0;
	FString ComponentPath;

	bool IsSet() const
	{
		return OwnerActorUUID != 0 && !ComponentPath.empty();
	}

	void Reset()
	{
		OwnerActorUUID = 0;
		ComponentPath.clear();
	}
};
