#include "GameClient/GameClientEngine.h"

#include "GameClient/GameClientRenderPipeline.h"
#include "GameClient/GameClientPackageValidator.h"
#include "GameClient/LinkedRuntimeModules.h"
#include "Core/Notification.h"
#include "Engine/Platform/Paths.h"
#include "Engine/Input/GameplayInputRouter.h"
#include "Engine/Input/InputFrame.h"
#include "Engine/Input/InputSystem.h"
#include "Engine/Platform/DirectoryWatcher.h"
#include "Engine/Runtime/WindowsWindow.h"
#include "GameFramework/PlayerController.h"
#include "Runtime/ActorPoolSystem.h"
#include "Runtime/EngineFactory.h"
#include "Object/ObjectFactory.h"
#include "GameFramework/World.h"

IMPLEMENT_CLASS(UGameClientEngine, UEngine)

namespace
{
	UEngine* CreateGameClientEngine()
	{
		return UObjectManager::Get().CreateObject<UGameClientEngine>();
	}

	FEngineFactoryRegistrar GGameClientEngineRegistrar("GameClient", &CreateGameClientEngine);
}

void UGameClientEngine::InitCameraManager()
{
	UWorld* World = GetWorld();
	CameraManager.SetWorld(World);

	APlayerController* PlayerController = World ? World->FindOrCreatePlayerController() : nullptr;
	CameraManager.SetPlayerController(PlayerController);

	CameraManager.CreateDebugCamera();

	if (World)
	{
		World->AutoWirePlayerController(PlayerController);
	}

	CameraManager.FindStartupGameplayCamera();

	if (World)
	{
		World->AutoWirePlayerController(PlayerController);
	}
	CameraManager.SyncWorldViewCamera();
}

void UGameClientEngine::Init(FWindowsWindow* InWindow)
{
	Settings.Load();

	FString PackageValidationErrors;
	if (!FGameClientPackageValidator::ValidateBeforeEngineInit(Settings, PackageValidationErrors))
	{
		::MessageBoxA(
			InWindow ? InWindow->GetHWND() : nullptr,
			PackageValidationErrors.c_str(),
			"GameClient package validation failed",
			MB_OK | MB_ICONERROR);
		::PostQuitMessage(1);
		return;
	}

	RegisterLinkedRuntimeModules();

	if (InWindow)
	{
		InWindow->SetTitle(FPaths::ToWide(Settings.WindowTitle).c_str());
		InWindow->ResizeClient(static_cast<unsigned int>(Settings.WindowWidth), static_cast<unsigned int>(Settings.WindowHeight));
		if (Settings.bFullscreen && !InWindow->IsFullscreen())
		{
			InWindow->ToggleFullscreen();
		}
	}

	GetRuntimeModules().LoadModules(Settings.RuntimeModules);

	UEngine::Init(InWindow);

	if (!Session.Initialize(this))
	{
		::MessageBoxA(
			InWindow ? InWindow->GetHWND() : nullptr,
			"Failed to initialize GameClient session.",
			"GameClient startup failed",
			MB_OK | MB_ICONERROR);
		::PostQuitMessage(1);
		return;
	}

	GetRuntimeModules().OnWorldCreated(GetWorld());

	InitCameraManager();

	GameViewport.Initialize(this, InWindow, Renderer);
	GameViewport.BindPlayerController(CameraManager.GetPlayerController());
	GameViewport.BindDebugCamera(CameraManager.GetDebugCamera());

	Overlay.Initialize(InWindow, Renderer, this);

	SetGameViewportClient(GameViewport.GetViewportClient());
	SetRenderPipeline(std::make_unique<FGameClientRenderPipeline>(this, Renderer));
}

void UGameClientEngine::Shutdown()
{
	FSoundManager::Get().StopBGM();
	CameraManager.ClearWorldBinding();
	Overlay.Shutdown();
	SetGameViewportClient(nullptr);
	GameViewport.Shutdown();
	Session.Shutdown();

	UEngine::Shutdown();
}

void UGameClientEngine::BeginPlay()
{
	UEngine::BeginPlay();
	FSoundManager::Get().PlayBGM();
}

