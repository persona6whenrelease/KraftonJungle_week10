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

// 카메라 단위 PostProcess 매개변수.
// CameraModifier chain이 매 프레임 누적/덮어써서 FCameraView 안에서 BlendViews와 함께 흐름.
// 하드웨어 AlphaBlend 합성을 전제로 하므로 *Alpha 필드는 [0,1] 범위.
struct FCameraPostProcess
{
	// Vignette
	FVector2 VignetteCenter = FVector2(0.5f, 0.5f);  // UV 공간. Pawn 추적 시 매 프레임 갱신, 폴백은 화면 중심
	float VignetteIntensity = 1.0f;                  // smoothstep 시작 거리 (1.0이면 효과 없음)
	float VignetteSmoothness = 0.5f;                 // smoothstep 폭
	FVector VignetteColor = FVector::ZeroVector;     // 가장자리 색상

	// Fade
	FVector FadeColor = FVector::ZeroVector;
	float FadeAlpha = 0.0f;                          // 0=무효, 1=완전 가림
};

struct FCameraView
{
	FVector Location = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity;
	FCameraState State;
	FCameraPostProcess PostProcess;
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
