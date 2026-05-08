#pragma once

#include "Object/Object.h"
#include "UI/SWindow.h"
#include "Viewport/ViewportClient.h"
#include "Viewport/ViewportPresentationTypes.h"
#include "Engine/UI/ViewportUiLayer.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

class FViewport;
class UCameraComponent;
class APlayerController;
struct FInputFrame;

// UE의 UGameViewportClient 대응 — UObject + FViewportClient 다중상속
// 게임 런타임 뷰포트를 담당 (PIE / Standalone)
class UGameViewportClient : public UObject, public FViewportClient
{
public:
	DECLARE_CLASS(UGameViewportClient, UObject)

	UGameViewportClient() = default;
	~UGameViewportClient() override = default;

	// FViewportClient overrides
	void Draw(FViewport* Viewport, float DeltaTime) override {}
	bool InputKey(int32 Key, bool bPressed) override { return false; }

	// Viewport 소유
	void SetViewport(FViewport* InViewport) { Viewport = InViewport; }
	FViewport* GetViewport() const { return Viewport; }
	void SetOwnerWindow(HWND InOwnerHWnd) { OwnerHWnd = InOwnerHWnd; }

	// 실제 화면에 표시되는 게임 뷰포트 영역.
	// Standalone에서는 보통 전체 클라이언트 영역이고, Editor PIE에서는 에디터 뷰포트 패널 영역이다.
	void SetPresentationRect(const FViewportPresentationRect& InRect);
	void ClearPresentationRect();
	const FViewportPresentationRect& GetPresentationRect() const { return PresentationRect; }
	bool HasValidPresentationRect() const { return PresentationRect.IsValid(); }
	bool IsScreenPositionInsideViewport(float ScreenX, float ScreenY) const;
	bool ScreenToViewportPosition(float ScreenX, float ScreenY, float& OutX, float& OutY) const;

	void SetCursorClipRect(const FViewportPresentationRect& InViewportScreenRect);
	void SetCursorClipRect(const FRect& InViewportScreenRect);
	void SetDrivingCamera(UCameraComponent* InCamera) { Possess(InCamera); }
	UCameraComponent* GetDrivingCamera() const { return GetPossessedTarget(); }
	void SetPlayerController(APlayerController* InController);
	APlayerController* GetPlayerController() const { return PlayerController; }

	void SetPIEPossessedInputEnabled(bool bEnabled);
	bool IsPIEPossessedInputEnabled() const { return bPIEPossessedInputEnabled; }
	bool IsPossessed() const { return bPIEPossessedInputEnabled; }
	void SetPossessed(bool bPossessed);
	void OnBeginPIE(UCameraComponent* InitialTarget, FViewport* InViewport);
	void OnEndPIE();
	void ResetInputState();
	void Possess(UCameraComponent* TargetCamera);
	void UnPossess();
	UCameraComponent* GetPossessedTarget() const;
	bool HasPossessedTarget() const { return GetPossessedTarget() != nullptr; }
	bool Tick(float DeltaTime, FInputFrame& InputFrame);

	void SetUiLayer(IViewportUiLayer* InUiLayer) { UiLayer = InUiLayer; }
	IViewportUiLayer* GetUiLayer() const { return UiLayer; }

private:
	bool ApplyInputToCameraOrActor(float DeltaTime, FInputFrame& InputFrame);
	void SetCursorCaptured(bool bCaptured);
	void ApplyCursorClip();

	FViewport* Viewport = nullptr;
	FViewportPresentationRect PresentationRect;
	HWND OwnerHWnd = nullptr;
	UCameraComponent* PossessedCamera = nullptr;
	APlayerController* PlayerController = nullptr;
	RECT CursorClipClientRect = {};
	bool bHasCursorClipRect = false;
	bool bPIEPossessedInputEnabled = false;
	bool bCursorCaptured = false;
	IViewportUiLayer* UiLayer = nullptr;
};
