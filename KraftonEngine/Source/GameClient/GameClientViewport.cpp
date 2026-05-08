#include "GameClient/GameClientViewport.h"

#include "GameClient/GameClientEngine.h"
#include "Component/CameraComponent.h"
#include "Engine/Runtime/WindowsWindow.h"
#include "Object/ObjectFactory.h"
#include "Render/Pipeline/Renderer.h"
#include "Viewport/GameViewportClient.h"
#include "Viewport/Viewport.h"
#include "Viewport/ViewportPresentationTypes.h"

#include <utility>

bool FGameClientViewport::Initialize(
	UGameClientEngine* InEngine,
	FWindowsWindow* Window,
	FRenderer& Renderer)
{
	Engine = InEngine;
	if (!Engine || !Window)
	{
		return false;
	}

	if (!CreateViewport(Window, Renderer))
	{
		return false;
	}

	if (!CreateViewportClient(Window))
	{
		return false;
	}

	Viewport->SetClient(ViewportClient);
	ViewportClient->SetViewport(Viewport);
	ViewportClient->SetOwnerWindow(Window->GetHWND());
	const FViewportPresentationRect InitialPresentationRect(
		0.0f,
		0.0f,
		static_cast<float>(Window->GetWidth()),
		static_cast<float>(Window->GetHeight()));
	ViewportClient->SetPresentationRect(InitialPresentationRect);
	ViewportClient->SetCursorClipRect(InitialPresentationRect);

	FViewportModuleContext ModuleContext;
	ModuleContext.Engine = InEngine;
	ModuleContext.Window = Window;
	ModuleContext.Renderer = &Renderer;
	ModuleContext.ViewportClient = ViewportClient;
	ModuleContext.UiCommands.ExecuteCommand = [InEngine, Window](const FString& CommandName)
	{
		if (CommandName == "Viewport.Resume" || CommandName == "Viewport.ClosePauseMenu")
		{
			if (InEngine)
			{
				InEngine->SetPauseMenuOpen(false);
			}
		}
		else if (CommandName == "Application.RestartSession")
		{
			if (InEngine)
			{
				InEngine->RequestRestart();
			}
		}
		else if (CommandName == "Application.Exit")
		{
			if (InEngine)
			{
				InEngine->RequestExit();
			}
		}
		else if (CommandName == "Application.ToggleFullscreen")
		{
			if (Window)
			{
				Window->ToggleFullscreen();
			}
		}
	};
	ModuleContext.UiCommands.QueryBool = [InEngine, Window](const FString& StateName) -> bool
	{
		if (StateName == "Application.Fullscreen")
		{
			return Window && Window->IsFullscreen();
		}
		if (StateName == "Render.FXAA")
		{
			return InEngine && InEngine->GetSettings().RenderOptions.ShowFlags.bFXAA;
		}
		return false;
	};
	ModuleContext.UiCommands.SetBool = [InEngine](const FString& StateName, bool bValue)
	{
		if (StateName == "Render.FXAA" && InEngine)
		{
			InEngine->GetSettings().RenderOptions.ShowFlags.bFXAA = bValue;
		}
	};
	InEngine->GetRuntimeModules().OnViewportCreated(ModuleContext);

	SetInputEnabled(true);

	return true;
}

bool FGameClientViewport::CreateViewport(FWindowsWindow* Window, FRenderer& Renderer)
{
	Viewport = new FViewport();
	return Viewport->Initialize(
		Renderer.GetFD3DDevice().GetDevice(),
		static_cast<uint32>(Window->GetWidth()),
		static_cast<uint32>(Window->GetHeight()));
}

bool FGameClientViewport::CreateViewportClient(FWindowsWindow* Window)
{
	ViewportClient = UObjectManager::Get().CreateObject<UGameViewportClient>();
	if (ViewportClient && Window)
	{
		ViewportClient->SetOwnerWindow(Window->GetHWND());
	}
	return ViewportClient != nullptr;
}

void FGameClientViewport::BindDebugCamera(UCameraComponent* DebugCamera)
{
	if (ViewportClient)
	{
		ViewportClient->SetDrivingCamera(DebugCamera);
		ViewportClient->SetPossessed(bInputEnabled);
	}
}

void FGameClientViewport::BindPlayerController(APlayerController* PlayerController)
{
	if (ViewportClient)
	{
		ViewportClient->SetPlayerController(PlayerController);
	}
}

void FGameClientViewport::ReleaseWorldBinding()
{
	if (ViewportClient)
	{
		ViewportClient->SetPossessed(false);
		ViewportClient->SetPlayerController(nullptr);
		ViewportClient->SetDrivingCamera(nullptr);
	}
}

void FGameClientViewport::SetInputEnabled(bool bEnabled)
{
	if (bInputEnabled == bEnabled)
	{
		return;
	}

	bInputEnabled = bEnabled;
	if (ViewportClient)
	{
		ViewportClient->SetPossessed(bInputEnabled);
	}
}

void FGameClientViewport::OnWindowResized(uint32 Width, uint32 Height)
{
	if (Viewport)
	{
		Viewport->RequestResize(Width, Height);
	}

	if (ViewportClient)
	{
		const FViewportPresentationRect PresentationRect(
			0.0f,
			0.0f,
			static_cast<float>(Width),
			static_cast<float>(Height));
		ViewportClient->SetPresentationRect(PresentationRect);
		ViewportClient->SetCursorClipRect(PresentationRect);
	}
}

void FGameClientViewport::Shutdown()
{
	if (ViewportClient)
	{
		ViewportClient->OnEndPIE();
		UObjectManager::Get().DestroyObject(ViewportClient);
		ViewportClient = nullptr;
	}

	if (Viewport)
	{
		Viewport->Release();
		delete Viewport;
		Viewport = nullptr;
	}

	Engine = nullptr;
}
