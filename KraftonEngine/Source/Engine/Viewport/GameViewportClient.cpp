#include "Viewport/GameViewportClient.h"

#include "Component/CameraComponent.h"
#include "Component/ControllerInputComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Input/InputFrame.h"
#include "Engine/Input/InputSystem.h"
#include "Object/Object.h"

#include <windows.h>

namespace
{
	FRotator GetCameraWorldRotation(const UCameraComponent* Camera)
	{
		return Camera ? Camera->GetWorldMatrix().ToRotator() : FRotator();
	}

	void SyncControllerRotationFromCamera(APlayerController* Controller, UCameraComponent* Camera)
	{
		if (!Controller || !Camera)
		{
			return;
		}
		FRotator Rotation = GetCameraWorldRotation(Camera);
		Rotation.Roll = 0.0f;
		Controller->SetControlRotation(Rotation);
	}
}

DEFINE_CLASS(UGameViewportClient, UObject)

void UGameViewportClient::OnBeginPIE(UCameraComponent* InitialTarget, FViewport* InViewport)
{
	Viewport = InViewport;
	Possess(InitialTarget);
	ResetInputState();
}

void UGameViewportClient::OnEndPIE()
{
	SetPossessed(false);
	SetPlayerController(nullptr);
	UnPossess();
	ResetInputState();
	bHasCursorClipRect = false;
	ClearPresentationRect();
	Viewport = nullptr;
}

void UGameViewportClient::SetPIEPossessedInputEnabled(bool bEnabled)
{
	SetPossessed(bEnabled);
}

void UGameViewportClient::SetPresentationRect(const FViewportPresentationRect& InRect)
{
	PresentationRect = InRect;
}

void UGameViewportClient::ClearPresentationRect()
{
	PresentationRect = FViewportPresentationRect();
}

bool UGameViewportClient::IsScreenPositionInsideViewport(float ScreenX, float ScreenY) const
{
	return PresentationRect.Contains(ScreenX, ScreenY);
}

bool UGameViewportClient::ScreenToViewportPosition(float ScreenX, float ScreenY, float& OutX, float& OutY) const
{
	return PresentationRect.ScreenToLocal(ScreenX, ScreenY, OutX, OutY);
}

void UGameViewportClient::SetCursorClipRect(const FViewportPresentationRect& InViewportScreenRect)
{
	if (!InViewportScreenRect.IsValid())
	{
		bHasCursorClipRect = false;
		if (bCursorCaptured)
		{
			ApplyCursorClip();
		}
		return;
	}

	CursorClipClientRect.left = static_cast<LONG>(InViewportScreenRect.X);
	CursorClipClientRect.top = static_cast<LONG>(InViewportScreenRect.Y);
	CursorClipClientRect.right = static_cast<LONG>(InViewportScreenRect.Right());
	CursorClipClientRect.bottom = static_cast<LONG>(InViewportScreenRect.Bottom());
	bHasCursorClipRect = CursorClipClientRect.right > CursorClipClientRect.left
		&& CursorClipClientRect.bottom > CursorClipClientRect.top;

	if (bCursorCaptured)
	{
		ApplyCursorClip();
	}
}

void UGameViewportClient::SetCursorClipRect(const FRect& InViewportScreenRect)
{
	SetCursorClipRect(FViewportPresentationRect(
		InViewportScreenRect.X,
		InViewportScreenRect.Y,
		InViewportScreenRect.Width,
		InViewportScreenRect.Height));
}

void UGameViewportClient::SetPossessed(bool bPossessed)
{
	if (bPIEPossessedInputEnabled == bPossessed)
	{
		return;
	}
	bPIEPossessedInputEnabled = bPossessed;
	SetCursorCaptured(bPossessed);
	ResetInputState();
}

void UGameViewportClient::SetPlayerController(APlayerController* InController)
{
	if (PlayerController == InController)
	{
		return;
	}
	PlayerController = InController;
	SyncControllerRotationFromCamera(PlayerController, GetPossessedTarget());
	ResetInputState();
}

