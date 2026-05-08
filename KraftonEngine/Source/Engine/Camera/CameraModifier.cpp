#include "Math/MathUtils.h"
#include "Camera/CameraModifier.h"
#include "Camera/PlayerCameraManager.h"
#include "Core/Log.h"

IMPLEMENT_CLASS(UCameraModifier, UObject)

void UCameraModifier::Initialize(APlayerCameraManager* Owner)
{
	CameraOwner = Owner;
}

bool UCameraModifier::UpdateCameraModifier(float DeltaTime, FCameraView& InOutView)
{
	const float SafeDeltaTime = DeltaTime > 0.0f ? DeltaTime : 0.0f;

	if (IsDisabled())
	{
		if (AlphaOutTime <= 0.0f)
		{
			Alpha = 0.0f;
		}
		else
		{
			Alpha = Clamp(Alpha - SafeDeltaTime / AlphaOutTime, 0.0f, 1.0f);
		}
	}
	else
	{
		if (AlphaInTime <= 0.0f)
		{
			Alpha = 1.0f;
		}
		else
		{
			Alpha = Clamp(Alpha + SafeDeltaTime / AlphaInTime, 0.0f, 1.0f);
		}
	}

	if (Alpha <= 0.0f)
	{
		return true;
	}

	return ModifyCamera(DeltaTime, InOutView);
}

bool UCameraModifier::ModifyCamera(float DeltaTime, FCameraView& InOutView)
{
	(void)DeltaTime;
	(void)InOutView;
	return true;
}

void UCameraModifier::EnableModifier()
{
	bDisabled = 0;
	bPendingRemove = 0;

	if (AlphaInTime <= 0.0f)
	{
		Alpha = 1.0f;
	}
}

void UCameraModifier::DisableModifier(bool bRemoveAfterFadeOut)
{
	bDisabled = 1;

	if (bRemoveAfterFadeOut)
	{
		bPendingRemove = 1;
	}

	if (AlphaOutTime <= 0.0f)
	{
		Alpha = 0.0f;
	}
}

void UCameraModifier::MarkPendingRemove()
{
	DisableModifier(true);
}