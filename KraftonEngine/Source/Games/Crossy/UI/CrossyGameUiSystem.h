#pragma once

#include "Core/CoreTypes.h"
#include "Viewport/ViewportPresentationTypes.h"
#include "Engine/UI/ViewportUiLayer.h"

#include <cstdint>
#include <functional>
#include <memory>

// Keep <windows.h> out of this public header. Several Win32 helper macros
// collide with RmlUi headers when this header is included transitively.

class FRenderer;
class FWindowsWindow;
class UGameViewportClient;
class FRmlD3D11RenderInterface;
class FRmlWin32SystemInterface;
class FCrossyGameUiEventListener;

namespace Rml
{
	class Context;
	class Element;
	class ElementDocument;
}

struct FCrossyGameUiCallbacks
{
	std::function<void(const FString& CommandName)> ExecuteCommand;
	std::function<bool(const FString& StateName)> QueryBool;
	std::function<void(const FString& StateName, bool bValue)> SetBool;
};

// 공통 게임 UI 레이어.
// Standalone Client와 Editor PIE가 같은 UGameViewportClient/PresentationRect를 통해 동일한 HUD와 메뉴를 사용한다.
class FCrossyGameUiSystem : public IViewportUiLayer
{
public:
	FCrossyGameUiSystem();
	~FCrossyGameUiSystem() override;

	bool Initialize(FWindowsWindow* Window, FRenderer& Renderer, UGameViewportClient* InViewportClient);
	void Shutdown() override;

	void SetCallbacks(FCrossyGameUiCallbacks InCallbacks);
	void SetScriptEventHandler(std::function<void(const FString&)> InHandler);
	void ClearScriptEventHandler();

	void SetPresentationRect(const FViewportPresentationRect& InRect) override;
	const FViewportPresentationRect& GetPresentationRect() const override { return PresentationRect; }

	void Update(float DeltaTime) override;
	void Render() override;

	void SetIntroVisible(bool bVisible);
	bool IsIntroVisible() const { return bIntroVisible; }

	void SetHudVisible(bool bVisible);
	bool IsHudVisible() const { return bHudVisible; }

	void SetPauseMenuVisible(bool bVisible);
	void SetLayerVisible(const FString& LayerName, bool bVisible) override;
	bool IsPauseMenuVisible() const { return bPauseMenuVisible; }

	void SetGameOverVisible(bool bVisible);
	bool IsGameOverVisible() const { return bGameOverVisible; }

	void SetScore(int32 Score);
	void SetBestScore(int32 BestScore);
	void SetCoins(int32 Coins);
	void SetLane(int32 Lane);
	void SetCombo(int32 Combo);
	void SetStatusText(const FString& Text);
	void SetTopScoresText(const FString& Text);
	void ShowGameOver(int32 FinalScore, int32 BestScore);
	void HideGameOver();
	void ResetRunUi();

	bool IsAvailable() const override { return bAvailable; }
	bool IsInitialized() const override { return bInitialized; }

	bool ProcessWin32Message(void* hWnd, uint32 Msg, std::uintptr_t wParam, std::intptr_t lParam) override;
	bool ProcessMouseMove(float ScreenX, float ScreenY);
	bool ProcessMouseButtonDown(int Button, float ScreenX, float ScreenY);
	bool ProcessMouseButtonUp(int Button, float ScreenX, float ScreenY);
	bool ProcessMouseWheel(float WheelDelta, float ScreenX, float ScreenY);
	bool ProcessKeyDown(int VirtualKey);
	bool ProcessKeyUp(int VirtualKey);
	bool ProcessTextInput(uint32 Codepoint);

	bool WantsMouse() const override;
	bool WantsKeyboard() const override;
	void UnbindPauseMenuEvents();

private:
	friend class FCrossyGameUiEventListener;

	bool LoadDocuments();
	void BindUiEvents();
	void BindDocumentClickEvents(Rml::ElementDocument* Document, const char* const* ElementIds, int32 ElementCount);
	void UnbindDocumentClickEvents(Rml::ElementDocument* Document, const char* const* ElementIds, int32 ElementCount);
	void HandleClick(const FString& ElementId);
	void QueueScriptEvent(const FString& EventName);
	void FlushQueuedScriptEvents();
	void DispatchScriptEvent(const FString& EventName);

	void BeginStartTransition();
	void UpdateStartTransition(float DeltaTime);
	void CompleteStartTransition();
	void ApplyStartTransitionVisual(float TopHeightPx, float BottomHeightPx, int32 Alpha);
	void ApplyIntroIdleBoxVisual();
	float EaseInOutCubic(float T) const;
	float LerpFloat(float A, float B, float T) const;
	void SetElementProperty(Rml::ElementDocument* Document, const char* ElementId, const char* PropertyName, const char* Value);

	void SetOptionsVisible(bool bVisible);
	void SetIntroOptionsVisible(bool bVisible);
	void RefreshOptionLabels();
	void SetElementDisplay(Rml::ElementDocument* Document, const char* ElementId, bool bVisible);
	void SetElementText(Rml::ElementDocument* Document, const char* ElementId, const char* Text);
	void SetElementTextAny(const char* ElementId, const char* Text);
	bool IsInteractiveUiVisible() const;
	bool ShouldCaptureKeyboard() const;
	void ClearDocumentFocus(Rml::ElementDocument* Document);
	void ClearInteractiveFocus();
	void SyncContextDimensions();

private:
	FWindowsWindow* OwnerWindow = nullptr;
	UGameViewportClient* ViewportClient = nullptr;
	FViewportPresentationRect PresentationRect;
	FCrossyGameUiCallbacks Callbacks;
	std::function<void(const FString&)> ScriptEventHandler;
	TArray<FString> PendingScriptEvents;
	bool bFlushingScriptEvents = false;
	bool bStartEventQueuedOrDispatched = false;
	bool bStartTransitionActive = false;
	bool bStartTransitionResetDispatched = false;
	float StartTransitionTime = 0.0f;

	std::unique_ptr<FRmlD3D11RenderInterface> RenderInterface;
	std::unique_ptr<FRmlWin32SystemInterface> SystemInterface;
	std::unique_ptr<FCrossyGameUiEventListener> EventListener;

	Rml::Context* Context = nullptr;
	Rml::ElementDocument* IntroDocument = nullptr;
	Rml::ElementDocument* HudDocument = nullptr;
	Rml::ElementDocument* PauseMenuDocument = nullptr;
	Rml::ElementDocument* GameOverDocument = nullptr;

	bool bInitialized = false;
	bool bAvailable = false;
	bool bIntroVisible = false;
	bool bHudVisible = false;
	bool bPauseMenuVisible = false;
	bool bGameOverVisible = false;
	bool bShowingOptions = false;
	bool bShowingIntroOptions = false;
};
