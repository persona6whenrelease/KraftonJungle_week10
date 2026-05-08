#pragma once

#include "Engine/Render/Viewport/D3D11ViewportPresenter.h"

class FRenderer;
class FWindowsWindow;
class UGameClientEngine;

class FGameClientOverlay
{
public:
	bool Initialize(FWindowsWindow* Window, FRenderer& Renderer, UGameClientEngine* InEngine);
	void Shutdown();

	void Update(float DeltaTime);
	void Render(float DeltaTime);

private:
	void DrawViewport();
	void DrawOverlay(float DeltaTime);
	void DrawPauseMenu();
	void DrawPauseMainMenu();
	void DrawPauseOptionsMenu();

private:
	UGameClientEngine* Engine = nullptr;
	FWindowsWindow* Window = nullptr;
	FD3D11ViewportPresenter D3D11ViewportPresenter;
	bool bInitialized = false;
	bool bShowingOptions = false;
	bool bUseD3D11ViewportPresenter = false;
};