void UGameClientEngine::Tick(float DeltaTime)
{
	const float RawDeltaTime = DeltaTime;
	GetTimeManager().Update(RawDeltaTime);

	TickAlways(RawDeltaTime);

	if (!bPauseMenuOpen)
	{
		const float GameDeltaTime = GetTimeManager().GetGameDeltaTime();
		TickInGame(GameDeltaTime, RawDeltaTime);
	}

	Render(RawDeltaTime);
}

void UGameClientEngine::OnWindowResized(uint32 Width, uint32 Height)
{
	UEngine::OnWindowResized(Width, Height);
	GameViewport.OnWindowResized(Width, Height);
}

void UGameClientEngine::RenderOverlay(float DeltaTime)
{
	Overlay.Render(DeltaTime);
}

void UGameClientEngine::SetPauseMenuOpen(bool bOpen)
{
	if (bPauseMenuOpen == bOpen)
	{
		return;
	}

	bPauseMenuOpen = bOpen;
	GameViewport.SetInputEnabled(!bPauseMenuOpen);
	InputSystem::Get().ResetTransientState();
}

void UGameClientEngine::TogglePauseMenu()
{
	SetPauseMenuOpen(!bPauseMenuOpen);
}

void UGameClientEngine::RequestRestart()
{
	bRestartRequested = true;
	SetPauseMenuOpen(false);
}

void UGameClientEngine::RequestExit()
{
	if (Window && Window->GetHWND())
	{
		::PostMessage(Window->GetHWND(), WM_CLOSE, 0, 0);
		return;
	}

	::PostQuitMessage(0);
}

void UGameClientEngine::TickAlways(float RawDeltaTime)
{
	FDirectoryWatcher::Get().ProcessChanges();
	FNotificationManager::Get().Tick(RawDeltaTime);

	ProcessPendingCommands();

	InputSystem::Get().Tick();
	FInputFrame GlobalInputFrame(InputSystem::Get().MakeSnapshot());
	const FInputSystemSnapshot& RawInput = GlobalInputFrame.GetRawSnapshotForGlobalShortcuts();
	if (RawInput.WasPressed(VK_ESCAPE))
	{
		TogglePauseMenu();
		GlobalInputFrame.ConsumeKey(VK_ESCAPE, "GameClientGlobalShortcut", "Toggle pause menu");
	}

	Overlay.Update(RawDeltaTime);
	GameViewport.SetInputEnabled(!bPauseMenuOpen);
}

void UGameClientEngine::TickInGame(float GameDeltaTime, float RawDeltaTime)
{
    FInputFrame InputFrame(InputSystem::Get().MakeSnapshot());

    FGameplayInputRouteContext InputContext;
    InputContext.World = GetWorld();
    InputContext.ViewportClient = GameViewport.GetViewportClient();
    InputContext.DeltaTime = GameDeltaTime;
	
    FGameplayInputRouter::Route(InputFrame, InputContext);

    TaskScheduler.Tick(GameDeltaTime);
    WorldTick(GameDeltaTime, RawDeltaTime);
	GetRuntimeModules().OnTick(GameDeltaTime);

    CameraManager.SyncWorldViewCamera();
}

void UGameClientEngine::ProcessPendingCommands()
{
	if (bRestartRequested)
	{
		bRestartRequested = false;
		RestartGame();
	}
}

bool UGameClientEngine::RestartGame()
{
	CameraManager.ClearWorldBinding();
	GameViewport.ReleaseWorldBinding();

	TaskScheduler.Clear();

	GetRuntimeModules().OnPreWorldReset(GetWorld());

	// 재시작에서는 풀 반환이 아니라 월드에서 실제 삭제

	// 풀에 남은 비활성 액터까지 제거
	FActorPoolSystem::Get().Shutdown();

	if (!Session.Restart())
	{
		return false;
	}

	GetRuntimeModules().OnPostWorldReset(GetWorld());

	InitCameraManager();
	GameViewport.BindPlayerController(CameraManager.GetPlayerController());
	GameViewport.BindDebugCamera(CameraManager.GetDebugCamera());

	BeginPlay();
	return true;
}
