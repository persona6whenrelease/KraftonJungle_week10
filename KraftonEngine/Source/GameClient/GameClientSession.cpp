#include "GameClient/GameClientSession.h"

#include "GameClient/GameClientEngine.h"
#include "GameFramework/World.h"
#include "Object/ObjectFactory.h"
#include "Platform/Paths.h"
#include "Serialization/SceneSaveManager.h"

#include <filesystem>

namespace
{
	FWorldContext MakeGameWorldContext(UWorld* World, const FName& Handle)
	{
		FWorldContext Context;
		Context.WorldType = EWorldType::Game;
		Context.ContextHandle = Handle;
		Context.ContextName = "GameClient";
		Context.World = World;
		if (Context.World)
		{
			Context.World->SetWorldType(EWorldType::Game);
		}
		return Context;
	}
}

bool FGameClientSession::Initialize(UGameClientEngine* InEngine)
{
	Engine = InEngine;
	if (!Engine)
	{
		return false;
	}

	const FGameClientSettings& Settings = Engine->GetSettings();
	std::wstring SceneDiskPath;
	FString Error;
	if (!FPaths::TryResolvePackagePath(Settings.StartupScenePackagePath, SceneDiskPath, &Error))
	{
		return false;
	}

	if (!std::filesystem::exists(std::filesystem::path(SceneDiskPath)))
	{
		return false;
	}

	FWorldContext LoadedContext;
	FPerspectiveCameraData CameraData;
	FSceneSaveManager::LoadSceneFromJSON(FPaths::ToUtf8(SceneDiskPath), LoadedContext, CameraData);

	if (!LoadedContext.World)
	{
		return false;
	}

	UWorld* GameWorld = LoadedContext.World->DuplicateAs(EWorldType::Game);
	LoadedContext.World->EndPlay();
	UObjectManager::Get().DestroyObject(LoadedContext.World);
	LoadedContext.World = nullptr;

	if (!GameWorld)
	{
		return false;
	}

	Engine->GetWorldList().push_back(MakeGameWorldContext(GameWorld, WorldHandle));
	Engine->SetActiveWorld(WorldHandle);
	World = GameWorld;
	return true;
}

bool FGameClientSession::Restart()
{
	if (!Engine)
	{
		return false;
	}

	UGameClientEngine* Owner = Engine;
	DestroyWorld();
	return Initialize(Owner);
}

void FGameClientSession::Shutdown()
{
	DestroyWorld();
	World = nullptr;
	Engine = nullptr;
}

void FGameClientSession::DestroyWorld()
{
	if (Engine && World)
	{
		Engine->DestroyWorldContext(WorldHandle);
	}
	World = nullptr;
}
