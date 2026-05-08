#include "Engine/Runtime/Engine.h"

#include "Platform/Paths.h"
#include "Core/Log.h"
#include "Core/Notification.h"
#include "Engine/Platform/DirectoryWatcher.h"
#include "Profiling/Stats.h"
#include "Profiling/StartupProfiler.h"
#include "Engine/Input/InputSystem.h"
#include "Engine/Runtime/WindowsWindow.h"
#include "Resource/ResourceManager.h"
#include "Render/Pipeline/DefaultRenderPipeline.h"
#include "Render/Resource/MeshBufferManager.h"
#include "Mesh/ObjManager.h"
#include "Texture/Texture2D.h"
#include "GameFramework/World.h"
#include "GameFramework/AActor.h"
#include "Core/TickFunction.h"
#include "Scripting/LuaScriptSubsystem.h"
#include "Viewport/GameViewportClient.h"

DEFINE_CLASS(UEngine, UObject)

UEngine* GEngine = nullptr;

namespace
{
	ELevelTick ToLevelTickType(EWorldType WorldType)
	{
		switch (WorldType)
		{
		case EWorldType::Editor:
			return ELevelTick::LEVELTICK_ViewportsOnly;
		case EWorldType::PIE:
		case EWorldType::Game:
			return ELevelTick::LEVELTICK_All;
		default:
			return ELevelTick::LEVELTICK_TimeOnly;
		}
	}
}

void UEngine::Init(FWindowsWindow* InWindow)
{
	Window = InWindow;

	// 싱글턴 초기화 순서 보장
	FNamePool::Get();
	FObjectFactory::Get();

	InputSystem::Get().SetOwnerWindow(Window->GetHWND());

	{
		SCOPE_STARTUP_STAT("Renderer::Create");
		Renderer.Create(Window->GetHWND());
	}

	ID3D11Device* Device = Renderer.GetFD3DDevice().GetDevice();

	{
		SCOPE_STARTUP_STAT("MeshBufferManager::Init");
		FMeshBufferManager::Get().Initialize(Device);
	}

	{
		SCOPE_STARTUP_STAT("ResourceManager::LoadFromFile");
		FResourceManager::Get().LoadFromFile(FPaths::ToUtf8(FPaths::ResourceFilePath()), Device);
	}

	{
		SCOPE_STARTUP_STAT("ResourceManager::LoadFromDir");
		FResourceManager::Get().LoadFromDirectory(FPaths::ToUtf8(FPaths::RootDir()), Device);
	}

	{
		SCOPE_STARTUP_STAT("RenderPipeline::Create");
		SetRenderPipeline(std::make_unique<FDefaultRenderPipeline>(this, Renderer));
	}

	FLogManager::Get().Initialize();
	FDirectoryWatcher::Get().Initialize();
	FLuaScriptSubsystem::Get().Initialize();
	FSoundManager::Get().initialize();

	FEngineModuleContext ModuleContext;
	ModuleContext.Engine = this;
	ModuleContext.Window = Window;
	RuntimeModules.OnEngineInit(ModuleContext);
}

void UEngine::Shutdown()
{
	TaskScheduler.Clear();
	RuntimeModules.UnloadModules();
	FLuaScriptSubsystem::Get().Shutdown();
	FDirectoryWatcher::Get().Shutdown();
	FLogManager::Get().Shutdown();
	RenderPipeline.reset();
	FResourceManager::Get().ReleaseGPUResources();
	UTexture2D::ReleaseAllGPU();
	FObjManager::ReleaseAllGPU();
	FMeshBufferManager::Get().Release();
	Renderer.Release();
}

void UEngine::BeginPlay()
{
	FWorldContext* Context = GetWorldContextFromHandle(ActiveWorldHandle);
	if (Context && Context->World)
	{
		if (Context->WorldType == EWorldType::Game || Context->WorldType == EWorldType::PIE)
		{
			Context->World->BeginPlay();
			RuntimeModules.OnBeginPlay(Context->World);
		}
	}
}

void UEngine::Tick(float DeltaTime)
{
	const float RawDeltaTime = DeltaTime;
	TimeManager.Update(RawDeltaTime);
	const float GameDeltaTime = TimeManager.GetGameDeltaTime();

	FDirectoryWatcher::Get().ProcessChanges();
	FNotificationManager::Get().Tick(RawDeltaTime);
	InputSystem::Get().Tick();
	TaskScheduler.Tick(RawDeltaTime);
	WorldTick(GameDeltaTime, RawDeltaTime);
	Render(RawDeltaTime);
}

