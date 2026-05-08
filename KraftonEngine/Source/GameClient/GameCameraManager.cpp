#include "GameClient/GameCameraManager.h"

#include "Component/CameraComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "GameFramework/PlayerController.h"
#include "Object/ObjectFactory.h"
#include "Object/Object.h"

void FGameCameraManager::SetWorld(UWorld* InWorld)
{
	World = InWorld;
}

void FGameCameraManager::ClearWorldBinding()
{
	World = nullptr;
	PlayerController = nullptr;
	DebugCamera = nullptr;
	StartupGameplayCamera = nullptr;
	ViewCamera = nullptr;
}

bool FGameCameraManager::CreateDebugCamera()
{
	// Debug camera actors are no longer spawned during startup.
	// The gameplay camera path is PlayerController -> APlayerCameraManager -> OutputCamera.
	DebugCamera = nullptr;
	return false;
}

bool FGameCameraManager::FindStartupGameplayCamera()
{
	if (!World)
	{
		return false;
	}

	UCameraComponent* Existing = World->ResolveGameplayViewCamera(PlayerController);
	if (!Existing)
	{
		return false;
	}

	StartupGameplayCamera = Existing;
	World->SetActiveCamera(Existing);
	World->SetViewCamera(Existing);
	return true;
}

bool FGameCameraManager::CreateFallbackGameplayCamera()
{
	StartupGameplayCamera = nullptr;
	return false;
}

void FGameCameraManager::SetDebugFreeCameraEnabled(bool bEnabled)
{
	bDebugFreeCameraEnabled = bEnabled;
}

void FGameCameraManager::SetActiveCamera(UCameraComponent* Camera)
{
	if (World)
	{
		World->SetActiveCamera(Camera);
	}
}

UCameraComponent* FGameCameraManager::GetActiveCamera() const
{
	return World ? World->GetActiveCamera() : nullptr;
}

UCameraComponent* FGameCameraManager::ResolveViewCamera() const
{
	if (bDebugFreeCameraEnabled && IsAliveObject(DebugCamera))
	{
		return DebugCamera;
	}

	if (World)
	{
		if (UCameraComponent* GameplayCamera = World->ResolveGameplayViewCamera(PlayerController))
		{
			return GameplayCamera;
		}
	}

	return IsAliveObject(DebugCamera) ? DebugCamera : nullptr;
}


UCameraComponent* FGameCameraManager::GetViewCamera() const
{
	if (World)
	{
		if (UCameraComponent* Camera = World->GetViewCamera())
		{
			return Camera;
		}
	}
	return IsAliveObject(ViewCamera) ? ViewCamera : nullptr;
}

void FGameCameraManager::SyncWorldViewCamera()
{
	ViewCamera = ResolveViewCamera();

	if (World)
	{
		World->SetViewCamera(ViewCamera);
	}
}
