#include "Games/Crossy/CrossyGameModule.h"

#include "Engine/Platform/Paths.h"
#include "Games/Crossy/Audio/CrossyAudioIds.h"
#include "Games/Crossy/Components/ParryableProjectileComponent.h"
#include "Games/Crossy/Components/HopMovementComponent.h"
#include "Games/Crossy/Components/ParryComponent.h"
#include "Games/Crossy/Map/RowManager.h"
#include "Games/Crossy/Scripting/CrossyLuaBindings.h"
#include "Runtime/RuntimeModuleManager.h"
#include "Scripting/LuaScriptSubsystem.h"
#include "Scripting/LuaWorldLibrary.h"
#include "Sound/SoundManager.h"
#include "Viewport/GameViewportClient.h"
#include "GameFramework/World.h"

#include <memory>
#include <utility>

namespace
{
	std::unique_ptr<IRuntimeModule> CreateCrossyGameModule()
	{
		return std::make_unique<FCrossyGameModule>();
	}
}

void RegisterCrossyGameModule()
{
	FRuntimeModuleRegistry::RegisterFactory("CrossyGame", &CreateCrossyGameModule);
	(void)UHopMovementComponent::StaticClass();
	(void)UParryComponent::StaticClass();
	(void)UParryableProjectileComponent::StaticClass();
}

void FCrossyGameModule::OnRegister()
{
	FLuaScriptSubsystem::Get().AddBindingRegistrar(&RegisterCrossyLuaBindings);
	FLuaWorldLibrary::RegisterAllowedComponentClass("hopmovement", UHopMovementComponent::StaticClass(), true);
	FLuaWorldLibrary::RegisterAllowedComponentClass("parry", UParryComponent::StaticClass(), true);
}

void FCrossyGameModule::OnEngineInit(FEngineModuleContext& Context)
{
	(void)Context;
	FLuaScriptSubsystem::Get().RegisterScriptDirectoryWatcher("Game/");
	LoadCrossyAudio();
}

void FCrossyGameModule::OnViewportCreated(FViewportModuleContext& Context)
{
	if (!Context.Window || !Context.Renderer || !Context.ViewportClient)
	{
		return;
	}

	if (!GameUi.Initialize(Context.Window, *Context.Renderer, Context.ViewportClient))
	{
		return;
	}

	FCrossyGameUiCallbacks Callbacks;
	Callbacks.ExecuteCommand = std::move(Context.UiCommands.ExecuteCommand);
	Callbacks.QueryBool = std::move(Context.UiCommands.QueryBool);
	Callbacks.SetBool = std::move(Context.UiCommands.SetBool);
	GameUi.SetCallbacks(std::move(Callbacks));

	Context.ViewportClient->SetUiLayer(&GameUi);
}

void FCrossyGameModule::OnWorldCreated(UWorld* World)
{
	if (!World || (World->GetWorldType() != EWorldType::Game && World->GetWorldType() != EWorldType::PIE))
	{
		return;
	}

	if (!bRowManagerInitialized)
	{
		FRowManager::Get().Initialize(World);
		bRowManagerInitialized = true;
	}
}

void FCrossyGameModule::OnPreWorldReset(UWorld* World)
{
	(void)World;
	if (bRowManagerInitialized)
	{
		FRowManager::Get().Shutdown(true);
		bRowManagerInitialized = false;
	}
}

void FCrossyGameModule::OnPostWorldReset(UWorld* World)
{
	OnWorldCreated(World);
}

void FCrossyGameModule::OnShutdown()
{
	GameUi.Shutdown();
	if (bRowManagerInitialized)
	{
		FRowManager::Get().Shutdown(true);
		bRowManagerInitialized = false;
	}
	ClearCrossyLuaUiEventHandler();
}

void FCrossyGameModule::LoadCrossyAudio()
{
	FSoundManager::Get().LoadMusic(CrossyAudioIds::BGM, FPaths::Combine(FPaths::AssetDir(), L"Sound/BackgroundMusic.wav"));
	FSoundManager::Get().LoadEffect(CrossyAudioIds::Jump, FPaths::Combine(FPaths::AssetDir(), L"Sound/Jump.wav"));
	FSoundManager::Get().LoadEffect(CrossyAudioIds::Death, FPaths::Combine(FPaths::AssetDir(), L"Sound/Death.wav"));
	FSoundManager::Get().LoadEffect(CrossyAudioIds::Parry, FPaths::Combine(FPaths::AssetDir(), L"Sound/Parry.wav"));
	FSoundManager::Get().LoadEffect(CrossyAudioIds::Dash, FPaths::Combine(FPaths::AssetDir(), L"Sound/Dash.wav"));
	FSoundManager::Get().LoadEffect(CrossyAudioIds::Crash, FPaths::Combine(FPaths::AssetDir(), L"Sound/Crash.wav"));
}
