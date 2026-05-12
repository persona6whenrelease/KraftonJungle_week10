#include "Render/Snapshot/SnapShotRenderer.h"

#include "Component/PrimitiveComponent.h"
#include "GameFramework/AActor.h"
#include "Object/Object.h"
#include "Render/Command/DrawCommandBuilder.h"
#include "Render/Command/DrawCommandList.h"
#include "Render/Device/D3DDevice.h"
#include "Render/Pipeline/RenderCollector.h"
#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Render/Scene/FScene.h"
#include "Render/Types/FrameContext.h"
#include "Render/Types/GlobalLightParams.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float SnapshotFovY = 0.70f;
	constexpr float SnapshotPadding = 1.18f;
	constexpr float SnapshotMinRadius = 0.5f;

	struct FScopedD3DRenderState
	{
		ID3D11DeviceContext* Context = nullptr;
		ID3D11RenderTargetView* RTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
		ID3D11DepthStencilView* DSV = nullptr;
		D3D11_VIEWPORT Viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
		UINT NumViewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
		ID3D11ShaderResourceView* PSSrvs[32] = {};
		ID3D11ShaderResourceView* VSSrvs[32] = {};

		explicit FScopedD3DRenderState(ID3D11DeviceContext* InContext)
			: Context(InContext)
		{
			if (!Context)
			{
				return;
			}

			Context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, RTVs, &DSV);
			Context->RSGetViewports(&NumViewports, Viewports);
			Context->PSGetShaderResources(0, ARRAYSIZE(PSSrvs), PSSrvs);
			Context->VSGetShaderResources(0, ARRAYSIZE(VSSrvs), VSSrvs);
		}

		~FScopedD3DRenderState()
		{
			if (!Context)
			{
				return;
			}

			Context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, RTVs, DSV);
			if (NumViewports > 0)
			{
				Context->RSSetViewports(NumViewports, Viewports);
			}
			Context->PSSetShaderResources(0, ARRAYSIZE(PSSrvs), PSSrvs);
			Context->VSSetShaderResources(0, ARRAYSIZE(VSSrvs), VSSrvs);

			for (ID3D11RenderTargetView*& RTV : RTVs)
			{
				if (RTV)
				{
					RTV->Release();
					RTV = nullptr;
				}
			}
			if (DSV)
			{
				DSV->Release();
				DSV = nullptr;
			}
			for (ID3D11ShaderResourceView*& SRV : PSSrvs)
			{
				if (SRV)
				{
					SRV->Release();
					SRV = nullptr;
				}
			}
			for (ID3D11ShaderResourceView*& SRV : VSSrvs)
			{
				if (SRV)
				{
					SRV->Release();
					SRV = nullptr;
				}
			}
		}
	};

	bool ExpandActorBounds(AActor* Actor, FBoundingBox& OutBounds)
	{
		if (!Actor || !IsAliveObject(Actor) || !Actor->IsVisible())
		{
			return false;
		}

		bool bAnyBounds = false;
		for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
		{
			if (!Primitive || !IsAliveObject(Primitive) || !Primitive->IsVisible())
			{
				continue;
			}

			FBoundingBox Bounds = Primitive->GetWorldBoundingBox();
			if (!Bounds.IsValid())
			{
				continue;
			}

			OutBounds.Expand(Bounds.Min);
			OutBounds.Expand(Bounds.Max);
			bAnyBounds = true;
		}

		return bAnyBounds;
	}

	float ComputeRadius(const FBoundingBox& Bounds)
	{
		const FVector Extent = Bounds.GetExtent();
		const float RadiusSq = Extent.X * Extent.X + Extent.Y * Extent.Y + Extent.Z * Extent.Z;
		return (std::max)(std::sqrt(RadiusSq), SnapshotMinRadius);
	}

	FVector GetSafeUpVector(const FVector& Forward)
	{
		if (std::abs(Forward.Dot(FVector::UpVector)) > 0.96f)
		{
			return FVector::RightVector;
		}
		return FVector::UpVector;
	}

	void FillSnapshotFrame(FFrameContext& Frame, const FViewport& Viewport, const FBoundingBox& Bounds)
	{
		const float Aspect = (Viewport.GetHeight() > 0)
			? static_cast<float>(Viewport.GetWidth()) / static_cast<float>(Viewport.GetHeight())
			: 1.0f;
		const FVector Center = Bounds.GetCenter();
		const float Radius = ComputeRadius(Bounds) * SnapshotPadding;

		FVector CameraDir = FVector(-1.0f, -1.0f, -0.45f).Normalized();
		const FVector UpHint = GetSafeUpVector(CameraDir);
		const float HalfFovY = SnapshotFovY * 0.5f;
		const float HalfFovX = std::atan(std::tan(HalfFovY) * Aspect);
		const float FitHalfFov = (std::min)(HalfFovX, HalfFovY);
		const float Distance = Radius / (std::max)(std::tan(FitHalfFov), 0.001f);
		const FVector Eye = Center - CameraDir * Distance;

		FVector Forward = (Center - Eye).Normalized();
		FVector Right = UpHint.Cross(Forward).Normalized();
		FVector Up = Forward.Cross(Right).Normalized();

		Frame.ClearViewportResources();
		Frame.View = FMatrix::LookAtLH(Eye, Center, UpHint);
		Frame.Proj = FMatrix::PerspectiveFovLH(SnapshotFovY, Aspect, 0.01f, Distance + Radius * 4.0f);
		Frame.CameraPosition = Eye;
		Frame.CameraForward = Forward;
		Frame.CameraRight = Right;
		Frame.CameraUp = Up;
		Frame.NearClip = 0.01f;
		Frame.FarClip = Distance + Radius * 4.0f;
		Frame.bIsOrtho = false;
		Frame.SetViewportInfo(&Viewport);
		Frame.FrustumVolume.UpdateFromMatrix(Frame.View * Frame.Proj);
		Frame.LODContext.CameraPos = Eye;
		Frame.LODContext.bForceFullRefresh = true;
		Frame.LODContext.bValid = true;
		Frame.CursorViewportX = UINT32_MAX;
		Frame.CursorViewportY = UINT32_MAX;

		FViewportRenderOptions Options;
		Options.ViewMode = EViewMode::Lit_Phong;
		Options.ViewportType = ELevelViewportType::Perspective;
		Options.LightCullingMode = ELightCullingMode::Off;
		Options.ShowFlags.bGrid = false;
		Options.ShowFlags.bWorldAxis = false;
		Options.ShowFlags.bGizmo = false;
		Options.ShowFlags.bBillboardText = false;
		Options.ShowFlags.bBoundingVolume = false;
		Options.ShowFlags.bCollisionShapes = false;
		Options.ShowFlags.bDebugDraw = false;
		Options.ShowFlags.bOctree = false;
		Options.ShowFlags.bPickingBVH = false;
		Options.ShowFlags.bCollisionBVH = false;
		Options.ShowFlags.bFog = false;
		Options.ShowFlags.bFXAA = false;
		Options.ShowFlags.bViewLightCulling = false;
		Options.ShowFlags.bVisualize25DCulling = false;
		Options.ShowFlags.bShowShadowFrustum = false;
		Options.ShowFlags.bVignette = false;
		Options.ShowFlags.bFade = false;
		Frame.SetRenderOptions(Options);
	}

	void AddSnapshotLighting(FScene& Scene)
	{
		FGlobalAmbientLightParams Ambient;
		Ambient.Intensity = 0.85f;
		Ambient.LightColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		Ambient.bVisible = true;
		Ambient.bCastShadows = false;
		Scene.GetEnvironment().AddGlobalAmbientLight(nullptr, Ambient);
	}

	void AddActorProxiesToScene(const TArray<AActor*>& Actors, FScene& Scene)
	{
		for (AActor* Actor : Actors)
		{
			if (!Actor || !IsAliveObject(Actor) || !Actor->IsVisible())
			{
				continue;
			}

			for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
			{
				if (!Primitive || !IsAliveObject(Primitive) || !Primitive->IsVisible())
				{
					continue;
				}

				if (FPrimitiveSceneProxy* Proxy = Primitive->CreateSceneProxy())
				{
					Scene.RegisterProxy(Proxy);
				}
			}
		}
	}

	void BuildSnapshotCommands(
		FDrawCommandBuilder& Builder,
		const FFrameContext& Frame,
		FScene& Scene)
	{
		FCollectOutput Output;
		Builder.BeginCollect(Frame, Scene.GetProxyCount());

		Scene.UpdateDirtyProxies();

		const TArray<FPrimitiveSceneProxy*>& Proxies = Scene.GetAllProxies();
		Output.FrustumVisibleProxies.reserve(Proxies.size());
		Output.RenderableProxies.reserve(Proxies.size());

		for (FPrimitiveSceneProxy* Proxy : Proxies)
		{
			if (!Proxy || !Proxy->IsVisible())
			{
				continue;
			}

			if (Proxy->HasProxyFlag(EPrimitiveProxyFlags::PerViewportUpdate))
			{
				Proxy->UpdatePerViewport(Frame);
			}

			Output.FrustumVisibleProxies.push_back(Proxy);
			Output.RenderableProxies.push_back(Proxy);
			Output.VisibleProxySet.insert(Proxy);
		}

		Builder.BuildCommands(Frame, &Scene, Output);
	}

	ID3D11ShaderResourceView* CopyViewportToOwnedSRV(FD3DDevice& Device, FViewport& Viewport)
	{
		ID3D11Device* D3DDevice = Device.GetDevice();
		ID3D11DeviceContext* Context = Device.GetDeviceContext();
		ID3D11Texture2D* SourceTexture = Viewport.GetRTTexture();
		if (!D3DDevice || !Context || !SourceTexture)
		{
			return nullptr;
		}

		D3D11_TEXTURE2D_DESC Desc = {};
		SourceTexture->GetDesc(&Desc);
		Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		Desc.CPUAccessFlags = 0;
		Desc.MiscFlags = 0;
		Desc.Usage = D3D11_USAGE_DEFAULT;

		ID3D11Texture2D* CopyTexture = nullptr;
		if (FAILED(D3DDevice->CreateTexture2D(&Desc, nullptr, &CopyTexture)))
		{
			return nullptr;
		}

		Context->CopyResource(CopyTexture, SourceTexture);

		ID3D11ShaderResourceView* ResultSRV = nullptr;
		if (FAILED(D3DDevice->CreateShaderResourceView(CopyTexture, nullptr, &ResultSRV)))
		{
			CopyTexture->Release();
			return nullptr;
		}

		CopyTexture->Release();
		return ResultSRV;
	}
}

