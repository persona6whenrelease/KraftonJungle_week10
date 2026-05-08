#pragma once

class UWorld;
class UCameraComponent;
class APlayerController;

class FGameCameraManager
{
public:
	void SetWorld(UWorld* InWorld);
	void ClearWorldBinding();

	bool CreateDebugCamera();
	bool FindStartupGameplayCamera();
	bool CreateFallbackGameplayCamera();

	void SetPlayerController(APlayerController* InController) { PlayerController = InController; }
	APlayerController* GetPlayerController() const { return PlayerController; }

	UCameraComponent* GetDebugCamera() const { return DebugCamera; }

	void SetDebugFreeCameraEnabled(bool bEnabled);
	bool IsDebugFreeCameraEnabled() const { return bDebugFreeCameraEnabled; }

	void SetActiveCamera(UCameraComponent* Camera);
	UCameraComponent* GetActiveCamera() const;

	UCameraComponent* ResolveViewCamera() const;
	void SyncWorldViewCamera();

	UCameraComponent* GetViewCamera() const;

private:
	UWorld* World = nullptr;
	APlayerController* PlayerController = nullptr;

	UCameraComponent* DebugCamera = nullptr;
	UCameraComponent* StartupGameplayCamera = nullptr;
	UCameraComponent* ViewCamera = nullptr;

	bool bDebugFreeCameraEnabled = false;
};
