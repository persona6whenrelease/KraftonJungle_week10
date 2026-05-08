#pragma once

#include "Engine/Runtime/Engine.h"
#include "GameClient/GameCameraManager.h"
#include "GameClient/GameClientOverlay.h"
#include "GameClient/GameClientSession.h"
#include "GameClient/GameClientSettings.h"
#include "GameClient/GameClientViewport.h"

class UGameClientEngine : public UEngine
{
public:
	DECLARE_CLASS(UGameClientEngine, UEngine)

	UGameClientEngine() = default;
	~UGameClientEngine() override = default;

	void Init(FWindowsWindow* InWindow) override;
	void Shutdown() override;
	void BeginPlay() override;
	void Tick(float DeltaTime) override;
	void OnWindowResized(uint32 Width, uint32 Height) override;

	FGameClientSettings& GetSettings() { return Settings; }
	const FGameClientSettings& GetSettings() const { return Settings; }

	FGameClientSession& GetSession() { return Session; }
	const FGameClientSession& GetSession() const { return Session; }

	FGameClientViewport& GetGameViewport() { return GameViewport; }
	const FGameClientViewport& GetGameViewport() const { return GameViewport; }

	FGameCameraManager& GetCameraManager() { return CameraManager; }
	const FGameCameraManager& GetCameraManager() const { return CameraManager; }

	FGameClientOverlay& GetOverlay() { return Overlay; }

	void RenderOverlay(float DeltaTime);
	bool IsPauseMenuOpen() const { return bPauseMenuOpen; }
	void SetPauseMenuOpen(bool bOpen);
	void TogglePauseMenu();
	void RequestRestart() override;
	void RequestExit() override;

private:
	void TickAlways(float RawDeltaTime);
	void TickInGame(float GameDeltaTime, float RawDeltaTime);
	void ProcessPendingCommands();
	bool RestartGame();
	void InitCameraManager();

private:
	FGameClientSettings Settings;
	FGameClientSession Session;
	FGameCameraManager CameraManager;
	FGameClientViewport GameViewport;
	FGameClientOverlay Overlay;
	bool bPauseMenuOpen = false;
	bool bRestartRequested = false;
};