FSnapShotRenderer& FSnapShotRenderer::Get()
{
	static FSnapShotRenderer Instance;
	return Instance;
}

bool FSnapShotRenderer::Initialize(FD3DDevice& InDevice, uint32 InWidth, uint32 InHeight)
{
	Release();

	Device = &InDevice;
	Width = (std::max)(InWidth, 1u);
	Height = (std::max)(InHeight, 1u);

	ID3D11Device* D3DDevice = Device->GetDevice();
	if (!D3DDevice || !Device->GetDeviceContext())
	{
		Device = nullptr;
		return false;
	}

	if (!Viewport.Initialize(D3DDevice, Width, Height))
	{
		Device = nullptr;
		Width = 0;
		Height = 0;
		return false;
	}

	Resources.Create(D3DDevice);
	bResourcesCreated = true;

	Pipeline.Initialize();
	bPipelineInitialized = true;

	return true;
}

void FSnapShotRenderer::Release()
{
	Clear();

	Viewport.Release();

	if (bResourcesCreated)
	{
		Resources.Release();
		bResourcesCreated = false;
	}

	bPipelineInitialized = false;
	Device = nullptr;
	Width = 0;
	Height = 0;
}

void FSnapShotRenderer::DrawActor(AActor* Actor)
{
	if (!Actor || !IsAliveObject(Actor))
	{
		return;
	}

	if (std::find(PendingActors.begin(), PendingActors.end(), Actor) == PendingActors.end())
	{
		PendingActors.push_back(Actor);
	}
}