void UEngine::Render(float DeltaTime)
{
	if (RenderPipeline)
	{
		SCOPE_STAT_CAT("UEngine::Render", "2_Render");
		RenderPipeline->Execute(DeltaTime, Renderer);
	}
}

void UEngine::SetRenderPipeline(std::unique_ptr<IRenderPipeline> InPipeline)
{
	RenderPipeline = std::move(InPipeline);
}


bool UEngine::HandleWindowMessage(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	if (GameViewportClient)
	{
		if (IViewportUiLayer* UiLayer = GameViewportClient->GetUiLayer())
		{
			return UiLayer->ProcessWin32Message(hWnd, Msg, wParam, lParam);
		}
	}
	return false;
}

void UEngine::OnWindowResized(uint32 Width, uint32 Height)
{
	if (Width == 0 || Height == 0)
	{
		return;
	}

	Renderer.GetFD3DDevice().OnResizeViewport(Width, Height);
	Renderer.ResetRenderStateCache();
}

void UEngine::WorldTick(float GameDeltaTime, float RawDeltaTime)
{
	SCOPE_STAT_CAT("UEngine::WorldTick", "1_WorldTick");

	// PIE 활성 시 Editor 월드는 sleep (UE 동작과 동일).
	// culling/octree/visibility 갱신을 건너뛰어 50k+ 환경에서 비용 2배를 방지.
	bool bHasPIEWorld = false;
	for (const FWorldContext& Ctx : WorldList)
	{
		if (Ctx.WorldType == EWorldType::PIE && Ctx.World)
		{
			bHasPIEWorld = true;
			break;
		}
	}

	// 월드 타입별 Tick 라우팅:
	// - Editor: bTickInEditor 액터만 TickManager 대상
	// - PIE/Game: BeginPlay 이후 bNeedsTick 액터만 TickManager 대상
	// - 기타:   시간 갱신만 유지
	for (FWorldContext& Ctx : WorldList)
	{
		UWorld* World = Ctx.World;
		if (!World) continue;

		// PIE 활성 시 Editor 월드는 완전히 skip
		if (bHasPIEWorld && Ctx.WorldType == EWorldType::Editor)
		{
			continue;
		}

		const ELevelTick TickType = ToLevelTickType(Ctx.WorldType);

		// 월드 단위 업데이트 (FlushPrimitive / VisibleProxies / DebugDraw /s TickManager)
		World->Tick(GameDeltaTime, RawDeltaTime, TickType);
	}
}

UWorld* UEngine::GetWorld() const
{
	const FWorldContext* Context = GetWorldContextFromHandle(ActiveWorldHandle);
	return Context ? Context->World : nullptr;
}

FWorldContext& UEngine::CreateWorldContext(EWorldType Type, const FName& Handle, const FString& Name)
{
	FWorldContext Context;
	Context.WorldType = Type;
	Context.ContextHandle = Handle;
	Context.ContextName = Name.empty() ? Handle.ToString() : Name;
	Context.World = UObjectManager::Get().CreateObject<UWorld>();
	if (Context.World)
	{
		Context.World->SetWorldType(Type);
	}
	WorldList.push_back(Context);
	RuntimeModules.OnWorldCreated(WorldList.back().World);
	return WorldList.back();
}

void UEngine::DestroyWorldContext(const FName& Handle)
{
	for (auto it = WorldList.begin(); it != WorldList.end(); ++it)
	{
		if (it->ContextHandle == Handle)
		{
			it->World->EndPlay();
			UObjectManager::Get().DestroyObject(it->World);
			WorldList.erase(it);
			return;
		}
	}
}

FWorldContext* UEngine::GetWorldContextFromHandle(const FName& Handle)
{
	for (FWorldContext& Ctx : WorldList)
	{
		if (Ctx.ContextHandle == Handle)
		{
			return &Ctx;
		}
	}
	return nullptr;
}

const FWorldContext* UEngine::GetWorldContextFromHandle(const FName& Handle) const
{
	for (const FWorldContext& Ctx : WorldList)
	{
		if (Ctx.ContextHandle == Handle)
		{
			return &Ctx;
		}
	}
	return nullptr;
}

FWorldContext* UEngine::GetWorldContextFromWorld(const UWorld* World)
{
	for (FWorldContext& Ctx : WorldList)
	{
		if (Ctx.World == World)
		{
			return &Ctx;
		}
	}
	return nullptr;
}

void UEngine::SetActiveWorld(const FName& Handle)
{
	ActiveWorldHandle = Handle;
}
