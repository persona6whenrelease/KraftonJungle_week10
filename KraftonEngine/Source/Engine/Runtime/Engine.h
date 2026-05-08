#pragma once

#include "Object/Object.h"
#include "GameFramework/World.h"
#include "GameFramework/WorldContext.h"
#include "Render/Pipeline/Renderer.h"
#include "Render/Pipeline/IRenderPipeline.h"
#include "Runtime/TaskScheduler.h"
#include "Sound/SoundManager.h"
#include <memory>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

class FWindowsWindow;
class FTimer;
class UCameraComponent;
class UGameViewportClient;

class UEngine : public UObject
{
public:
	DECLARE_CLASS(UEngine, UObject)

	UEngine() = default;
	~UEngine() override = default;

	// Lifecycle
	virtual void ConfigureWindow(FWindowsWindow* InWindow) {}
	virtual void Init(FWindowsWindow* InWindow);
	virtual void Shutdown();
	virtual void BeginPlay();
	virtual void Tick(float DeltaTime);

	virtual void OnWindowResized(uint32 Width, uint32 Height);
	virtual bool HandleWindowMessage(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);

	// Script-facing game flow requests. Base engine treats them as no-op.
	virtual void RequestRestart() {}
	virtual void RequestExit() {}

	// World context management
	FWorldContext& CreateWorldContext(EWorldType Type, const FName& Handle, const FString& Name = "");
	void DestroyWorldContext(const FName& Handle);

	// World context lookup
	FWorldContext* GetWorldContextFromHandle(const FName& Handle);
	const FWorldContext* GetWorldContextFromHandle(const FName& Handle) const;
	FWorldContext* GetWorldContextFromWorld(const UWorld* World);

	// Active world
	void SetActiveWorld(const FName& Handle);
	FName GetActiveWorldHandle() const { return ActiveWorldHandle; }

	// Accessors
	FWindowsWindow* GetWindow() const { return Window; }
	UWorld* GetWorld() const;
	const TArray<FWorldContext>& GetWorldList() const { return WorldList; }
	TArray<FWorldContext>& GetWorldList() { return WorldList; }

	void SetTimer(FTimer* InTimer) { Timer = InTimer; }
	FTimer* GetTimer() const { return Timer; }

	FRenderer& GetRenderer() { return Renderer; }
	FTaskScheduler& GetTaskScheduler() { return TaskScheduler; }

	// Game Viewport Client — PIE/Standalone 용
	void SetGameViewportClient(UGameViewportClient* InClient) { GameViewportClient = InClient; }
	UGameViewportClient* GetGameViewportClient() const { return GameViewportClient; }

protected:
	void Render(float DeltaTime);
	void SetRenderPipeline(std::unique_ptr<IRenderPipeline> InPipeline);
	IRenderPipeline* GetRenderPipeline() const { return RenderPipeline.get(); }
	void WorldTick(float DeltaTime);

protected:
	FWindowsWindow* Window = nullptr;

	FName ActiveWorldHandle;
	TArray<FWorldContext> WorldList;

	FTimer* Timer = nullptr;

	UGameViewportClient* GameViewportClient = nullptr;
	FRenderer Renderer;
	FTaskScheduler TaskScheduler;

private:
	std::unique_ptr<IRenderPipeline> RenderPipeline;
};

extern UEngine* GEngine;