ID3D11ShaderResourceView* FSnapShotRenderer::GetSnapShot()
{
	if (!Device || !bResourcesCreated || !bPipelineInitialized || PendingActors.empty())
	{
		Clear();
		return nullptr;
	}

	FBoundingBox CombinedBounds;
	bool bHasBounds = false;
	for (AActor* Actor : PendingActors)
	{
		bHasBounds |= ExpandActorBounds(Actor, CombinedBounds);
	}

	if (!bHasBounds || !CombinedBounds.IsValid())
	{
		Clear();
		return nullptr;
	}

	ID3D11DeviceContext* Context = Device->GetDeviceContext();
	if (!Context)
	{
		Clear();
		return nullptr;
	}

	FScopedD3DRenderState RestoreState(Context);

	ID3D11ShaderResourceView* NullSRVs[32] = {};
	Context->PSSetShaderResources(0, ARRAYSIZE(NullSRVs), NullSRVs);
	Context->VSSetShaderResources(0, ARRAYSIZE(NullSRVs), NullSRVs);
	Context->CSSetShaderResources(0, ARRAYSIZE(NullSRVs), NullSRVs);

	const float ClearColor[4] = { 0.18f, 0.18f, 0.18f, 1.0f };
	Viewport.BeginRender(Context, ClearColor);

	FFrameContext Frame;
	FillSnapshotFrame(Frame, Viewport, CombinedBounds);

	FScene Scene;
	AddSnapshotLighting(Scene);
	AddActorProxiesToScene(PendingActors, Scene);

	if (Scene.GetProxyCount() == 0)
	{
		Clear();
		return nullptr;
	}

	FDrawCommandBuilder Builder;
	Builder.Create(Device->GetDevice(), Context, &Pipeline.GetStateTable());
	BuildSnapshotCommands(Builder, Frame, Scene);

	Resources.UpdateFrameBuffer(*Device, Frame);
	Resources.UpdateLightBuffer(*Device, Scene, Frame);
	Resources.BindSystemSamplers(*Device);

	FDrawCommandList& CommandList = Builder.GetCommandList();
	CommandList.Sort();

	FStateCache Cache;
	Cache.Reset();
	Cache.RTV = Frame.ViewportRTV;
	Cache.DSV = Frame.ViewportDSV;

	FPassContext PassCtx{ *Device, Frame, Cache, Resources, CommandList, nullptr, &Scene };
	Pipeline.Execute(PassCtx);

	Resources.UnbindSystemTextures(*Device);
	Resources.UnbindTileCullingBuffers(*Device);
	Resources.UnbindClusterCullingResources(*Device);
	Cache.Cleanup(Context);
	CommandList.Reset();
	Builder.Release();

	ID3D11ShaderResourceView* ResultSRV = CopyViewportToOwnedSRV(*Device, Viewport);

	Viewport.BeginRender(Context, ClearColor);
	Clear();
	return ResultSRV;
}

void FSnapShotRenderer::Clear()
{
	PendingActors.clear();
}

void FSnapShotRenderer::Resize(uint32 InWidth, uint32 InHeight)
{
	if (!Device)
	{
		return;
	}

	Width = (std::max)(InWidth, 1u);
	Height = (std::max)(InHeight, 1u);
	Viewport.Resize(Width, Height);
}