void UGameViewportClient::Possess(UCameraComponent* TargetCamera)
{
	if (PossessedCamera == TargetCamera)
	{
		return;
	}
	PossessedCamera = TargetCamera;
	SyncControllerRotationFromCamera(PlayerController, GetPossessedTarget());
	ResetInputState();
}

void UGameViewportClient::UnPossess()
{
	PossessedCamera = nullptr;
	SetCursorCaptured(false);
	InputSystem::Get().SetUseRawMouse(false);
	ResetInputState();
}

UCameraComponent* UGameViewportClient::GetPossessedTarget() const
{
	if (PlayerController && IsAliveObject(PlayerController))
	{
		if (UCameraComponent* Camera = PlayerController->ResolveViewCamera())
		{
			return Camera;
		}
	}
	return IsAliveObject(PossessedCamera) ? PossessedCamera : nullptr;
}

void UGameViewportClient::ResetInputState()
{
	InputSystem::Get().ResetMouseDelta();
	InputSystem::Get().ResetWheelDelta();
}

bool UGameViewportClient::Tick(float DeltaTime, FInputFrame& InputFrame)
{
	const bool bUiActive = UiLayer && UiLayer->IsBlockingGameplayInput();

	if (!bPIEPossessedInputEnabled || !HasPossessedTarget())
	{
		if (bPIEPossessedInputEnabled && bUiActive)
		{
			InputSystem::Get().SetUseRawMouse(false);
			SetCursorCaptured(false);
		}
		return false;
	}
	if (!InputFrame.IsWindowFocused())
	{
		InputSystem::Get().SetUseRawMouse(false);
		SetCursorCaptured(false);
		ResetInputState();
		return false;
	}
	if (bUiActive)
	{
		InputSystem::Get().SetUseRawMouse(false);
		SetCursorCaptured(false);
		return false;
	}
	InputSystem::Get().SetUseRawMouse(true);
	SetCursorCaptured(true);
	return ApplyInputToCameraOrActor(DeltaTime, InputFrame);
}

bool UGameViewportClient::ApplyInputToCameraOrActor(float DeltaTime, FInputFrame& InputFrame)
{
	if (!HasPossessedTarget())
	{
		return false;
	}
	APlayerController* SafeController = IsAliveObject(PlayerController) ? PlayerController : nullptr;
	if (SafeController)
	{
		if (UControllerInputComponent* InputComponent = SafeController->FindControllerInputComponent())
		{
			return InputComponent->ApplyInput(SafeController, PossessedCamera, DeltaTime, InputFrame);
		}
	}
	return false;
}

void UGameViewportClient::SetCursorCaptured(bool bCaptured)
{
	if (bCursorCaptured == bCaptured)
	{
		if (bCaptured)
		{
			ApplyCursorClip();
		}
		return;
	}
	bCursorCaptured = bCaptured;
	if (bCursorCaptured)
	{
		while (::ShowCursor(FALSE) >= 0) {}
		ApplyCursorClip();
		return;
	}
	while (::ShowCursor(TRUE) < 0) {}
	::ClipCursor(nullptr);
}

void UGameViewportClient::ApplyCursorClip()
{
	if (!OwnerHWnd)
	{
		return;
	}
	RECT ClientRect = {};
	if (bHasCursorClipRect)
	{
		ClientRect = CursorClipClientRect;
	}
	else if (!::GetClientRect(OwnerHWnd, &ClientRect))
	{
		return;
	}
	POINT TopLeft = { ClientRect.left, ClientRect.top };
	POINT BottomRight = { ClientRect.right, ClientRect.bottom };
	if (!::ClientToScreen(OwnerHWnd, &TopLeft) || !::ClientToScreen(OwnerHWnd, &BottomRight))
	{
		return;
	}
	RECT ScreenRect = { TopLeft.x, TopLeft.y, BottomRight.x, BottomRight.y };
	if (ScreenRect.right > ScreenRect.left && ScreenRect.bottom > ScreenRect.top)
	{
		::ClipCursor(&ScreenRect);
	}
}
