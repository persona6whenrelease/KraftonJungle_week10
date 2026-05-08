#pragma once

#include "Core/CoreTypes.h"

class FRenderer;
class FViewport;
class FWindowsWindow;
class UCameraComponent;
class UGameClientEngine;
class UGameViewportClient;
class APlayerController;
class FGameClientViewport
{
public:
	bool Initialize(UGameClientEngine* InEngine, FWindowsWindow* Window, FRenderer& Renderer);
	void Shutdown();
	void OnWindowResized(uint32 Width, uint32 Height);
	void BindDebugCamera(UCameraComponent* DebugCamera);
	void BindPlayerController(APlayerController* PlayerController);
	void ReleaseWorldBinding();
	void SetInputEnabled(bool bEnabled);

	FViewport* GetViewport() { return Viewport; }
	const FViewport* GetViewport() const { return Viewport; }

	UGameViewportClient* GetViewportClient() const { return ViewportClient; }

private:
	bool CreateViewport(FWindowsWindow* Window, FRenderer& Renderer);
	bool CreateViewportClient(FWindowsWindow* Window);

private:
	UGameClientEngine* Engine = nullptr;
	FViewport* Viewport = nullptr;
	UGameViewportClient* ViewportClient = nullptr;
	bool bInputEnabled = false;
};
