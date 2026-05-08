#include "GameClient/GameClientRenderPipeline.h"

#include "GameClient/GameClientEngine.h"
#include "GameClient/GameClientViewport.h"
#include "Component/CameraComponent.h"
#include "GameFramework/World.h"
#include "Render/Command/DrawCommandBuilder.h"
#include "Render/Pipeline/Renderer.h"
#include "Render/Scene/FScene.h"
#include "Viewport/Viewport.h"

FGameClientRenderPipeline::FGameClientRenderPipeline(
	UGameClientEngine* InEngine,
	FRenderer& InRenderer)
	: Engine(InEngine)
{
	(void)InRenderer;
}

void FGameClientRenderPipeline::Execute(float DeltaTime, FRenderer& Renderer)
{
	RenderGameViewport(Renderer);

	Renderer.BeginFrame();
	if (Engine)
	{
		Engine->RenderOverlay(DeltaTime);
	}
	Renderer.EndFrame();
}

void FGameClientRenderPipeline::RenderGameViewport(FRenderer& Renderer)
{
	if (!Engine)
	{
		return;
	}

	UWorld* World = Engine->GetWorld();
	if (!World)
	{
		return;
	}

	FViewport* Viewport = Engine->GetGameViewport().GetViewport();
	if (!Viewport)
	{
		return;
	}

	UCameraComponent* Camera = Engine->GetCameraManager().ResolveViewCamera();
	if (!Camera)
	{
		Camera = Engine->GetCameraManager().GetViewCamera();
	}
	if (!Camera)
	{
		return;
	}

	ID3D11DeviceContext* Context = Renderer.GetFD3DDevice().GetDeviceContext();
	if (!Context)
	{
		return;
	}

	if (Viewport->ApplyPendingResize())
	{
		Camera->OnResize(
			static_cast<int32>(Viewport->GetWidth()),
			static_cast<int32>(Viewport->GetHeight()));
	}

	const float ClearColor[4] = { 0.02f, 0.02f, 0.025f, 1.0f };
	Viewport->BeginRender(Context, ClearColor);

	Frame.ClearViewportResources();
	Frame.SetCameraInfo(Camera);

	FViewportRenderOptions Options = Engine->GetSettings().RenderOptions;

	Frame.SetRenderOptions(Options);
	Frame.SetViewportInfo(Viewport);
	Frame.LODContext = World->PrepareLODContext(Camera);

	FScene& Scene = World->GetScene();
	Scene.ClearFrameData();

	FDrawCommandBuilder& Builder = Renderer.GetBuilder();
	Builder.BeginCollect(Frame, Scene.GetProxyCount());

	FCollectOutput Output;
	Collector.Collect(World, Frame, Output);
	Collector.CollectDebugDraw(Frame, Scene);

	Builder.BuildCommands(Frame, &Scene, Output);
	Renderer.Render(Frame, Scene);
}
