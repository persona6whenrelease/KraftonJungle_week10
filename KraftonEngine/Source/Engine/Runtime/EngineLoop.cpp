#include "Engine/Runtime/EngineLoop.h"
#include "Engine/Runtime/LoadingScreen.h"
#include "Profiling/StartupProfiler.h"

#include "Runtime/EngineFactory.h"
#include "Object/ObjectFactory.h"

void FEngineLoop::CreateEngine()
{
#if IS_GAME_CLIENT
	const FString EngineFactoryName = "GameClient";
#elif IS_OBJ_VIEWER
	const FString EngineFactoryName = "ObjViewer";
#elif WITH_EDITOR
	const FString EngineFactoryName = "Editor";
#else
	const FString EngineFactoryName = "Engine";
#endif

	GEngine = FEngineFactoryRegistry::Create(EngineFactoryName);
	if (!GEngine)
	{
		GEngine = UObjectManager::Get().CreateObject<UEngine>();
	}
}

bool FEngineLoop::Init(HINSTANCE hInstance, int nShowCmd)
{
	{
		SCOPE_STARTUP_STAT("WindowsApplication::Init");
		if (!Application.Init(hInstance))
		{
			return false;
		}
	}

	Application.SetOnSizingCallback([this]()
		{
			Timer.Tick();
			if (GEngine)
			{
				GEngine->Tick(Timer.GetDeltaTime());
			}
		});

	Application.SetOnResizedCallback([](unsigned int Width, unsigned int Height)
		{
			if (GEngine)
			{
				GEngine->OnWindowResized(Width, Height);
			}
		});

	CreateEngine();

	// 엔진별 윈도우 설정(해상도, 풀스크린 등)을 로딩 화면 전에 적용
	GEngine->ConfigureWindow(&Application.GetWindow());

	FLoadingScreen LoadingScreen;
	LoadingScreen.Begin(Application.GetWindow().GetHWND());

	LoadingScreen.Update(L"리소스 로딩 중...");
	{
		SCOPE_STARTUP_STAT("Engine::Init");
		GEngine->Init(&Application.GetWindow());
	}

	GEngine->SetTimer(&Timer);

	LoadingScreen.Update(L"씬 불러오는 중...");
	{
		SCOPE_STARTUP_STAT("Engine::BeginPlay");
		GEngine->BeginPlay();
	}

	LoadingScreen.End();

	Timer.Initialize();
	FStartupProfiler::Get().Finish();

	return true;
}

int FEngineLoop::Run()
{
	while (!Application.IsExitRequested())
	{
		Application.PumpMessages();

		if (Application.IsExitRequested())
		{
			break;
		}

		Timer.Tick();
		GEngine->Tick(Timer.GetDeltaTime());
	}

	return 0;
}

void FEngineLoop::Shutdown()
{
	if (GEngine)
	{
		GEngine->Shutdown();
		UObjectManager::Get().DestroyObject(GEngine);
		GEngine = nullptr;
	}
}
