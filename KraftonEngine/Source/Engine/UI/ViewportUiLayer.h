#pragma once

#include "Core/CoreTypes.h"
#include "Viewport/ViewportPresentationTypes.h"

#include <cstdint>

class IViewportUiLayer
{
public:
	virtual ~IViewportUiLayer() = default;

	virtual void Shutdown() {}
	virtual void SetPresentationRect(const FViewportPresentationRect& InRect) { (void)InRect; }
	virtual const FViewportPresentationRect& GetPresentationRect() const
	{
		static FViewportPresentationRect EmptyRect;
		return EmptyRect;
	}

	virtual void Update(float DeltaTime) { (void)DeltaTime; }
	virtual void Render() {}
	virtual bool IsAvailable() const { return false; }
	virtual bool IsInitialized() const { return false; }

	virtual bool ProcessWin32Message(void* hWnd, uint32 Msg, std::uintptr_t wParam, std::intptr_t lParam)
	{
		(void)hWnd;
		(void)Msg;
		(void)wParam;
		(void)lParam;
		return false;
	}

	virtual bool WantsMouse() const { return false; }
	virtual bool WantsKeyboard() const { return false; }
	virtual bool IsBlockingGameplayInput() const { return WantsMouse() || WantsKeyboard(); }
	virtual void SetLayerVisible(const FString& LayerName, bool bVisible) { (void)LayerName; (void)bVisible; }
};
